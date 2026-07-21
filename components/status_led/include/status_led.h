#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* WLED-style onboard status LED (ESP32 DevKit / many controllers: GPIO 2). */
#define PM_STATUS_LED_GPIO_DEFAULT 2

typedef enum {
    PM_STATUS_BOOT = 0,        /* soft ramp-in, then hold */
    PM_STATUS_WIFI_CONNECTING, /* fast PWM breath — joining STA */
    PM_STATUS_WIFI_AP,         /* slow calm breath — setup / AP */
    PM_STATUS_OK,              /* soft double-heartbeat */
    PM_STATUS_DMX_ACTIVE,      /* medium sawtooth ramp — Art-Net / sACN */
    PM_STATUS_FAULT_STRIP,     /* sharp urgent peaks — LED output failed */
    PM_STATUS_FAULT_GENERAL,   /* soft-edged SOS — serious fault */
} pm_status_mode_t;

typedef struct {
    int gpio;                 /* <0 disables */
    bool active_high;         /* ESP32 DevKit onboard LED is active-high */
    const int *avoid_gpios;   /* data pins (and clock) that must not collide */
    uint8_t avoid_count;
} pm_status_led_config_t;

esp_err_t pm_status_led_init(const pm_status_led_config_t *cfg);
void pm_status_led_deinit(void);

void pm_status_led_set_mode(pm_status_mode_t mode);
pm_status_mode_t pm_status_led_get_mode(void);
bool pm_status_led_enabled(void);

/* Call from a low-rate task / render loop (~10–50 ms). Drives PWM ramps. */
void pm_status_led_tick(void);

#ifdef __cplusplus
}
#endif
