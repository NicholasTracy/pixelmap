#include "effects.h"
#include "color_engine.h"
#include <math.h>

static float hash2(float x, float y)
{
    float n = sinf(x * 127.1f + y * 311.7f) * 43758.5453f;
    return n - floorf(n);
}

static float noise2(float x, float y)
{
    float xi = floorf(x), yi = floorf(y);
    float xf = x - xi, yf = y - yi;
    float u = xf * xf * (3.0f - 2.0f * xf);
    float v = yf * yf * (3.0f - 2.0f * yf);
    float a = hash2(xi, yi);
    float b = hash2(xi + 1, yi);
    float c = hash2(xi, yi + 1);
    float d = hash2(xi + 1, yi + 1);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}

const char *pm_effect_name(pm_effect_id_t id)
{
    switch (id) {
    case PM_EFFECT_SOLID: return "Solid";
    case PM_EFFECT_RAINBOW_SPATIAL: return "Rainbow Spatial";
    case PM_EFFECT_PLASMA: return "Plasma";
    case PM_EFFECT_NOISE_FIELD: return "Noise Field";
    case PM_EFFECT_RADIAL_WAVE: return "Radial Wave";
    case PM_EFFECT_PLANE_SWEEP: return "Plane Sweep";
    default: return "Unknown";
    }
}

pm_rgb_t pm_effect_eval(const pm_effect_context_t *ctx, uint16_t map_index)
{
    const pm_mapped_pixel_t *px = pm_pixel_map_get(ctx->map, map_index);
    if (!px) {
        return (pm_rgb_t){0, 0, 0};
    }

    float t = ctx->time_ms * 0.001f * ctx->params.speed;
    float x = px->pos.x * ctx->params.scale;
    float y = px->pos.y * ctx->params.scale;
    float z = px->pos.z * ctx->params.scale;
    float inten = ctx->params.intensity;
    if (inten < 0) inten = 0;
    if (inten > 1) inten = 1;

    pm_hsv_t hsv = ctx->params.primary;

    switch (ctx->params.id) {
    case PM_EFFECT_SOLID:
        break;

    case PM_EFFECT_RAINBOW_SPATIAL: {
        float h = x * 40.0f + y * 25.0f + z * 15.0f + t * 40.0f;
        hsv.h = (uint8_t)((int)h & 255);
        hsv.s = 255;
        hsv.v = (uint8_t)(255 * inten);
        break;
    }
    case PM_EFFECT_PLASMA: {
        float v = sinf(x * 3.0f + t);
        v += sinf(y * 4.0f + t * 1.3f);
        v += sinf((x + y) * 2.0f + t * 0.7f);
        v += sinf(sqrtf(x * x + y * y) * 3.0f - t);
        v = v * 0.25f + 0.5f;
        hsv.h = (uint8_t)(v * 255.0f);
        hsv.s = 220;
        hsv.v = (uint8_t)(255 * inten);
        break;
    }
    case PM_EFFECT_NOISE_FIELD: {
        float n = noise2(x + t * 0.2f, y - t * 0.15f);
        float n2 = noise2(x * 2.0f - t, z * 2.0f + t * 0.3f);
        hsv.h = (uint8_t)((n * 200.0f + ctx->params.primary.h) );
        hsv.s = (uint8_t)(180 + n2 * 75.0f);
        hsv.v = (uint8_t)(n * 255.0f * inten);
        break;
    }
    case PM_EFFECT_RADIAL_WAVE: {
        float cx = x - 0.5f * ctx->params.scale;
        float cy = y - 0.5f * ctx->params.scale;
        float d = sqrtf(cx * cx + cy * cy + z * z);
        float w = 0.5f + 0.5f * sinf(d * 8.0f - t * 4.0f);
        hsv = ctx->params.primary;
        pm_hsv_t b = ctx->params.secondary;
        hsv.h = (uint8_t)((1.0f - w) * hsv.h + w * b.h);
        hsv.s = (uint8_t)((1.0f - w) * hsv.s + w * b.s);
        hsv.v = (uint8_t)(w * 255.0f * inten);
        break;
    }
    case PM_EFFECT_PLANE_SWEEP: {
        float plane = x * 0.6f + y * 0.3f + z * 0.1f - t * 0.5f;
        float f = plane - floorf(plane);
        float band = f < 0.15f ? (1.0f - f / 0.15f) : 0.0f;
        hsv = ctx->params.primary;
        hsv.v = (uint8_t)(band * 255.0f * inten);
        break;
    }
    default:
        break;
    }

    pm_rgb_t rgb = pm_hsv_to_rgb(hsv);
    if (ctx->params.palette_blend) {
        pm_rgb_t sec = pm_hsv_to_rgb(ctx->params.secondary);
        rgb = pm_rgb_blend(rgb, sec, ctx->params.palette_blend);
    }
    return rgb;
}

void pm_effect_render(const pm_effect_context_t *ctx, pm_effect_set_pixel_fn set_px, void *user)
{
    if (!ctx || !ctx->map || !set_px) return;
    uint16_t n = pm_pixel_map_count(ctx->map);
    for (uint16_t i = 0; i < n; ++i) {
        const pm_mapped_pixel_t *px = pm_pixel_map_get(ctx->map, i);
        if (!px) continue;
        set_px(user, px->index, pm_effect_eval(ctx, i));
    }
}
