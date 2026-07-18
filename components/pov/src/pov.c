#include "pov.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void pm_pov_params_set_defaults(pm_pov_params_t *p)
{
    if (!p) return;
    p->mode = PM_POV_OFF;
    p->layout = PM_POV_LAYOUT_DIAMETER;
    p->rpm = 600.0f;             /* typical small POV fan / wand spin */
    p->linear_speed_mps = 4.0f;  /* ~ brisk wand wave */
    p->radius_m = 0.25f;         /* 25 cm tip radius / half-span */
    p->path_length_m = 1.0f;     /* 1 m sweep path */
}

const char *pm_pov_mode_name(pm_pov_mode_t mode)
{
    switch (mode) {
    case PM_POV_OFF: return "Off";
    case PM_POV_ROTATION: return "Rotation (fan)";
    case PM_POV_LINEAR: return "Linear (wand)";
    default: return "Unknown";
    }
}

static float pixel_span(uint16_t index, uint16_t count, pm_pov_layout_t layout)
{
    if (count <= 1) return 0.0f;
    float t = (float)index / (float)(count - 1); /* 0..1 along strip */
    if (layout == PM_POV_LAYOUT_RADIUS) {
        return t; /* 0 at hub, 1 at tip */
    }
    /* diameter: -1 .. +1 across hub */
    return (t * 2.0f) - 1.0f;
}

static float wrap01(float x)
{
    x = fmodf(x, 1.0f);
    if (x < 0.0f) x += 1.0f;
    return x;
}

pm_vec3_t pm_pov_world_pos(const pm_pov_params_t *pov, uint16_t pixel_index,
                           uint16_t pixel_count, uint32_t time_ms)
{
    pm_vec3_t out = {0, 0, 0};
    if (!pov || pixel_count == 0) return out;

    float span = pixel_span(pixel_index, pixel_count, pov->layout);
    float radius = pov->radius_m > 1e-4f ? pov->radius_m : 0.25f;
    float t_sec = time_ms * 0.001f;

    if (pov->mode == PM_POV_ROTATION) {
        float rpm = pov->rpm;
        if (rpm < 0.0f) rpm = 0.0f;
        /* θ = 2π · (RPM/60) · t */
        float angle = (float)(2.0 * M_PI) * (rpm / 60.0f) * t_sec;
        float r = span * radius; /* radius layout: 0..R; diameter: -R..R */
        out.x = r * cosf(angle);
        out.y = r * sinf(angle);
        out.z = 0.0f;
        return out;
    }

    if (pov->mode == PM_POV_LINEAR) {
        /*
         * Wand model: strip is vertical (along Y by span), swept along X.
         * Triangular back-and-forth so a waved wand paints a plane.
         */
        float path = pov->path_length_m > 1e-4f ? pov->path_length_m : 1.0f;
        float speed = pov->linear_speed_mps;
        if (speed < 0.0f) speed = 0.0f;
        float dist = speed * t_sec;
        float phase = wrap01(dist / path); /* 0..1 along path */
        /* triangle: 0→1→0 */
        float u = phase < 0.5f ? (phase * 2.0f) : (2.0f - phase * 2.0f);
        out.x = (u - 0.5f) * path;
        out.y = span * radius;
        out.z = 0.0f;
        return out;
    }

    /* Off: fall back to static strip layout in the plane (no motion) */
    out.x = span * radius;
    out.y = 0.0f;
    out.z = 0.0f;
    return out;
}

esp_err_t pm_pov_build_strip_map(pm_pixel_map_t *map, uint16_t count,
                                 pm_pov_layout_t layout)
{
    if (!map || count == 0) return ESP_ERR_INVALID_ARG;
    for (uint16_t i = 0; i < count; ++i) {
        float span = pixel_span(i, count, layout);
        pm_mapped_pixel_t px = {
            .index = i,
            .pos = {span, 0.0f, 0.0f},
            .group = 0,
        };
        esp_err_t err = pm_pixel_map_set(map, i, &px);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}
