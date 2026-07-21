#pragma once

#include "effects.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* pm_audio_levels_t / PM_AUDIO_BINS defined in effects.h for shared use. */

typedef struct {
    bool enable;
    int gpio_ws;   /* I2S WS / LRCLK (INMP441) */
    int gpio_sck;  /* I2S BCLK */
    int gpio_sd;   /* I2S DIN from mic DOUT */
    uint8_t gain;    /* 1..255 linear gain after AGC seed */
    uint8_t squelch; /* 0..255 noise gate */
} pm_audio_config_t;

esp_err_t pm_audio_start(const pm_audio_config_t *cfg);
void pm_audio_stop(void);
bool pm_audio_running(void);

/** Copy latest levels (thread-safe). Zeros / inactive if not running. */
void pm_audio_get_levels(pm_audio_levels_t *out);

#ifdef __cplusplus
}
#endif
