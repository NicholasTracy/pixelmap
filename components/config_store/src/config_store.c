#include "config_store.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

#define NS "pixelmap"

void pm_config_set_defaults(pm_app_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->hostname, "pixelmap", sizeof(cfg->hostname) - 1);
    cfg->ap_fallback = true;
    cfg->gpio_data = 16;   /* WLED-style default */
    cfg->gpio_clock = 14;
    cfg->gpio_status_led = 2; /* ESP32 DevKit / common WLED onboard LED */
    cfg->status_led_active_high = true;
    cfg->chipset = PM_CHIPSET_WS2812B;
    cfg->color_order = PM_COLOR_ORDER_GRB;
    cfg->pixel_count = 60;
    cfg->brightness = 128;
    cfg->gamma = 220;
    cfg->auto_white = true;
    cfg->effect_id = PM_EFFECT_RAINBOW_SPATIAL;
    cfg->effect_speed = 1.0f;
    cfg->effect_scale = 1.0f;
    cfg->effect_intensity = 255;
    cfg->artnet_universe = 0;
    cfg->sacn_universe = 1;
    cfg->universe_count = 4;
    cfg->artnet_enable = true;
    cfg->sacn_enable = true;
    cfg->map_width = 10;
    cfg->map_height = 6;

    cfg->pov_enable = false;
    cfg->pov_mode = PM_POV_ROTATION;
    cfg->pov_layout = PM_POV_LAYOUT_DIAMETER;
    cfg->pov_rpm = 600.0f;
    cfg->pov_linear_speed_mps = 4.0f;
    cfg->pov_radius_m = 0.25f;
    cfg->pov_path_length_m = 1.0f;
}

esp_err_t pm_config_load(pm_app_config_t *cfg)
{
    pm_config_set_defaults(cfg);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    size_t len = sizeof(cfg->sta_ssid);
    nvs_get_str(h, "ssid", cfg->sta_ssid, &len);
    len = sizeof(cfg->sta_pass);
    nvs_get_str(h, "pass", cfg->sta_pass, &len);
    len = sizeof(cfg->hostname);
    nvs_get_str(h, "host", cfg->hostname, &len);

    int32_t v;
    if (nvs_get_i32(h, "gpio", &v) == ESP_OK) cfg->gpio_data = v;
    if (nvs_get_i32(h, "clk", &v) == ESP_OK) cfg->gpio_clock = v;
    if (nvs_get_i32(h, "sled", &v) == ESP_OK) cfg->gpio_status_led = v;
    if (nvs_get_i32(h, "sledh", &v) == ESP_OK) cfg->status_led_active_high = v != 0;
    if (nvs_get_i32(h, "chip", &v) == ESP_OK) cfg->chipset = (pm_chipset_t)v;
    if (nvs_get_i32(h, "order", &v) == ESP_OK) cfg->color_order = (pm_color_order_t)v;
    if (nvs_get_i32(h, "count", &v) == ESP_OK) cfg->pixel_count = (uint16_t)v;
    if (nvs_get_i32(h, "bri", &v) == ESP_OK) cfg->brightness = (uint8_t)v;
    if (nvs_get_i32(h, "gamma", &v) == ESP_OK) cfg->gamma = (uint8_t)v;
    if (nvs_get_i32(h, "aw", &v) == ESP_OK) cfg->auto_white = v != 0;
    if (nvs_get_i32(h, "fx", &v) == ESP_OK) cfg->effect_id = (pm_effect_id_t)v;
    if (nvs_get_i32(h, "fxint", &v) == ESP_OK) cfg->effect_intensity = (uint8_t)v;
    if (nvs_get_i32(h, "aun", &v) == ESP_OK) cfg->artnet_universe = (uint16_t)v;
    if (nvs_get_i32(h, "sun", &v) == ESP_OK) cfg->sacn_universe = (uint16_t)v;
    if (nvs_get_i32(h, "ucnt", &v) == ESP_OK) cfg->universe_count = (uint16_t)v;
    if (nvs_get_i32(h, "aen", &v) == ESP_OK) cfg->artnet_enable = v != 0;
    if (nvs_get_i32(h, "sen", &v) == ESP_OK) cfg->sacn_enable = v != 0;
    if (nvs_get_i32(h, "mw", &v) == ESP_OK) cfg->map_width = (uint16_t)v;
    if (nvs_get_i32(h, "mh", &v) == ESP_OK) cfg->map_height = (uint16_t)v;

    if (nvs_get_i32(h, "pove", &v) == ESP_OK) cfg->pov_enable = v != 0;
    if (nvs_get_i32(h, "povm", &v) == ESP_OK) cfg->pov_mode = (pm_pov_mode_t)v;
    if (nvs_get_i32(h, "poyl", &v) == ESP_OK) cfg->pov_layout = (pm_pov_layout_t)v;
    if (nvs_get_i32(h, "povrpm", &v) == ESP_OK) cfg->pov_rpm = (float)v / 100.0f;
    if (nvs_get_i32(h, "povspd", &v) == ESP_OK) cfg->pov_linear_speed_mps = (float)v / 100.0f;
    if (nvs_get_i32(h, "povrad", &v) == ESP_OK) cfg->pov_radius_m = (float)v / 1000.0f;
    if (nvs_get_i32(h, "povpath", &v) == ESP_OK) cfg->pov_path_length_m = (float)v / 1000.0f;

    nvs_close(h);
    return ESP_OK;
}

esp_err_t pm_config_save(const pm_app_config_t *cfg)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    nvs_set_str(h, "ssid", cfg->sta_ssid);
    nvs_set_str(h, "pass", cfg->sta_pass);
    nvs_set_str(h, "host", cfg->hostname);
    nvs_set_i32(h, "gpio", cfg->gpio_data);
    nvs_set_i32(h, "clk", cfg->gpio_clock);
    nvs_set_i32(h, "sled", cfg->gpio_status_led);
    nvs_set_i32(h, "sledh", cfg->status_led_active_high ? 1 : 0);
    nvs_set_i32(h, "chip", (int32_t)cfg->chipset);
    nvs_set_i32(h, "order", (int32_t)cfg->color_order);
    nvs_set_i32(h, "count", cfg->pixel_count);
    nvs_set_i32(h, "bri", cfg->brightness);
    nvs_set_i32(h, "gamma", cfg->gamma);
    nvs_set_i32(h, "aw", cfg->auto_white ? 1 : 0);
    nvs_set_i32(h, "fx", (int32_t)cfg->effect_id);
    nvs_set_i32(h, "fxint", cfg->effect_intensity);
    nvs_set_i32(h, "aun", cfg->artnet_universe);
    nvs_set_i32(h, "sun", cfg->sacn_universe);
    nvs_set_i32(h, "ucnt", cfg->universe_count);
    nvs_set_i32(h, "aen", cfg->artnet_enable ? 1 : 0);
    nvs_set_i32(h, "sen", cfg->sacn_enable ? 1 : 0);
    nvs_set_i32(h, "mw", cfg->map_width);
    nvs_set_i32(h, "mh", cfg->map_height);
    nvs_set_i32(h, "pove", cfg->pov_enable ? 1 : 0);
    nvs_set_i32(h, "povm", (int32_t)cfg->pov_mode);
    nvs_set_i32(h, "poyl", (int32_t)cfg->pov_layout);
    nvs_set_i32(h, "povrpm", (int32_t)(cfg->pov_rpm * 100.0f));
    nvs_set_i32(h, "povspd", (int32_t)(cfg->pov_linear_speed_mps * 100.0f));
    nvs_set_i32(h, "povrad", (int32_t)(cfg->pov_radius_m * 1000.0f));
    nvs_set_i32(h, "povpath", (int32_t)(cfg->pov_path_length_m * 1000.0f));
    err = nvs_commit(h);
    nvs_close(h);
    return err;
}
