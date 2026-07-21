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
    p->blade_count = 2;
    p->rpm = 600.0f;
    p->linear_speed_mps = 4.0f;
    p->radius_m = 0.25f;
    p->path_length_m = 1.0f;
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

uint8_t pm_pov_clamp_blades(uint8_t blades)
{
    if (blades < PM_POV_BLADE_MIN) return PM_POV_BLADE_MIN;
    if (blades > PM_POV_BLADE_MAX) return PM_POV_BLADE_MAX;
    return blades;
}

static float pixel_span(uint16_t index, uint16_t count, pm_pov_layout_t layout)
{
    if (count <= 1) return 0.0f;
    float t = (float)index / (float)(count - 1);
    if (layout == PM_POV_LAYOUT_RADIUS) {
        return t;
    }
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

    uint8_t blades = 1;
    uint16_t ppb = pixel_count;
    uint16_t blade = 0;
    uint16_t local = pixel_index;

    if (pov->mode == PM_POV_ROTATION) {
        blades = pm_pov_clamp_blades(pov->blade_count);
        ppb = pixel_count / blades;
        if (ppb < 1) {
            ppb = pixel_count;
            blades = 1;
        }
        blade = (uint16_t)(pixel_index / ppb);
        if (blade >= blades) blade = (uint16_t)(blades - 1);
        local = (uint16_t)(pixel_index - blade * ppb);
        if (local >= ppb) local = (uint16_t)(ppb - 1);
    }

    float span = pixel_span(local, ppb, pov->layout);
    float radius = pov->radius_m > 1e-4f ? pov->radius_m : 0.25f;
    float t_sec = time_ms * 0.001f;

    if (pov->mode == PM_POV_ROTATION) {
        float rpm = pov->rpm > 0.0f ? pov->rpm : 0.0f;
        float base = (float)(2.0 * M_PI) * (rpm / 60.0f) * t_sec;
        float step = (float)(2.0 * M_PI) / (float)blades;
        float angle = base + (float)blade * step;
        float r = span * radius;
        out.x = r * cosf(angle);
        out.y = r * sinf(angle);
        out.z = 0.0f;
        return out;
    }

    if (pov->mode == PM_POV_LINEAR) {
        float path = pov->path_length_m > 1e-4f ? pov->path_length_m : 1.0f;
        float speed = pov->linear_speed_mps > 0.0f ? pov->linear_speed_mps : 0.0f;
        float dist = speed * t_sec;
        float phase = wrap01(dist / path);
        float u = phase < 0.5f ? (phase * 2.0f) : (2.0f - phase * 2.0f);
        out.x = (u - 0.5f) * path;
        out.y = span * radius;
        out.z = 0.0f;
        return out;
    }

    out.x = span * radius;
    out.y = 0.0f;
    out.z = 0.0f;
    return out;
}

esp_err_t pm_pov_build_strip_map(pm_pixel_map_t *map, uint16_t count,
                                 pm_pov_layout_t layout, uint8_t blade_count)
{
    if (!map || count == 0) return ESP_ERR_INVALID_ARG;
    uint8_t blades = pm_pov_clamp_blades(blade_count);
    uint16_t ppb = count / blades;
    if (ppb < 1) {
        blades = 1;
        ppb = count;
    }

    uint16_t written = 0;
    for (uint8_t b = 0; b < blades; ++b) {
        for (uint16_t li = 0; li < ppb; ++li) {
            uint16_t i = (uint16_t)(b * ppb + li);
            if (i >= count) break;
            float span = pixel_span(li, ppb, layout);
            /* Rest angle for blade b (static map preview coordinates) */
            float ang = (float)(2.0 * M_PI) * ((float)b / (float)blades);
            pm_mapped_pixel_t px = {
                .index = i,
                .pos = {span * cosf(ang), span * sinf(ang), 0.0f},
                .group = b,
            };
            esp_err_t err = pm_pixel_map_set(map, written, &px);
            if (err != ESP_OK) return err;
            written++;
        }
    }
    return ESP_OK;
}
