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

    /* Geometric / lattice — work on 2D planes and 3D volumes */
    PM_EFFECT_CHECKERBOARD,
    PM_EFFECT_STRIPES,
    PM_EFFECT_CARTESIAN_GRID,
    PM_EFFECT_BOX_RINGS,       /* Manhattan / cubic shells */
    PM_EFFECT_SPHERE_RINGS,    /* Euclidean circles / spheres */
    PM_EFFECT_CROSSHAIR,
    PM_EFFECT_DIAMOND_LATTICE,
    PM_EFFECT_SPIRAL,
    PM_EFFECT_STARBURST,
    PM_EFFECT_CHEVRON,
    PM_EFFECT_HELIX,
    PM_EFFECT_AXIS_PLANES,
    PM_EFFECT_SCAN_VOLUME,
    PM_EFFECT_INTERFERENCE,
    PM_EFFECT_TRI_LATTICE,
    PM_EFFECT_CUBIC_PULSE,
    PM_EFFECT_POLAR_GRID,
    PM_EFFECT_WAVE_WALLS,

    PM_EFFECT_CUSTOM_LUA, /* user script via effect_lua runtime */

    /* Audio-reactive (need live mic levels in context) */
    PM_EFFECT_AUDIO_PULSE,
    PM_EFFECT_AUDIO_RIPPLE,
    PM_EFFECT_AUDIO_SPECTRUM,

    PM_EFFECT_COUNT
} pm_effect_id_t;

#define PM_AUDIO_BINS 16

/** Live analysis snapshot from `audio` component (optional on context). */
typedef struct {
    float volume;
    float bass, mid, treble;
    float bins[PM_AUDIO_BINS];
    bool beat;
    bool active;
} pm_audio_levels_t;

#define PM_FX_P_COUNT 8

/** Indexed effect controls (UI sliders + optional DMX channel bindings). */
typedef enum {
    PM_FXPARAM_SPEED = 0,
    PM_FXPARAM_SCALE,
    PM_FXPARAM_INTENSITY,
    PM_FXPARAM_PRIMARY_H,
    PM_FXPARAM_PRIMARY_S,
    PM_FXPARAM_PRIMARY_V,
    PM_FXPARAM_SECONDARY_H,
    PM_FXPARAM_SECONDARY_S,
    PM_FXPARAM_SECONDARY_V,
    PM_FXPARAM_P1,
    PM_FXPARAM_P2,
    PM_FXPARAM_P3,
    PM_FXPARAM_P4,
    PM_FXPARAM_P5,
    PM_FXPARAM_P6,
    PM_FXPARAM_P7,
    PM_FXPARAM_P8,
    PM_FXPARAM_POS_X,
    PM_FXPARAM_POS_Y,
    PM_FXPARAM_POS_Z,
    PM_FXPARAM_ROT_X,
    PM_FXPARAM_ROT_Y,
    PM_FXPARAM_ROT_Z,
    PM_FXPARAM_COUNT
} pm_fxparam_id_t;

typedef struct {
    pm_effect_id_t id;
    float speed;          /* 0..10 effect animation rate (independent of POV RPM) */
    float scale;          /* spatial frequency */
    float intensity;      /* 0..1 */
    pm_hsv_t primary;
    pm_hsv_t secondary;
    uint8_t palette_blend; /* 0..255 */
    float p[PM_FX_P_COUNT]; /* generic 0..1 knobs (Lua + effect-specific) */
    float pos[3];         /* XYZ offset in map units (−0.5..+0.5) */
    float rot[3];         /* XYZ rotation in degrees (0..360) */
} pm_effect_params_t;

/** Bitmask of which PM_FXPARAM_* slots are relevant for an effect. */
uint32_t pm_effect_param_mask(pm_effect_id_t id);

typedef struct {
    const pm_pixel_map_t *map;
    pm_effect_params_t params;
    uint32_t time_ms;
    bool pov_enabled;
    pm_pov_params_t pov;
    uint16_t strip_len;   /* physical strip length for POV */
    /** Optional mic levels; NULL or inactive → audio effects stay dark/quiet. */
    const pm_audio_levels_t *audio;
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
