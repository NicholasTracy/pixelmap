#pragma once

#include "color_types.h"
#include "pixel_map.h"
#include "pov.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PM_EFFECT_SOLID = 0,
    PM_EFFECT_RAINBOW_SPATIAL,
    PM_EFFECT_PLASMA,
    PM_EFFECT_NOISE_FIELD,
    PM_EFFECT_RADIAL_WAVE,
    PM_EFFECT_PLANE_SWEEP,
    PM_EFFECT_POV_IMAGE_PLANE, /* spatial pattern stable in the swept POV plane */
    PM_EFFECT_COUNT
} pm_effect_id_t;

typedef struct {
    pm_effect_id_t id;
    float speed;          /* 0..10 effect animation rate (independent of POV RPM) */
    float scale;          /* spatial frequency */
    float intensity;      /* 0..1 */
    pm_hsv_t primary;
    pm_hsv_t secondary;
    uint8_t palette_blend; /* 0..255 */
} pm_effect_params_t;

typedef struct {
    const pm_pixel_map_t *map;
    pm_effect_params_t params;
    uint32_t time_ms;
    bool pov_enabled;
    pm_pov_params_t pov;
    uint16_t strip_len;   /* physical strip length for POV */
} pm_effect_context_t;

const char *pm_effect_name(pm_effect_id_t id);

/* Evaluate one mapped pixel into RGB (pre-correction) */
pm_rgb_t pm_effect_eval(const pm_effect_context_t *ctx, uint16_t map_index);

/* Render entire map into a strip via callback */
typedef void (*pm_effect_set_pixel_fn)(void *user, uint16_t strip_index, pm_rgb_t color);
void pm_effect_render(const pm_effect_context_t *ctx, pm_effect_set_pixel_fn set_px, void *user);

#ifdef __cplusplus
}
#endif
