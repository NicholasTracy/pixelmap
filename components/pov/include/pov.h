#pragma once

#include "pixel_map.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PM_POV_OFF = 0,
    PM_POV_ROTATION,   /* fan / propeller — strip spins in a plane */
    PM_POV_LINEAR,     /* wand / blade — strip sweeps along a path */
} pm_pov_mode_t;

typedef enum {
    PM_POV_LAYOUT_RADIUS = 0,   /* hub → tip (0 at center) */
    PM_POV_LAYOUT_DIAMETER,     /* tip → tip across hub */
} pm_pov_layout_t;

typedef struct {
    pm_pov_mode_t mode;
    pm_pov_layout_t layout;
    float rpm;                 /* rotation speed (rev/min); primary fixed param */
    float linear_speed_mps;    /* linear sweep speed for PM_POV_LINEAR */
    float radius_m;            /* physical radius / half-span of the strip (meters) */
    float path_length_m;       /* linear path length for wand sweep (meters) */
} pm_pov_params_t;

void pm_pov_params_set_defaults(pm_pov_params_t *p);

const char *pm_pov_mode_name(pm_pov_mode_t mode);

/* Instantaneous world position of a strip pixel given motion + time. */
pm_vec3_t pm_pov_world_pos(const pm_pov_params_t *pov, uint16_t pixel_index,
                           uint16_t pixel_count, uint32_t time_ms);

/*
 * Build a 1D strip map suited to POV (radius or diameter layout).
 * Positions are stored in normalized strip coordinates; pm_pov_world_pos
 * applies motion using radius_m / RPM.
 */
esp_err_t pm_pov_build_strip_map(pm_pixel_map_t *map, uint16_t count,
                                 pm_pov_layout_t layout);

#ifdef __cplusplus
}
#endif
