#include "effects.h"
#include "color_engine.h"
#include "effect_lua.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

static float fractf(float v)
{
    return v - floorf(v);
}

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float smoothpulse(float x, float width)
{
    /* Soft band around integer lattice; x is fractional distance 0..0.5 */
    if (width < 1e-4f) return 0.0f;
    float d = fabsf(x);
    if (d >= width) return 0.0f;
    float u = 1.0f - d / width;
    return u * u * (3.0f - 2.0f * u);
}

static uint8_t lerp_u8(uint8_t a, uint8_t b, float t)
{
    t = clampf(t, 0.0f, 1.0f);
    return (uint8_t)((1.0f - t) * (float)a + t * (float)b);
}

static pm_hsv_t lerp_hsv(pm_hsv_t a, pm_hsv_t b, float t)
{
    return (pm_hsv_t){
        lerp_u8(a.h, b.h, t),
        lerp_u8(a.s, b.s, t),
        lerp_u8(a.v, b.v, t),
    };
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
    case PM_EFFECT_POV_IMAGE_PLANE: return "POV Image Plane";
    case PM_EFFECT_CHECKERBOARD: return "Checkerboard";
    case PM_EFFECT_STRIPES: return "Stripes";
    case PM_EFFECT_CARTESIAN_GRID: return "Cartesian Grid";
    case PM_EFFECT_BOX_RINGS: return "Box Rings";
    case PM_EFFECT_SPHERE_RINGS: return "Sphere Rings";
    case PM_EFFECT_CROSSHAIR: return "Crosshair";
    case PM_EFFECT_DIAMOND_LATTICE: return "Diamond Lattice";
    case PM_EFFECT_SPIRAL: return "Spiral";
    case PM_EFFECT_STARBURST: return "Starburst";
    case PM_EFFECT_CHEVRON: return "Chevron";
    case PM_EFFECT_HELIX: return "Helix";
    case PM_EFFECT_AXIS_PLANES: return "Axis Planes";
    case PM_EFFECT_SCAN_VOLUME: return "Scan Volume";
    case PM_EFFECT_INTERFERENCE: return "Interference";
    case PM_EFFECT_TRI_LATTICE: return "Triangle Lattice";
    case PM_EFFECT_CUBIC_PULSE: return "Cubic Pulse";
    case PM_EFFECT_POLAR_GRID: return "Polar Grid";
    case PM_EFFECT_WAVE_WALLS: return "Wave Walls";
    case PM_EFFECT_CUSTOM_LUA: return "Custom Lua";
    default: return "Unknown";
    }
}

#define FXM_SPD   (1u << PM_FXPARAM_SPEED)
#define FXM_SCL   (1u << PM_FXPARAM_SCALE)
#define FXM_INT   (1u << PM_FXPARAM_INTENSITY)
#define FXM_PRI   ((1u << PM_FXPARAM_PRIMARY_H) | (1u << PM_FXPARAM_PRIMARY_S) | (1u << PM_FXPARAM_PRIMARY_V))
#define FXM_SEC   ((1u << PM_FXPARAM_SECONDARY_H) | (1u << PM_FXPARAM_SECONDARY_S) | (1u << PM_FXPARAM_SECONDARY_V))
#define FXM_PALL  (0xFFu << PM_FXPARAM_P1)
#define FXM_P1    (1u << PM_FXPARAM_P1)
#define FXM_P2    (1u << PM_FXPARAM_P2)
#define FXM_GEO   ((1u << PM_FXPARAM_POS_X) | (1u << PM_FXPARAM_POS_Y) | (1u << PM_FXPARAM_POS_Z) | \
                   (1u << PM_FXPARAM_ROT_X) | (1u << PM_FXPARAM_ROT_Y) | (1u << PM_FXPARAM_ROT_Z))

uint32_t pm_effect_param_mask(pm_effect_id_t id)
{
    switch (id) {
    case PM_EFFECT_SOLID:
        return FXM_INT | FXM_PRI;
    case PM_EFFECT_RAINBOW_SPATIAL:
    case PM_EFFECT_POV_IMAGE_PLANE:
        return FXM_SPD | FXM_SCL | FXM_INT | FXM_GEO;
    case PM_EFFECT_PLASMA:
        return FXM_SPD | FXM_SCL | FXM_INT | FXM_PRI | FXM_SEC | FXM_P1 | FXM_GEO;
    case PM_EFFECT_NOISE_FIELD:
        return FXM_SPD | FXM_SCL | FXM_INT | FXM_PRI | FXM_P1 | FXM_GEO;
    case PM_EFFECT_RADIAL_WAVE:
    case PM_EFFECT_PLANE_SWEEP:
        return FXM_SPD | FXM_SCL | FXM_INT | FXM_PRI | FXM_SEC | FXM_P1 | FXM_GEO;
    /* Geometric: Scale owns spatial frequency — no Density/Thickness knobs */
    case PM_EFFECT_CHECKERBOARD:
    case PM_EFFECT_STRIPES:
    case PM_EFFECT_CARTESIAN_GRID:
    case PM_EFFECT_BOX_RINGS:
    case PM_EFFECT_SPHERE_RINGS:
    case PM_EFFECT_CROSSHAIR:
    case PM_EFFECT_DIAMOND_LATTICE:
    case PM_EFFECT_SPIRAL:
    case PM_EFFECT_STARBURST:
    case PM_EFFECT_CHEVRON:
    case PM_EFFECT_HELIX:
    case PM_EFFECT_AXIS_PLANES:
    case PM_EFFECT_SCAN_VOLUME:
    case PM_EFFECT_INTERFERENCE:
    case PM_EFFECT_TRI_LATTICE:
    case PM_EFFECT_CUBIC_PULSE:
    case PM_EFFECT_POLAR_GRID:
    case PM_EFFECT_WAVE_WALLS:
        return FXM_SPD | FXM_SCL | FXM_INT | FXM_PRI | FXM_SEC | FXM_GEO;
    case PM_EFFECT_CUSTOM_LUA:
        return (uint32_t)((1u << PM_FXPARAM_COUNT) - 1u);
    default:
        return FXM_SPD | FXM_SCL | FXM_INT | FXM_PRI | FXM_GEO;
    }
}

static pm_vec3_t sample_pos(const pm_effect_context_t *ctx, uint16_t map_index,
                            const pm_mapped_pixel_t *px)
{
    (void)map_index;
    if (ctx->pov_enabled && ctx->pov.mode != PM_POV_OFF && ctx->strip_len > 0) {
        pm_vec3_t w = pm_pov_world_pos(&ctx->pov, px->index, ctx->strip_len, ctx->time_ms);
        /* Normalize into roughly -1..1 / 0..1 space using radius */
        float inv = ctx->pov.radius_m > 1e-4f ? (1.0f / ctx->pov.radius_m) : 1.0f;
        if (ctx->pov.mode == PM_POV_LINEAR) {
            float path = ctx->pov.path_length_m > 1e-4f ? ctx->pov.path_length_m : 1.0f;
            w.x = w.x / path;          /* -0.5..0.5 */
            w.y = w.y * inv;           /* -1..1 or 0..1 by layout */
        } else {
            w.x *= inv;
            w.y *= inv;
        }
        return w;
    }
    return px->pos;
}

/** Rotate then translate effect space (degrees; pos is −0.5..+0.5). */
static pm_vec3_t apply_fx_geom(pm_vec3_t p, const pm_effect_params_t *params)
{
    float x = p.x - 0.5f;
    float y = p.y - 0.5f;
    float z = p.z - 0.5f;
    const float deg = (float)(M_PI / 180.0);
    float rx = params->rot[0] * deg;
    float ry = params->rot[1] * deg;
    float rz = params->rot[2] * deg;

    float cy = cosf(rx), sy = sinf(rx);
    float y1 = y * cy - z * sy;
    float z1 = y * sy + z * cy;
    y = y1; z = z1;

    float cx = cosf(ry), sx = sinf(ry);
    float x1 = x * cx + z * sx;
    z1 = -x * sx + z * cx;
    x = x1; z = z1;

    cx = cosf(rz); sx = sinf(rz);
    x1 = x * cx - y * sx;
    y1 = x * sx + y * cx;
    x = x1; y = y1;

    return (pm_vec3_t){
        x + 0.5f + params->pos[0],
        y + 0.5f + params->pos[1],
        z + 0.5f + params->pos[2],
    };
}

pm_rgb_t pm_effect_eval(const pm_effect_context_t *ctx, uint16_t map_index)
{
    const pm_mapped_pixel_t *px = pm_pixel_map_get(ctx->map, map_index);
    if (!px) {
        return (pm_rgb_t){0, 0, 0};
    }

    pm_vec3_t pos = apply_fx_geom(sample_pos(ctx, map_index, px), &ctx->params);
    float t = ctx->time_ms * 0.001f * ctx->params.speed;
    float scale = ctx->params.scale > 1e-4f ? ctx->params.scale : 1.0f;
    float x = pos.x * scale;
    float y = pos.y * scale;
    float z = pos.z * scale;
    /* Centered coords for geometric patterns (map space is typically 0..1) */
    float cx = (pos.x - 0.5f) * scale;
    float cy = (pos.y - 0.5f) * scale;
    float cz = (pos.z - 0.5f) * scale;
    float inten = ctx->params.intensity;
    if (inten < 0) inten = 0;
    if (inten > 1) inten = 1;
    float p1 = clampf(ctx->params.p[0], 0.0f, 1.0f);
    /* Fixed geometric pattern constants — spatial frequency comes from Scale */
    const float pat = 6.0f;
    const float line_w = 0.08f;

    pm_hsv_t hsv = ctx->params.primary;
    pm_hsv_t sec = ctx->params.secondary;

    switch (ctx->params.id) {
    case PM_EFFECT_SOLID:
        hsv.v = (uint8_t)(hsv.v * inten);
        break;

    case PM_EFFECT_RAINBOW_SPATIAL: {
        float h = x * 40.0f + y * 25.0f + z * 15.0f + t * 40.0f;
        hsv.h = (uint8_t)((int)h & 255);
        hsv.s = 255;
        hsv.v = (uint8_t)(255 * inten);
        break;
    }
    case PM_EFFECT_PLASMA: {
        /* p1 = complexity / spatial frequency; primary↔secondary by field */
        float freq = 1.5f + p1 * 4.0f;
        float field = sinf(x * freq + t);
        field += sinf(y * (freq + 1.0f) + t * 1.3f);
        field += sinf(z * (freq + 0.5f) + t * 0.9f);
        field += sinf((x + y) * (1.5f + p1 * 2.0f) + t * 0.7f);
        field += sinf(sqrtf(cx * cx + cy * cy + cz * cz) * (2.0f + p1 * 3.0f) - t);
        field = clampf(field * 0.2f + 0.5f, 0.0f, 1.0f);
        hsv = lerp_hsv(hsv, sec, field);
        hsv.v = (uint8_t)((hsv.v / 255.0f) * (0.2f + 0.8f * field) * 255.0f * inten);
        break;
    }
    case PM_EFFECT_NOISE_FIELD: {
        /* p1 = contrast */
        float n = noise2(x + t * 0.2f, y - t * 0.15f);
        float n2 = noise2(x * 2.0f - t, z * 2.0f + t * 0.3f);
        float contrast = 0.35f + p1 * 1.65f;
        n = clampf((n - 0.5f) * contrast + 0.5f, 0.0f, 1.0f);
        n2 = clampf((n2 - 0.5f) * contrast + 0.5f, 0.0f, 1.0f);
        hsv.h = (uint8_t)((int)hsv.h + (int)(n * 40.0f));
        hsv.s = lerp_u8(hsv.s, (uint8_t)(hsv.s * 0.55f), n2);
        hsv.v = (uint8_t)((hsv.v / 255.0f) * n * 255.0f * inten);
        break;
    }
    case PM_EFFECT_RADIAL_WAVE: {
        float d = sqrtf(cx * cx + cy * cy + cz * cz);
        float waves = 4.0f + p1 * 16.0f;
        float w = 0.5f + 0.5f * sinf(d * waves - t * 4.0f);
        hsv = lerp_hsv(hsv, sec, w);
        hsv.v = (uint8_t)((hsv.v / 255.0f) * w * 255.0f * inten);
        break;
    }
    case PM_EFFECT_PLANE_SWEEP: {
        float plane = x * 0.6f + y * 0.3f + z * 0.1f - t * 0.5f;
        float f = plane - floorf(plane);
        float bw = 0.06f + p1 * 0.35f;
        float band = f < bw ? (1.0f - f / bw) : 0.0f;
        hsv = lerp_hsv(hsv, sec, band);
        hsv.v = (uint8_t)((hsv.v / 255.0f) * band * 255.0f * inten);
        break;
    }
    case PM_EFFECT_POV_IMAGE_PLANE: {
        float r = sqrtf(x * x + y * y);
        float ang = atan2f(y, x);
        float spokes = 6.0f;
        float a = ang / (float)(2.0 * M_PI);
        if (a < 0.0f) a += 1.0f;
        float spoke = fabsf(sinf(a * (float)M_PI * spokes));
        float ring = 0.5f + 0.5f * sinf(r * 14.0f);
        float mix = spoke * 0.65f + ring * 0.35f;
        hsv.h = (uint8_t)((a * 255.0f) + r * 30.0f);
        hsv.s = 230;
        hsv.v = (uint8_t)(mix * 255.0f * inten);
        break;
    }

    case PM_EFFECT_CHECKERBOARD: {
        int ix = (int)floorf(x * pat + t * 0.35f);
        int iy = (int)floorf(y * pat - t * 0.2f);
        int iz = (int)floorf(z * pat + t * 0.15f);
        int parity = (ix + iy + iz) & 1;
        hsv = parity ? hsv : sec;
        hsv.v = (uint8_t)(hsv.v * inten);
        break;
    }
    case PM_EFFECT_STRIPES: {
        float phase = cx * pat + cy * 2.0f + cz * 3.0f - t * 2.0f;
        float f = fractf(phase);
        float band = f < 0.5f ? 1.0f : 0.0f;
        hsv = band > 0.5f ? hsv : sec;
        hsv.v = (uint8_t)(hsv.v * inten * (0.4f + 0.6f * band));
        break;
    }
    case PM_EFFECT_CARTESIAN_GRID: {
        float gx = fabsf(fractf(cx * pat + t * 0.1f) - 0.5f);
        float gy = fabsf(fractf(cy * pat - t * 0.08f) - 0.5f);
        float gz = fabsf(fractf(cz * pat + t * 0.06f) - 0.5f);
        float line = fmaxf(fmaxf(smoothpulse(gx, line_w), smoothpulse(gy, line_w)),
                           smoothpulse(gz, line_w));
        int cx_i = (int)floorf(cx * pat + t * 0.1f);
        int cy_i = (int)floorf(cy * pat - t * 0.08f);
        int cz_i = (int)floorf(cz * pat + t * 0.06f);
        float cell = ((cx_i + cy_i + cz_i) & 1) ? 0.35f : 0.12f;
        float mix = clampf(fmaxf(line, cell), 0.0f, 1.0f);
        hsv = lerp_hsv(hsv, sec, cell / 0.35f);
        hsv.v = (uint8_t)((hsv.v / 255.0f) * mix * 255.0f * inten);
        break;
    }
    case PM_EFFECT_BOX_RINGS: {
        float d = fmaxf(fabsf(cx), fmaxf(fabsf(cy), fabsf(cz)));
        float rings = 14.0f;
        float field = 0.5f + 0.5f * sinf(d * rings - t * 5.0f);
        float edge = fabsf(sinf(d * rings - t * 5.0f));
        edge = edge > 0.72f ? (edge - 0.72f) / 0.28f : 0.0f;
        hsv = lerp_hsv(hsv, sec, field);
        float bri = clampf(0.22f + field * 0.55f + edge * 0.45f, 0.0f, 1.0f);
        hsv.v = (uint8_t)((hsv.v / 255.0f) * bri * 255.0f * inten);
        break;
    }
    case PM_EFFECT_SPHERE_RINGS: {
        float d = sqrtf(cx * cx + cy * cy + cz * cz);
        float rings = 12.0f;
        float field = 0.5f + 0.5f * sinf(d * rings - t * 4.5f);
        float ring = fabsf(sinf(d * rings - t * 4.5f));
        ring = ring > 0.78f ? (ring - 0.78f) / 0.22f : 0.0f;
        hsv = lerp_hsv(hsv, sec, field);
        float bri = clampf(0.2f + field * 0.55f + ring * 0.45f, 0.0f, 1.0f);
        hsv.v = (uint8_t)((hsv.v / 255.0f) * bri * 255.0f * inten);
        break;
    }
    case PM_EFFECT_CROSSHAIR: {
        float thr = 0.14f + 0.03f * sinf(t * 3.0f);
        float ax = clampf(1.0f - fabsf(cx) / thr, 0.0f, 1.0f);
        float ay = clampf(1.0f - fabsf(cy) / thr, 0.0f, 1.0f);
        float az = clampf(1.0f - fabsf(cz) / thr, 0.0f, 1.0f);
        float m = fmaxf(ax, fmaxf(ay, az));
        m = m * m;
        hsv = m > 0.25f ? hsv : sec;
        hsv.v = (uint8_t)((hsv.v / 255.0f) * clampf(m + 0.1f, 0.0f, 1.0f) * 255.0f * inten);
        break;
    }
    case PM_EFFECT_DIAMOND_LATTICE: {
        float d = fabsf(cx) + fabsf(cy) + fabsf(cz);
        float cell = fractf(d * pat - t * 1.2f);
        float on = cell < 0.45f ? 1.0f : (cell < 0.65f ? (0.65f - cell) / 0.2f : 0.0f);
        float body = 0.15f + 0.2f * (0.5f + 0.5f * sinf(d * 8.0f - t));
        float mix = clampf(fmaxf(on, body), 0.0f, 1.0f);
        hsv = lerp_hsv(hsv, sec, body);
        hsv.v = (uint8_t)((hsv.v / 255.0f) * mix * 255.0f * inten);
        break;
    }
    case PM_EFFECT_SPIRAL: {
        float r = sqrtf(cx * cx + cy * cy + cz * cz);
        float ang = atan2f(cy, cx);
        float arms = 3.0f;
        float arm = fractf((ang / (float)(2.0 * M_PI)) * arms + r * 3.5f - t * 0.8f);
        float band = arm < 0.35f ? 1.0f - arm / 0.35f : 0.0f;
        float body = 0.12f + 0.25f * (0.5f + 0.5f * sinf(r * 8.0f - t * 2.0f));
        float mix = clampf(fmaxf(band, body), 0.0f, 1.0f);
        hsv = lerp_hsv(hsv, sec, band);
        hsv.v = (uint8_t)((hsv.v / 255.0f) * mix * 255.0f * inten);
        break;
    }
    case PM_EFFECT_STARBURST: {
        float r = sqrtf(cx * cx + cy * cy + cz * cz) + 1e-4f;
        float ang = atan2f(cy, cx);
        float a = ang / (float)(2.0 * M_PI);
        if (a < 0.0f) a += 1.0f;
        float spokes = 8.0f;
        float spoke = powf(fabsf(sinf(a * (float)M_PI * spokes + t * 0.7f)), 2.5f);
        float pulse = 0.5f + 0.5f * sinf(r * 9.0f - t * 3.0f);
        float mix = spoke * (0.45f + 0.55f * pulse);
        mix = fmaxf(mix, 0.15f + 0.2f * pulse);
        mix *= 0.8f + 0.2f * (0.5f + 0.5f * sinf(cz * 6.0f + t));
        hsv = lerp_hsv(hsv, sec, spoke);
        hsv.v = (uint8_t)((hsv.v / 255.0f) * clampf(mix, 0.0f, 1.0f) * 255.0f * inten);
        break;
    }
    case PM_EFFECT_CHEVRON: {
        float u = fabsf(cx) * pat + cy * pat + cz * 2.4f - t * 2.2f;
        float f = fractf(u);
        float band = f < 0.4f ? 1.0f : 0.0f;
        hsv = band > 0.5f ? hsv : sec;
        hsv.v = (uint8_t)(hsv.v * inten);
        break;
    }
    case PM_EFFECT_HELIX: {
        float ang = atan2f(cx, cz);
        float ridge = fabsf(sinf(ang * 2.0f + cy * 10.0f - t * 3.0f));
        float rad2 = cx * cx + cz * cz;
        float tube = expf(-(rad2 * 5.0f));
        float mix = ridge * (0.3f + 0.7f * tube);
        mix = fmaxf(mix, 0.12f + 0.2f * (0.5f + 0.5f * sinf(cy * 8.0f - t * 2.0f)));
        hsv = lerp_hsv(hsv, sec, ridge);
        hsv.v = (uint8_t)((hsv.v / 255.0f) * clampf(mix, 0.0f, 1.0f) * 255.0f * inten);
        break;
    }
    case PM_EFFECT_AXIS_PLANES: {
        float thr = 0.14f;
        float pxp = clampf(1.0f - fabsf(cx) / thr, 0.0f, 1.0f);
        float pyp = clampf(1.0f - fabsf(cy) / thr, 0.0f, 1.0f);
        float pzp = clampf(1.0f - fabsf(cz) / thr, 0.0f, 1.0f);
        float m = fmaxf(pxp, fmaxf(pyp, pzp));
        m = m * m;
        if (pxp >= pyp && pxp >= pzp) {
            /* keep primary */
        } else if (pyp >= pzp) {
            hsv = sec;
        } else {
            hsv = lerp_hsv(hsv, sec, 0.5f);
        }
        hsv.v = (uint8_t)((hsv.v / 255.0f) * clampf(m + 0.1f, 0.0f, 1.0f) * 255.0f * inten);
        break;
    }
    case PM_EFFECT_SCAN_VOLUME: {
        float phase = fractf(t * 0.25f);
        float axis = floorf(phase * 3.0f); /* 0=X 1=Y 2=Z */
        float u = fractf(phase * 3.0f);
        float scan = (axis < 0.5f) ? cx : (axis < 1.5f ? cy : cz);
        float d = fabsf(scan - (u * 2.0f - 1.0f) * 0.55f);
        float bw = 0.14f;
        float band = d < bw ? 1.0f - d / bw : 0.0f;
        float glow = 0.14f;
        hsv = lerp_hsv(hsv, sec, band);
        hsv.v = (uint8_t)((hsv.v / 255.0f) * clampf(band + glow, 0.0f, 1.0f) * 255.0f * inten);
        break;
    }
    case PM_EFFECT_INTERFERENCE: {
        float d1 = sqrtf((cx - 0.25f) * (cx - 0.25f) + cy * cy + cz * cz);
        float d2 = sqrtf((cx + 0.25f) * (cx + 0.25f) + cy * cy + (cz - 0.1f) * (cz - 0.1f));
        float v = sinf(d1 * 14.0f - t * 3.0f) + sinf(d2 * 14.0f + t * 2.2f);
        v = clampf(v * 0.25f + 0.5f, 0.0f, 1.0f);
        hsv = lerp_hsv(hsv, sec, v);
        hsv.v = (uint8_t)((hsv.v / 255.0f) * v * 255.0f * inten);
        break;
    }
    case PM_EFFECT_TRI_LATTICE: {
        float s = cx * pat;
        float qq = cy * pat;
        float r = cz * pat;
        float uu = s + 0.5f * qq;
        float vv = 0.8660254f * qq;
        float ww = r * 0.75f;
        float fu = fabsf(fractf(uu + t * 0.2f) - 0.5f);
        float fv = fabsf(fractf(vv - t * 0.15f) - 0.5f);
        float fw = fabsf(fractf(ww + t * 0.1f) - 0.5f);
        float line = fmaxf(fmaxf(smoothpulse(fu, line_w), smoothpulse(fv, line_w)),
                           smoothpulse(fw, line_w));
        float fill = ((int)floorf(uu) + (int)floorf(vv) + (int)floorf(ww)) & 1 ? 0.38f : 0.14f;
        float mix = clampf(fmaxf(line, fill), 0.0f, 1.0f);
        hsv = lerp_hsv(hsv, sec, fill);
        hsv.v = (uint8_t)((hsv.v / 255.0f) * mix * 255.0f * inten);
        break;
    }
    case PM_EFFECT_CUBIC_PULSE: {
        float d = fmaxf(fabsf(cx), fmaxf(fabsf(cy), fabsf(cz)));
        float pulse = fractf(t * 0.35f);
        float shell_w = 0.08f;
        float shell = fabsf(d - pulse * 0.7f);
        float on = shell < shell_w ? 1.0f - shell / shell_w : 0.0f;
        float core = d < pulse * 0.7f
                         ? (0.2f + 0.35f * (1.0f - d / fmaxf(pulse * 0.7f, 1e-3f)))
                         : 0.08f;
        float mix = clampf(fmaxf(on, core), 0.0f, 1.0f);
        hsv = lerp_hsv(hsv, sec, pulse);
        hsv.v = (uint8_t)((hsv.v / 255.0f) * mix * 255.0f * inten);
        break;
    }
    case PM_EFFECT_POLAR_GRID: {
        float r = sqrtf(cx * cx + cy * cy + cz * cz);
        float ang = atan2f(cy, cx);
        float a = ang / (float)(2.0 * M_PI);
        if (a < 0.0f) a += 1.0f;
        float rings = fabsf(fractf(r * pat - t * 0.5f) - 0.5f);
        float rays = fabsf(fractf(a * 10.0f + t * 0.1f) - 0.5f);
        float m = fmaxf(smoothpulse(rings, line_w), smoothpulse(rays, line_w * 0.85f));
        float body = 0.14f + 0.28f * (0.5f + 0.5f * sinf(r * 7.0f - t * 2.0f));
        float mix = clampf(fmaxf(m, body), 0.0f, 1.0f);
        hsv = lerp_hsv(hsv, sec, m);
        hsv.v = (uint8_t)((hsv.v / 255.0f) * mix * 255.0f * inten);
        break;
    }
    case PM_EFFECT_WAVE_WALLS: {
        float wall_x = 0.5f * sinf(cy * pat + cz * 4.0f - t * 3.0f);
        float wall_y = 0.5f * sinf(cx * pat + cz * 3.0f + t * 2.5f);
        float wall_z = 0.5f * sinf(cx * pat + cy * pat - t * 2.0f);
        float w = 0.2f;
        float dx = clampf(1.0f - fabsf(cx - wall_x) / w, 0.0f, 1.0f);
        float dy = clampf(1.0f - fabsf(cy - wall_y) / w, 0.0f, 1.0f);
        float dz = clampf(1.0f - fabsf(cz - wall_z) / w, 0.0f, 1.0f);
        float m = fmaxf(dx, fmaxf(dy, dz));
        m = 0.15f + 0.85f * (m * m);
        hsv = lerp_hsv(hsv, sec, m);
        hsv.v = (uint8_t)((hsv.v / 255.0f) * m * 255.0f * inten);
        break;
    }
    case PM_EFFECT_CUSTOM_LUA: {
        uint16_t n = pm_pixel_map_count(ctx->map);
        pm_lua_effect_inputs_t lin = {
            .speed = ctx->params.speed,
            .scale = scale,
            .intensity = inten,
            .ph = ctx->params.primary.h,
            .ps = ctx->params.primary.s,
            .pv = ctx->params.primary.v,
            .sh = ctx->params.secondary.h,
            .ss = ctx->params.secondary.s,
            .sv = ctx->params.secondary.v,
        };
        for (int k = 0; k < PM_FX_P_COUNT; ++k) lin.p[k] = ctx->params.p[k];
        return pm_effect_lua_eval(&lin, map_index, n, pos.x, pos.y, pos.z, t);
    }
    default:
        break;
    }

    pm_rgb_t rgb = pm_hsv_to_rgb(hsv);
    if (ctx->params.palette_blend) {
        pm_rgb_t sec_rgb = pm_hsv_to_rgb(ctx->params.secondary);
        rgb = pm_rgb_blend(rgb, sec_rgb, ctx->params.palette_blend);
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
