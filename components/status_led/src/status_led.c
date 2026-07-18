#include "status_led.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>
#include <string.h>

static const char *TAG = "status_led";

#define DUTY_RES       LEDC_TIMER_10_BIT
#define DUTY_MAX       1023
#define LEDC_MODE      LEDC_LOW_SPEED_MODE
#define LEDC_TIMER     LEDC_TIMER_3
#define LEDC_CHANNEL   LEDC_CHANNEL_7
#define PWM_FREQ_HZ    5000

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static bool s_ready;
static bool s_enabled;
static int s_gpio = -1;
static bool s_active_high = true;
static pm_status_mode_t s_mode = PM_STATUS_BOOT;
static int64_t s_mode_start_us;
static int s_sos_step;
static int64_t s_sos_step_start_us;

static uint32_t clampu(uint32_t v, uint32_t lo, uint32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void write_duty(uint32_t duty)
{
    if (!s_enabled || s_gpio < 0) return;
    duty = clampu(duty, 0, DUTY_MAX);
    if (!s_active_high) {
        duty = DUTY_MAX - duty;
    }
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

/* Smooth cosine breath, 0..1 over one period. */
static float wave_cos(uint32_t t_ms, uint32_t period_ms)
{
    if (period_ms < 1) period_ms = 1;
    float x = (float)(t_ms % period_ms) / (float)period_ms;
    return 0.5f - 0.5f * cosf(2.0f * (float)M_PI * x);
}

/* Asymmetric ramp: fast rise, slower fall (feels like activity). */
static float wave_saw(uint32_t t_ms, uint32_t period_ms, float rise_frac)
{
    if (period_ms < 1) period_ms = 1;
    if (rise_frac < 0.05f) rise_frac = 0.05f;
    if (rise_frac > 0.95f) rise_frac = 0.95f;
    float x = (float)(t_ms % period_ms) / (float)period_ms;
    if (x < rise_frac) {
        return x / rise_frac;
    }
    return 1.0f - (x - rise_frac) / (1.0f - rise_frac);
}

/* Ease a 0..1 pulse with smooth attack/release (ms windows). */
static float pulse_shaped(uint32_t t_ms, uint32_t attack_ms, uint32_t hold_ms, uint32_t release_ms)
{
    uint32_t total = attack_ms + hold_ms + release_ms;
    if (t_ms >= total) return 0.0f;
    if (t_ms < attack_ms) {
        float x = (float)t_ms / (float)attack_ms;
        return x * x * (3.0f - 2.0f * x); /* smoothstep */
    }
    t_ms -= attack_ms;
    if (t_ms < hold_ms) return 1.0f;
    t_ms -= hold_ms;
    float x = 1.0f - (float)t_ms / (float)release_ms;
    return x * x * (3.0f - 2.0f * x);
}

static uint32_t duty_from(float brightness, float min_b, float max_b)
{
    if (brightness < 0.0f) brightness = 0.0f;
    if (brightness > 1.0f) brightness = 1.0f;
    float b = min_b + (max_b - min_b) * brightness;
    return (uint32_t)(b * (float)DUTY_MAX + 0.5f);
}

esp_err_t pm_status_led_init(const pm_status_led_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    s_ready = false;
    s_enabled = false;
    s_gpio = cfg->gpio;
    s_active_high = cfg->active_high;
    s_mode = PM_STATUS_BOOT;
    s_mode_start_us = esp_timer_get_time();
    s_sos_step = 0;
    s_sos_step_start_us = s_mode_start_us;

    if (s_gpio < 0) {
        ESP_LOGI(TAG, "disabled (no pin)");
        return ESP_OK;
    }
    if (s_gpio == cfg->avoid_gpio_a || s_gpio == cfg->avoid_gpio_b) {
        ESP_LOGW(TAG, "GPIO %d in use for LED data/clock — status LED disabled", s_gpio);
        s_gpio = -1;
        return ESP_OK;
    }

    ledc_timer_config_t timer = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = DUTY_RES,
        .timer_num = LEDC_TIMER,
        .freq_hz = PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc timer: %s", esp_err_to_name(err));
        s_gpio = -1;
        return err;
    }

    ledc_channel_config_t channel = {
        .gpio_num = s_gpio,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    err = ledc_channel_config(&channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc channel: %s", esp_err_to_name(err));
        s_gpio = -1;
        return err;
    }

    s_enabled = true;
    s_ready = true;
    write_duty(DUTY_MAX);
    ESP_LOGI(TAG, "PWM status LED on GPIO %d (%s)", s_gpio,
             s_active_high ? "active-high" : "active-low");
    return ESP_OK;
}

void pm_status_led_deinit(void)
{
    if (s_enabled && s_gpio >= 0) {
        write_duty(0);
        ledc_stop(LEDC_MODE, LEDC_CHANNEL, s_active_high ? 0 : 1);
    }
    s_enabled = false;
    s_ready = false;
    s_gpio = -1;
}

void pm_status_led_set_mode(pm_status_mode_t mode)
{
    if (s_mode == mode) return;
    s_mode = mode;
    s_mode_start_us = esp_timer_get_time();
    s_sos_step = 0;
    s_sos_step_start_us = s_mode_start_us;
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
    uint32_t elapsed_ms = (uint32_t)((now - s_mode_start_us) / 1000ULL);
    uint32_t t_ms = (uint32_t)(now / 1000ULL);
    float b = 0.0f;

    switch (s_mode) {
    case PM_STATUS_BOOT:
        /* Soft ramp-in, then hold */
        if (elapsed_ms < 700) {
            float x = (float)elapsed_ms / 700.0f;
            b = x * x * (3.0f - 2.0f * x);
        } else {
            b = 0.85f;
        }
        write_duty(duty_from(b, 0.0f, 1.0f));
        break;

    case PM_STATUS_WIFI_CONNECTING:
        /* Fast anxious breath — easy to spot while joining Wi‑Fi */
        b = wave_cos(t_ms, 380);
        write_duty(duty_from(b, 0.05f, 1.0f));
        break;

    case PM_STATUS_WIFI_AP:
        /* Slow calm breath — setup / hotspot mode */
        b = wave_cos(t_ms, 2200);
        write_duty(duty_from(b, 0.08f, 0.75f));
        break;

    case PM_STATUS_OK: {
        /* Soft double-heartbeat every ~2.8 s */
        uint32_t cycle = t_ms % 2800;
        float p1 = pulse_shaped(cycle, 90, 40, 160);
        float p2 = 0.0f;
        if (cycle >= 280) {
            p2 = 0.7f * pulse_shaped(cycle - 280, 90, 30, 180);
        }
        b = p1 > p2 ? p1 : p2;
        write_duty(duty_from(b, 0.0f, 0.55f));
        break;
    }

    case PM_STATUS_DMX_ACTIVE:
        /* Medium sawtooth ramp — “data flowing” feel */
        b = wave_saw(t_ms, 650, 0.28f);
        write_duty(duty_from(b, 0.12f, 1.0f));
        break;

    case PM_STATUS_FAULT_STRIP:
        /* Sharp urgent peaks with short ramps (not hard digital blink) */
        b = wave_saw(t_ms, 180, 0.35f);
        b = b * b; /* emphasize peaks */
        write_duty(duty_from(b, 0.0f, 1.0f));
        break;

    case PM_STATUS_FAULT_GENERAL: {
        /* Soft-edged SOS: short breaths for dots, longer for dashes */
        static const uint16_t on_ms[] = {
            140, 140, 140, 420, 420, 420, 140, 140, 140
        };
        static const uint16_t gap_ms[] = {
            120, 120, 280, 120, 120, 280, 120, 120, 900
        };
        const int n = (int)(sizeof(on_ms) / sizeof(on_ms[0]));
        if (s_sos_step >= n) s_sos_step = 0;

        uint32_t step_elapsed = (uint32_t)((now - s_sos_step_start_us) / 1000ULL);
        uint32_t on = on_ms[s_sos_step];
        uint32_t gap = gap_ms[s_sos_step];
        uint32_t step_total = on + gap;

        if (step_elapsed < on) {
            uint32_t attack = on / 4;
            uint32_t release = on / 3;
            uint32_t hold = on > attack + release ? on - attack - release : 0;
            b = pulse_shaped(step_elapsed, attack, hold, release);
        } else {
            b = 0.0f;
        }
        write_duty(duty_from(b, 0.0f, 1.0f));

        if (step_elapsed >= step_total) {
            s_sos_step_start_us = now;
            s_sos_step++;
        }
        break;
    }

    default:
        write_duty(0);
        break;
    }
}
