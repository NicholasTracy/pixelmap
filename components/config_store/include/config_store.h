#pragma once

#include "led_chipsets.h"
#include "effects.h"
#include "pov.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_WIFI_SSID_MAX 32
#define PM_WIFI_PASS_MAX 64

typedef struct {
    char sta_ssid[PM_WIFI_SSID_MAX];
    char sta_pass[PM_WIFI_PASS_MAX];
    char hostname[32];
    bool ap_fallback;

    int gpio_data;
    int gpio_clock;
    int gpio_status_led;      /* WLED-style onboard status LED; <0 disables */
    bool status_led_active_high;
    pm_chipset_t chipset;
    pm_color_order_t color_order;
    uint16_t pixel_count;
    uint8_t brightness;
    uint8_t gamma;
    bool auto_white;

    pm_effect_id_t effect_id;
    float effect_speed;
    float effect_scale;
    uint8_t effect_intensity; /* 0-255 */

    uint16_t artnet_universe;
    uint16_t sacn_universe;
    uint16_t universe_count;
    bool artnet_enable;
    bool sacn_enable;

    uint16_t map_width;
    uint16_t map_height;

    /* Persistence-of-vision mapping (fan / wand) */
    bool pov_enable;
    pm_pov_mode_t pov_mode;
    pm_pov_layout_t pov_layout;
    float pov_rpm;              /* fixed rotation speed for now */
    float pov_linear_speed_mps; /* wand sweep speed */
    float pov_radius_m;
    float pov_path_length_m;
} pm_app_config_t;

void pm_config_set_defaults(pm_app_config_t *cfg);
esp_err_t pm_config_load(pm_app_config_t *cfg);
esp_err_t pm_config_save(const pm_app_config_t *cfg);

#ifdef __cplusplus
}
#endif
