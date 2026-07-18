#include "status_led.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "status_led";

static bool s_ready;
static bool s_enabled;
static int s_gpio = -1;
static bool s_active_high = true;
static pm_status_mode_t s_mode = PM_STATUS_BOOT;
static bool s_level;
static int64_t s_phase_us;
static int s_sos_step;

static void write_raw(bool on)
{
    if (!s_enabled || s_gpio < 0) return;
    int level = on ? (s_active_high ? 1 : 0) : (s_active_high ? 0 : 1);
    gpio_set_level((gpio_num_t)s_gpio, level);
    s_level = on;
}

static void set_periodic_blink(int64_t now_us, uint32_t period_ms)
{
    uint32_t half = period_ms / 2;
    if (half < 1) half = 1;
    uint32_t phase = (uint32_t)((now_us / 1000) % period_ms);
    write_raw(phase < half);
}

esp_err_t pm_status_led_init(const pm_status_led_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    s_ready = false;
    s_enabled = false;
    s_gpio = cfg->gpio;
    s_active_high = cfg->active_high;
    s_mode = PM_STATUS_BOOT;
    s_phase_us = esp_timer_get_time();
    s_sos_step = 0;

    if (s_gpio < 0) {
        ESP_LOGI(TAG, "disabled (no pin)");
        return ESP_OK;
    }
    if (s_gpio == cfg->avoid_gpio_a || s_gpio == cfg->avoid_gpio_b) {
        ESP_LOGW(TAG, "GPIO %d in use for LED data/clock — status LED disabled", s_gpio);
        s_gpio = -1;
        return ESP_OK;
    }

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << s_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(err));
        s_gpio = -1;
        return err;
    }

    s_enabled = true;
    s_ready = true;
    write_raw(true); /* boot solid */
    ESP_LOGI(TAG, "on GPIO %d (%s)", s_gpio, s_active_high ? "active-high" : "active-low");
    return ESP_OK;
}

void pm_status_led_deinit(void)
{
    if (s_enabled && s_gpio >= 0) {
        write_raw(false);
    }
    s_enabled = false;
    s_ready = false;
    s_gpio = -1;
}

void pm_status_led_set_mode(pm_status_mode_t mode)
{
    if (s_mode == mode) return;
    s_mode = mode;
    s_phase_us = esp_timer_get_time();
    s_sos_step = 0;
}

pm_status_mode_t pm_status_led_get_mode(void)
{
    return s_mode;
}

bool pm_status_led_enabled(void)
{
    return s_enabled;
}

void pm_status_led_tick(void)
{
    if (!s_ready || !s_enabled) return;

    int64_t now = esp_timer_get_time();

    switch (s_mode) {
    case PM_STATUS_BOOT:
        write_raw(true);
        break;

    case PM_STATUS_WIFI_CONNECTING:
        /* ~4 Hz — joining network */
        set_periodic_blink(now, 250);
        break;

    case PM_STATUS_WIFI_AP:
        /* 1 Hz — WLED-like AP / setup mode */
        set_periodic_blink(now, 1000);
        break;

    case PM_STATUS_DMX_ACTIVE:
        /* 2 Hz — live Art-Net / sACN (WLED-like “connected activity”) */
        set_periodic_blink(now, 500);
        break;

    case PM_STATUS_OK: {
        /* Quiet heartbeat: 80 ms on every 2.5 s */
        uint32_t t = (uint32_t)((now / 1000) % 2500);
        write_raw(t < 80);
        break;
    }

    case PM_STATUS_FAULT_STRIP:
        /* Rapid continuous blink */
        set_periodic_blink(now, 120);
        break;

    case PM_STATUS_FAULT_GENERAL: {
        /* SOS: ... --- ... then pause */
        static const uint16_t sos_ms[] = {
            120, 120, 120, 120, 120, 360,           /* S */
            360, 120, 360, 120, 360, 360,           /* O */
            120, 120, 120, 120, 120, 1000           /* S + gap */
        };
        static const bool sos_on[] = {
            true, false, true, false, true, false,
            true, false, true, false, true, false,
            true, false, true, false, true, false
        };
        const int n = (int)(sizeof(sos_ms) / sizeof(sos_ms[0]));
        if (s_sos_step >= n) s_sos_step = 0;
        write_raw(sos_on[s_sos_step]);
        if ((now - s_phase_us) / 1000 >= sos_ms[s_sos_step]) {
            s_phase_us = now;
            s_sos_step++;
        }
        break;
    }

    default:
        write_raw(false);
        break;
    }
}
