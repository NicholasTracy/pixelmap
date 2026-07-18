#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "config_store.h"
#include "led_strip.h"
#include "pixel_map.h"
#include "effects.h"
#include "wifi_mgr.h"
#include "web_ui.h"
#include "artnet.h"
#include "sacn.h"
#include "status_led.h"

static const char *TAG = "pixelmap";

static pm_app_config_t s_cfg;
static pm_led_strip_t *s_strip;
static pm_pixel_map_t *s_map;
static uint8_t *s_dmx_merge;
static size_t s_dmx_merge_len;
static volatile bool s_rebuild;
static volatile bool s_strip_fault;

static void apply_correction(void)
{
    if (!s_strip) return;
    pm_color_correction_t cc = *pm_led_strip_get_correction(s_strip);
    cc.brightness = s_cfg.brightness;
    cc.gamma = s_cfg.gamma;
    cc.auto_white = s_cfg.auto_white;
    /* FastLED-ish typical LED strip correction */
    cc.correction = (pm_rgb_t){255, 224, 204};
    cc.temperature = (pm_rgb_t){255, 255, 255};
    pm_led_strip_set_correction(s_strip, &cc);
}

static esp_err_t rebuild_strip(void)
{
    if (s_strip) {
        pm_led_strip_destroy(s_strip);
        s_strip = NULL;
    }
    pm_led_strip_config_t sc = {
        .gpio_data = s_cfg.gpio_data,
        .gpio_clock = s_cfg.gpio_clock,
        .chipset = s_cfg.chipset,
        .color_order = s_cfg.color_order,
        .pixel_count = s_cfg.pixel_count,
        .rmt_resolution_hz_mhz = 10,
    };
    esp_err_t err = pm_led_strip_create(&sc, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "strip create failed: %s", esp_err_to_name(err));
        s_strip_fault = true;
        return err;
    }
    s_strip_fault = false;
    apply_correction();

    free(s_dmx_merge);
    s_dmx_merge_len = (size_t)s_cfg.pixel_count * 3;
    s_dmx_merge = calloc(1, s_dmx_merge_len);
    return ESP_OK;
}

static void update_status_led(bool dmx_active)
{
    if (s_strip_fault) {
        pm_status_led_set_mode(PM_STATUS_FAULT_STRIP);
        return;
    }
    if (dmx_active) {
        pm_status_led_set_mode(PM_STATUS_DMX_ACTIVE);
        return;
    }
    if (pm_wifi_sta_connected()) {
        pm_status_led_set_mode(PM_STATUS_OK);
        return;
    }
    if (pm_wifi_ap_active()) {
        /* Prefer AP indication when serving setup network */
        pm_status_led_set_mode(PM_STATUS_WIFI_AP);
        return;
    }
    if (pm_wifi_sta_connecting()) {
        pm_status_led_set_mode(PM_STATUS_WIFI_CONNECTING);
        return;
    }
    pm_status_led_set_mode(PM_STATUS_WIFI_AP);
}

static void on_config_changed(void)
{
    s_rebuild = true;
    apply_correction();
}

static void on_map_changed(void)
{
    /* map pointer already mutated in place */
}

static void set_px(void *user, uint16_t strip_index, pm_rgb_t color)
{
    (void)user;
    pm_led_strip_set_rgb(s_strip, strip_index, color);
}

static void on_dmx(uint16_t universe, const uint8_t *data, uint16_t len, void *user)
{
    uint16_t base_uni = user ? *(const uint16_t *)user : 0;
    if (universe < base_uni) return;
    uint32_t offset = (uint32_t)(universe - base_uni) * 512;
    if (!s_dmx_merge) return;
    for (uint16_t i = 0; i < len && (offset + i) < s_dmx_merge_len; ++i) {
        s_dmx_merge[offset + i] = data[i];
    }
}

static void render_from_dmx(void)
{
    if (!s_strip || !s_dmx_merge) return;
    uint16_t n = pm_led_strip_length(s_strip);
    for (uint16_t i = 0; i < n; ++i) {
        size_t o = (size_t)i * 3;
        if (o + 2 >= s_dmx_merge_len) break;
        pm_led_strip_set_rgb(s_strip, i, (pm_rgb_t){
            s_dmx_merge[o], s_dmx_merge[o + 1], s_dmx_merge[o + 2]
        });
    }
}

static void render_loop(void *arg)
{
    (void)arg;
    while (1) {
        if (s_rebuild) {
            s_rebuild = false;
            rebuild_strip();
        }

        bool net = (s_cfg.artnet_enable && pm_artnet_active(1500)) ||
                   (s_cfg.sacn_enable && pm_sacn_active(1500));

        if (net) {
            render_from_dmx();
        } else if (s_strip && s_map) {
            pm_effect_context_t ctx = {
                .map = s_map,
                .params = {
                    .id = s_cfg.effect_id,
                    .speed = s_cfg.effect_speed,
                    .scale = s_cfg.effect_scale > 0 ? s_cfg.effect_scale : 1.0f,
                    .intensity = s_cfg.effect_intensity / 255.0f,
                    .primary = {.h = 0, .s = 255, .v = 255},
                    .secondary = {.h = 160, .s = 255, .v = 255},
                    .palette_blend = 0,
                },
                .time_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
            };
            pm_effect_render(&ctx, set_px, NULL);
        }

        if (s_strip && pm_led_strip_show(s_strip) != ESP_OK) {
            s_strip_fault = true;
        }

        update_status_led(net);
        pm_status_led_tick();

        vTaskDelay(pdMS_TO_TICKS(16)); /* ~60 FPS */
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(pm_config_load(&s_cfg));

    pm_status_led_config_t sled = {
        .gpio = s_cfg.gpio_status_led,
        .active_high = s_cfg.status_led_active_high,
        .avoid_gpio_a = s_cfg.gpio_data,
        .avoid_gpio_b = s_cfg.gpio_clock,
    };
    ESP_ERROR_CHECK(pm_status_led_init(&sled));
    pm_status_led_set_mode(PM_STATUS_BOOT);
    pm_status_led_tick();

    pm_pixel_map_config_t mc = {
        .capacity = s_cfg.pixel_count > 0 ? s_cfg.pixel_count : 512,
        .width = 1, .height = 1, .depth = 1,
    };
    if (mc.capacity < (uint16_t)(s_cfg.map_width * s_cfg.map_height)) {
        mc.capacity = (uint16_t)(s_cfg.map_width * s_cfg.map_height);
    }
    ESP_ERROR_CHECK(pm_pixel_map_create(&mc, &s_map));
    ESP_ERROR_CHECK(pm_pixel_map_build_grid(s_map, s_cfg.map_width, s_cfg.map_height, 1.0f, 0));
    pm_pixel_map_normalize(s_map);

    if (rebuild_strip() != ESP_OK) {
        pm_status_led_set_mode(PM_STATUS_FAULT_STRIP);
    }

    pm_wifi_config_t wifi = {
        .sta_ssid = s_cfg.sta_ssid,
        .sta_pass = s_cfg.sta_pass,
        .hostname = s_cfg.hostname,
        .ap_fallback = s_cfg.ap_fallback,
    };
    ESP_ERROR_CHECK(pm_wifi_start(&wifi));
    pm_status_led_set_mode(s_cfg.sta_ssid[0] ? PM_STATUS_WIFI_CONNECTING : PM_STATUS_WIFI_AP);

    pm_web_ui_hooks_t ui = {
        .cfg = &s_cfg,
        .map = s_map,
        .on_config_changed = on_config_changed,
        .on_map_changed = on_map_changed,
    };
    ESP_ERROR_CHECK(pm_web_ui_start(&ui));

    if (s_cfg.artnet_enable) {
        pm_artnet_config_t ac = {
            .listen_port = 6454,
            .universe_start = s_cfg.artnet_universe,
            .universe_count = s_cfg.universe_count,
            .on_dmx = on_dmx,
            .user = &s_cfg.artnet_universe,
        };
        ESP_ERROR_CHECK(pm_artnet_start(&ac));
    }
    if (s_cfg.sacn_enable) {
        pm_sacn_config_t sc = {
            .universe_start = s_cfg.sacn_universe,
            .universe_count = s_cfg.universe_count,
            .join_multicast = true,
            .on_dmx = on_dmx,
            .user = &s_cfg.sacn_universe,
        };
        ESP_ERROR_CHECK(pm_sacn_start(&sc));
    }

    xTaskCreate(render_loop, "render", 8192, NULL, 6, NULL);
    ESP_LOGI(TAG, "PixelMap ready — open http://%s/", pm_wifi_ip_str());
}
