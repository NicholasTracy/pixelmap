#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_spiffs.h"
#include "nvs_flash.h"

#include "config_store.h"
#include "led_strip.h"
#include "pixel_map.h"
#include "effects.h"
#include "effect_lua.h"
#include "pov.h"
#include "wifi_mgr.h"
#include "web_ui.h"
#include "artnet.h"
#include "sacn.h"
#include "status_led.h"

static const char *TAG = "pixelmap";
static const char *MAP_PATH = "/spiffs/map.json";

static pm_app_config_t s_cfg;
static pm_led_strip_t *s_buses[PM_STRIP_MAX];
static uint8_t s_bus_count;
static uint16_t s_bus_base[PM_STRIP_MAX];
static uint16_t s_total_len;
static pm_pixel_map_t *s_map;
static uint8_t *s_dmx_merge;
static size_t s_dmx_merge_len;
static volatile bool s_rebuild;
static volatile bool s_strip_fault;
static bool s_artnet_running;
static bool s_sacn_running;

static void on_dmx(uint16_t universe, const uint8_t *data, uint16_t len, void *user);

static void destroy_buses(void)
{
    for (uint8_t i = 0; i < PM_STRIP_MAX; ++i) {
        if (s_buses[i]) {
            pm_led_strip_destroy(s_buses[i]);
            s_buses[i] = NULL;
        }
    }
    s_bus_count = 0;
    s_total_len = 0;
}

static void apply_correction(void)
{
    for (uint8_t i = 0; i < s_bus_count; ++i) {
        if (!s_buses[i]) continue;
        pm_color_correction_t cc = *pm_led_strip_get_correction(s_buses[i]);
        cc.brightness = s_cfg.brightness;
        cc.gamma = s_cfg.gamma;
        cc.auto_white = s_cfg.auto_white; /* from Strip tab / API */
        /* FastLED-ish typical LED strip correction */
        cc.correction = (pm_rgb_t){255, 224, 204};
        cc.temperature = (pm_rgb_t){255, 255, 255};
        pm_led_strip_set_correction(s_buses[i], &cc);
    }
}

static bool resolve_bus(uint16_t global_index, pm_led_strip_t **bus, uint16_t *local)
{
    for (uint8_t i = 0; i < s_bus_count; ++i) {
        uint16_t len = pm_led_strip_length(s_buses[i]);
        if (global_index < s_bus_base[i] + len) {
            *bus = s_buses[i];
            *local = (uint16_t)(global_index - s_bus_base[i]);
            return true;
        }
    }
    return false;
}

static esp_err_t rebuild_strip(void)
{
    destroy_buses();
    pm_config_sync_strips(&s_cfg);

    uint16_t base = 0;
    esp_err_t err = ESP_OK;
    for (uint8_t i = 0; i < s_cfg.strip_count; ++i) {
        pm_led_strip_config_t sc = {
            .gpio_data = s_cfg.strip_gpio[i],
            .gpio_clock = s_cfg.gpio_clock,
            .chipset = s_cfg.chipset,
            .color_order = s_cfg.color_order,
            .pixel_count = s_cfg.strip_len[i],
            .rmt_resolution_hz_mhz = 10,
        };
        err = pm_led_strip_create(&sc, &s_buses[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "strip %u create failed (gpio %d): %s",
                     (unsigned)(i + 1), s_cfg.strip_gpio[i], esp_err_to_name(err));
            destroy_buses();
            s_strip_fault = true;
            return err;
        }
        s_bus_base[i] = base;
        base = (uint16_t)(base + s_cfg.strip_len[i]);
        s_bus_count++;
    }
    s_total_len = base;
    s_strip_fault = false;
    apply_correction();

    free(s_dmx_merge);
    s_dmx_merge = NULL;
    s_dmx_merge_len = (size_t)s_total_len * 3;
    if (s_dmx_merge_len > 0) {
        s_dmx_merge = calloc(1, s_dmx_merge_len);
        if (!s_dmx_merge) {
            ESP_LOGE(TAG, "dmx merge buffer alloc failed (%u bytes)", (unsigned)s_dmx_merge_len);
            s_dmx_merge_len = 0;
        }
    }
    if (s_map && s_total_len > 0) {
        esp_err_t merr = pm_pixel_map_ensure_capacity(s_map, s_total_len);
        if (merr != ESP_OK) {
            ESP_LOGE(TAG, "map capacity grow failed: %s", esp_err_to_name(merr));
        }
    }
    ESP_LOGI(TAG, "buses=%u total_leds=%u", (unsigned)s_bus_count, (unsigned)s_total_len);
    return ESP_OK;
}

static void persist_map(void)
{
    if (!s_map) return;
    esp_err_t err = pm_pixel_map_save_path(s_map, MAP_PATH);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "map save failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "map persisted (%u points)", (unsigned)pm_pixel_map_count(s_map));
    }
}

static void sync_dmx_receivers(void)
{
    if (s_artnet_running && !s_cfg.artnet_enable) {
        pm_artnet_stop();
        s_artnet_running = false;
        ESP_LOGI(TAG, "Art-Net stopped");
    }
    if (s_sacn_running && !s_cfg.sacn_enable) {
        pm_sacn_stop();
        s_sacn_running = false;
        ESP_LOGI(TAG, "sACN stopped");
    }

    if (s_cfg.artnet_enable) {
        if (s_artnet_running) {
            pm_artnet_stop();
            s_artnet_running = false;
        }
        pm_artnet_config_t ac = {
            .listen_port = 6454,
            .universe_start = s_cfg.artnet_universe,
            .universe_count = s_cfg.universe_count,
            .on_dmx = on_dmx,
            .user = &s_cfg.artnet_universe,
        };
        if (pm_artnet_start(&ac) == ESP_OK) {
            s_artnet_running = true;
            ESP_LOGI(TAG, "Art-Net started");
        } else {
            ESP_LOGW(TAG, "Art-Net start failed");
        }
    }

    if (s_cfg.sacn_enable) {
        if (s_sacn_running) {
            pm_sacn_stop();
            s_sacn_running = false;
        }
        pm_sacn_config_t sc = {
            .universe_start = s_cfg.sacn_universe,
            .universe_count = s_cfg.universe_count,
            .join_multicast = true,
            .on_dmx = on_dmx,
            .user = &s_cfg.sacn_universe,
        };
        if (pm_sacn_start(&sc) == ESP_OK) {
            s_sacn_running = true;
            ESP_LOGI(TAG, "sACN started");
        } else {
            ESP_LOGW(TAG, "sACN start failed");
        }
    }
}

static void apply_wifi_from_cfg(void)
{
    pm_wifi_config_t wifi = {
        .sta_ssid = s_cfg.sta_ssid,
        .sta_pass = s_cfg.sta_pass,
        .hostname = s_cfg.hostname,
        .ap_fallback = s_cfg.ap_fallback,
    };
    esp_err_t err = pm_wifi_apply(&wifi);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "wifi apply failed: %s", esp_err_to_name(err));
    }
}

static esp_err_t mount_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 4,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
    }
    return err;
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

static void apply_pov_map_if_needed(void)
{
    if (!s_map || !s_cfg.pov_enable) return;
    uint16_t n = s_cfg.pixel_count > 0 ? s_cfg.pixel_count : pm_pixel_map_count(s_map);
    if (n == 0) return;
    pm_pov_build_strip_map(s_map, n, s_cfg.pov_layout, s_cfg.pov_blade_count);
}

static void apply_spatial_map(void)
{
    if (!s_map) return;
    if (s_cfg.pov_enable) {
        apply_pov_map_if_needed();
        return;
    }
    float sp = s_cfg.map_spacing > 1e-4f ? s_cfg.map_spacing : 1.0f;
    uint16_t n = s_cfg.pixel_count > 0 ? s_cfg.pixel_count : 1;
    pm_map_layout_t layout = s_cfg.map_layout;

    if (s_cfg.map_dim == PM_MAP_DIM_2D && layout == PM_MAP_LAYOUT_SPHERE) {
        layout = PM_MAP_LAYOUT_CIRCLE;
    }
    if (s_cfg.map_dim == PM_MAP_DIM_3D && layout == PM_MAP_LAYOUT_CIRCLE) {
        layout = PM_MAP_LAYOUT_SPHERE;
    }

    uint8_t fill = (uint8_t)s_cfg.map_fill;
    bool open_tb = s_cfg.map_open_tb;

    if (layout == PM_MAP_LAYOUT_CUSTOM) {
        /* Keep whatever was last imported / formula-built */
        return;
    }
    uint16_t w = s_cfg.map_width > 0 ? s_cfg.map_width : 1;
    uint16_t h = s_cfg.map_height > 0 ? s_cfg.map_height : 1;
    uint16_t d = s_cfg.map_depth > 0 ? s_cfg.map_depth : 1;

    /* 3D Grid is the same as solid Box — prefer Box path */
    if (s_cfg.map_dim == PM_MAP_DIM_3D && layout == PM_MAP_LAYOUT_GRID) {
        layout = PM_MAP_LAYOUT_BOX;
        fill = 1;
    }

    if (layout == PM_MAP_LAYOUT_CIRCLE) {
        pm_pixel_map_build_circle(s_map, w, sp, 0, n, fill);
        pm_pixel_map_normalize_uniform(s_map);
        return;
    }
    if (layout == PM_MAP_LAYOUT_SPHERE) {
        pm_pixel_map_build_sphere(s_map, w, sp, 0, n, fill);
        pm_pixel_map_normalize_uniform(s_map);
        return;
    }
    if (layout == PM_MAP_LAYOUT_BOX) {
        pm_pixel_map_build_box(s_map, w, h, d, sp, 0, n, fill, open_tb && fill == 0);
        pm_pixel_map_normalize_uniform(s_map);
        return;
    }
    if (layout == PM_MAP_LAYOUT_CYLINDER) {
        pm_pixel_map_build_cylinder(s_map, w, h, sp, 0, n, open_tb);
        pm_pixel_map_normalize_uniform(s_map);
        return;
    }
    if (layout == PM_MAP_LAYOUT_DOME) {
        pm_pixel_map_build_dome(s_map, w, sp, 0, n);
        pm_pixel_map_normalize_uniform(s_map);
        return;
    }
    if (layout == PM_MAP_LAYOUT_PYRAMID) {
        pm_pixel_map_build_pyramid(s_map, w, h, sp, 0, n);
        pm_pixel_map_normalize_uniform(s_map);
        return;
    }

    /* 2D grid */
    pm_pixel_map_build_grid(s_map, w, h, sp, 0, n);
    pm_pixel_map_normalize_uniform(s_map);
}

static void on_config_changed(void)
{
    s_rebuild = true;
    apply_correction();
    /* Map points are owned by /api/map (+ SPIFFS). Layout params only seed
     * a fresh map when no persisted map exists (boot). Avoid wiping wire-order. */
    apply_wifi_from_cfg();
    sync_dmx_receivers();
}

static void on_map_changed(void)
{
    persist_map();
}

static void set_px(void *user, uint16_t strip_index, pm_rgb_t color)
{
    (void)user;
    pm_led_strip_t *bus = NULL;
    uint16_t local = 0;
    if (resolve_bus(strip_index, &bus, &local)) {
        pm_led_strip_set_rgb(bus, local, color);
    }
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
    if (!s_bus_count || !s_dmx_merge) return;
    for (uint16_t i = 0; i < s_total_len; ++i) {
        size_t o = (size_t)i * 3;
        if (o + 2 >= s_dmx_merge_len) break;
        pm_led_strip_t *bus = NULL;
        uint16_t local = 0;
        if (!resolve_bus(i, &bus, &local)) continue;
        pm_led_strip_set_rgb(bus, local, (pm_rgb_t){
            s_dmx_merge[o], s_dmx_merge[o + 1], s_dmx_merge[o + 2]
        });
    }
}

static esp_err_t show_all_buses(void)
{
    esp_err_t err = ESP_OK;
    for (uint8_t i = 0; i < s_bus_count; ++i) {
        esp_err_t e = pm_led_strip_show(s_buses[i]);
        if (e != ESP_OK) err = e;
    }
    return err;
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

        if (net && s_cfg.dmx_mode == PM_DMX_MODE_PIXELS) {
            render_from_dmx();
        } else if (s_bus_count && s_map) {
            pm_app_config_t fxcfg = s_cfg;
            uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
            if (net && s_cfg.dmx_mode == PM_DMX_MODE_PARAMS && s_dmx_merge) {
                pm_config_apply_fx_dmx(&fxcfg, s_dmx_merge, s_dmx_merge_len);
            }
            /* Local modulators only when Art-Net / sACN is not enabled */
            if (!s_cfg.artnet_enable && !s_cfg.sacn_enable) {
                pm_config_apply_fx_mods(&fxcfg, now_ms);
            }
            pm_effect_params_t params;
            pm_config_fill_effect_params(&fxcfg, &params);
            pm_effect_context_t ctx = {
                .map = s_map,
                .params = params,
                .time_ms = now_ms,
                .pov_enabled = s_cfg.pov_enable,
                .pov = {
                    .mode = s_cfg.pov_enable ? s_cfg.pov_mode : PM_POV_OFF,
                    .layout = s_cfg.pov_layout,
                    .blade_count = s_cfg.pov_blade_count,
                    .rpm = s_cfg.pov_rpm,
                    .linear_speed_mps = s_cfg.pov_linear_speed_mps,
                    .radius_m = s_cfg.pov_radius_m,
                    .path_length_m = s_cfg.pov_path_length_m,
                },
                .strip_len = s_total_len,
            };
            pm_effect_render(&ctx, set_px, NULL);
        }

        if (s_bus_count && show_all_buses() != ESP_OK) {
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

    err = pm_config_load(&s_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "config load failed (%s); using defaults", esp_err_to_name(err));
        pm_config_set_defaults(&s_cfg);
    }
    err = pm_effect_lua_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "lua init failed: %s", esp_err_to_name(err));
    }

    mount_spiffs();

    int avoid[PM_STRIP_MAX + 1];
    uint8_t avoid_n = 0;
    for (uint8_t i = 0; i < s_cfg.strip_count && i < PM_STRIP_MAX; ++i) {
        avoid[avoid_n++] = s_cfg.strip_gpio[i];
    }
    avoid[avoid_n++] = s_cfg.gpio_clock;
    pm_status_led_config_t sled = {
        .gpio = s_cfg.gpio_status_led,
        .active_high = s_cfg.status_led_active_high,
        .avoid_gpios = avoid,
        .avoid_count = avoid_n,
    };
    ESP_ERROR_CHECK(pm_status_led_init(&sled));
    pm_status_led_set_mode(PM_STATUS_BOOT);
    pm_status_led_tick();

    pm_pixel_map_config_t mc = {
        .capacity = s_cfg.pixel_count > 0 ? s_cfg.pixel_count : 512,
        .width = 1, .height = 1, .depth = 1,
    };
    ESP_ERROR_CHECK(pm_pixel_map_create(&mc, &s_map));
    if (pm_pixel_map_load_path(s_map, MAP_PATH) == ESP_OK && pm_pixel_map_count(s_map) > 0) {
        ESP_LOGI(TAG, "loaded persisted map (%u points)", (unsigned)pm_pixel_map_count(s_map));
    } else {
        apply_spatial_map();
        persist_map();
    }

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

    sync_dmx_receivers();

    xTaskCreate(render_loop, "render", 12288, NULL, 6, NULL);
    ESP_LOGI(TAG, "PixelMap ready — open http://%s/", pm_wifi_ip_str());
}
