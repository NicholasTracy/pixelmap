#pragma once

#include "color_types.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifndef PM_FX_P_COUNT
#define PM_FX_P_COUNT 8
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define PM_LUA_SCRIPT_MAX 4096

esp_err_t pm_effect_lua_init(void);

/** Replace script, compile, and persist to NVS. */
esp_err_t pm_effect_lua_set_script(const char *src);

/** Current script text (never NULL; may be empty / default). */
const char *pm_effect_lua_get_script(void);

bool pm_effect_lua_ready(void);
const char *pm_effect_lua_last_error(void);

typedef struct {
    float speed;
    float scale;
    float intensity; /* 0..1 */
    uint8_t ph, ps, pv;
    uint8_t sh, ss, sv;
    float p[PM_FX_P_COUNT]; /* 0..1 */
} pm_lua_effect_inputs_t;

/**
 * Evaluate one pixel. Globals:
 * t, speed, scale, intensity, i, count, x, y, z,
 * ph/ps/pv, sh/ss/sv, p1..p8 (and table p[1..8])
 * Helpers: hsv, rgb, lerp, clamp, noise, math
 */
pm_rgb_t pm_effect_lua_eval(const pm_lua_effect_inputs_t *in,
                            uint16_t i, uint16_t count,
                            float x, float y, float z, float t);

#ifdef __cplusplus
}
#endif
