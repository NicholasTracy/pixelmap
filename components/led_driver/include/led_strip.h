#pragma once

#include "led_chipsets.h"
#include "color_engine.h"
#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pm_led_strip pm_led_strip_t;

typedef struct {
    int gpio_data;
    int gpio_clock;           /* -1 if unused */
    pm_chipset_t chipset;
    pm_color_order_t color_order;
    uint16_t pixel_count;
    uint8_t rmt_resolution_hz_mhz; /* typically 10 (= 10 MHz) */
} pm_led_strip_config_t;

esp_err_t pm_led_strip_create(const pm_led_strip_config_t *cfg, pm_led_strip_t **out);
void pm_led_strip_destroy(pm_led_strip_t *strip);

uint16_t pm_led_strip_length(const pm_led_strip_t *strip);
pm_color_mode_t pm_led_strip_color_mode(const pm_led_strip_t *strip);

void pm_led_strip_clear(pm_led_strip_t *strip);
void pm_led_strip_set_rgb(pm_led_strip_t *strip, uint16_t index, pm_rgb_t c);
void pm_led_strip_set_rgbw(pm_led_strip_t *strip, uint16_t index, pm_rgbw_t c);
void pm_led_strip_fill_rgb(pm_led_strip_t *strip, pm_rgb_t c);

void pm_led_strip_set_correction(pm_led_strip_t *strip, const pm_color_correction_t *cc);
const pm_color_correction_t *pm_led_strip_get_correction(const pm_led_strip_t *strip);

/**
 * Pure encode path shared with host virtbench / tests.
 * Applies correction, derives whites, packs color order into out[].
 * Returns encoded byte count, or 0 on error / insufficient out_cap.
 */
size_t pm_led_encode_frame(const uint8_t *pixels, uint16_t count, uint8_t channels,
                           pm_color_order_t order, const pm_color_correction_t *cc,
                           uint8_t *out, size_t out_cap);

/* Encode + transmit via RMT. Blocks until DMA/RMT queue accepts. */
esp_err_t pm_led_strip_show(pm_led_strip_t *strip);

#ifdef __cplusplus
}
#endif
