#pragma once

#include "color_types.h"

#ifdef __cplusplus
extern "C" {
#endif

pm_rgb_t pm_hsv_to_rgb(pm_hsv_t hsv);
pm_hsv_t pm_rgb_to_hsv(pm_rgb_t rgb);

pm_rgb_t pm_rgb_blend(pm_rgb_t a, pm_rgb_t b, uint8_t amount);
pm_rgb_t pm_rgb_scale(pm_rgb_t c, uint8_t scale);
pm_rgb_t pm_rgb_add(pm_rgb_t a, pm_rgb_t b);

/* FastLED-style correction: scale, temperature, gamma, brightness */
pm_rgb_t pm_apply_correction(pm_rgb_t in, const pm_color_correction_t *cc);

/* RGB -> RGBW with optional auto white extraction (min channel) */
pm_rgbw_t pm_rgb_to_rgbw(pm_rgb_t in, bool auto_white);

/* RGB -> RGBWW; warm_mix 0=cool W1, 255=warm W2 */
pm_rgbww_t pm_rgb_to_rgbww(pm_rgb_t in, bool auto_white, uint8_t warm_mix);

/* Pack channels into output byte order for a chipset */
void pm_pack_pixel(const uint8_t *channels, int channel_count,
                   pm_color_order_t order, uint8_t *out, int *out_len);

const char *pm_color_order_name(pm_color_order_t order);
pm_color_order_t pm_color_order_from_name(const char *name);

uint8_t pm_gamma8(uint8_t v, uint8_t gamma_x100);

#ifdef __cplusplus
}
#endif
