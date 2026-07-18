#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t r, g, b;
} pm_rgb_t;

typedef struct {
    uint8_t r, g, b, w;
} pm_rgbw_t;

typedef struct {
    uint8_t r, g, b, w1, w2;
} pm_rgbww_t;

typedef struct {
    uint8_t h;   /* 0-255 hue wheel (FastLED-compatible scale) */
    uint8_t s;
    uint8_t v;
} pm_hsv_t;

typedef enum {
    PM_COLOR_MODE_RGB = 0,
    PM_COLOR_MODE_RGBW,
    PM_COLOR_MODE_RGBWW,
    PM_COLOR_MODE_GRBW,
} pm_color_mode_t;

typedef enum {
    PM_COLOR_ORDER_RGB = 0,
    PM_COLOR_ORDER_RBG,
    PM_COLOR_ORDER_GRB,
    PM_COLOR_ORDER_GBR,
    PM_COLOR_ORDER_BRG,
    PM_COLOR_ORDER_BGR,
    PM_COLOR_ORDER_RGBW,
    PM_COLOR_ORDER_GRBW,
    PM_COLOR_ORDER_WRGB,
} pm_color_order_t;

typedef struct {
    pm_rgb_t correction;      /* per-channel scale, 255 = unity */
    pm_rgb_t temperature;     /* white-balance / color temp tint */
    uint8_t gamma;            /* 100 = linear, 220 ≈ γ2.2 encoded as x100 */
    uint8_t brightness;       /* global 0-255 */
    bool auto_white;          /* extract W from RGB for RGBW/RGBWW */
    uint8_t warm_mix;         /* RGBWW: blend toward warm white 0-255 */
} pm_color_correction_t;

#ifdef __cplusplus
}
#endif
