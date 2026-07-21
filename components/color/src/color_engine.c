#include "color_engine.h"
#include <ctype.h>
#include <string.h>

static int order_ieq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static inline uint8_t qadd8(uint8_t a, uint8_t b)
{
    uint16_t s = (uint16_t)a + b;
    return s > 255 ? 255 : (uint8_t)s;
}

static inline uint8_t scale8(uint8_t v, uint8_t scale)
{
    return (uint8_t)(((uint16_t)v * scale) / 255);
}

pm_rgb_t pm_hsv_to_rgb(pm_hsv_t hsv)
{
    pm_rgb_t out = {0, 0, 0};
    if (hsv.s == 0) {
        out.r = out.g = out.b = hsv.v;
        return out;
    }

    uint8_t region = hsv.h / 43;
    uint8_t rem = (uint8_t)((hsv.h - (region * 43)) * 6);
    uint8_t p = scale8(hsv.v, 255 - hsv.s);
    uint8_t q = scale8(hsv.v, 255 - scale8(hsv.s, rem));
    uint8_t t = scale8(hsv.v, 255 - scale8(hsv.s, 255 - rem));

    switch (region) {
    case 0:  out.r = hsv.v; out.g = t;     out.b = p; break;
    case 1:  out.r = q;     out.g = hsv.v; out.b = p; break;
    case 2:  out.r = p;     out.g = hsv.v; out.b = t; break;
    case 3:  out.r = p;     out.g = q;     out.b = hsv.v; break;
    case 4:  out.r = t;     out.g = p;     out.b = hsv.v; break;
    default: out.r = hsv.v; out.g = p;     out.b = q; break;
    }
    return out;
}

pm_hsv_t pm_rgb_to_hsv(pm_rgb_t rgb)
{
    pm_hsv_t out = {0, 0, 0};
    uint8_t mn = rgb.r < rgb.g ? (rgb.r < rgb.b ? rgb.r : rgb.b)
                               : (rgb.g < rgb.b ? rgb.g : rgb.b);
    uint8_t mx = rgb.r > rgb.g ? (rgb.r > rgb.b ? rgb.r : rgb.b)
                               : (rgb.g > rgb.b ? rgb.g : rgb.b);
    out.v = mx;
    uint8_t delta = mx - mn;
    if (mx == 0 || delta == 0) {
        out.s = 0;
        out.h = 0;
        return out;
    }
    out.s = (uint8_t)(((uint16_t)delta * 255) / mx);

    if (mx == rgb.r) {
        int16_t h = 0 + ((int16_t)rgb.g - rgb.b) * 43 / delta;
        if (h < 0) h += 256;
        out.h = (uint8_t)h;
    } else if (mx == rgb.g) {
        out.h = (uint8_t)(85 + ((int16_t)rgb.b - rgb.r) * 43 / delta);
    } else {
        out.h = (uint8_t)(171 + ((int16_t)rgb.r - rgb.g) * 43 / delta);
    }
    return out;
}

pm_rgb_t pm_rgb_blend(pm_rgb_t a, pm_rgb_t b, uint8_t amount)
{
    pm_rgb_t o;
    o.r = scale8(a.r, 255 - amount) + scale8(b.r, amount);
    o.g = scale8(a.g, 255 - amount) + scale8(b.g, amount);
    o.b = scale8(a.b, 255 - amount) + scale8(b.b, amount);
    return o;
}

pm_rgb_t pm_rgb_scale(pm_rgb_t c, uint8_t scale)
{
    return (pm_rgb_t){scale8(c.r, scale), scale8(c.g, scale), scale8(c.b, scale)};
}

pm_rgb_t pm_rgb_add(pm_rgb_t a, pm_rgb_t b)
{
    return (pm_rgb_t){qadd8(a.r, b.r), qadd8(a.g, b.g), qadd8(a.b, b.b)};
}

uint8_t pm_gamma8(uint8_t v, uint8_t gamma_x100)
{
    if (gamma_x100 <= 100 || v == 0 || v == 255) {
        return v;
    }
    /* Compact approximation of v^(gamma/100) in 8-bit space */
    float x = v / 255.0f;
    float y;
    /* pow via exp/log would need libm; use iterative for common 2.2 */
    if (gamma_x100 >= 200 && gamma_x100 <= 240) {
        y = x * x * (0.8f + 0.2f * x); /* ≈ x^2.2 */
    } else {
        y = x * x;
        (void)gamma_x100;
    }
    int out = (int)(y * 255.0f + 0.5f);
    if (out < 0) out = 0;
    if (out > 255) out = 255;
    return (uint8_t)out;
}

pm_rgb_t pm_apply_correction(pm_rgb_t in, const pm_color_correction_t *cc)
{
    if (!cc) {
        return in;
    }
    pm_rgb_t c = in;
    c.r = scale8(c.r, cc->correction.r);
    c.g = scale8(c.g, cc->correction.g);
    c.b = scale8(c.b, cc->correction.b);
    c.r = scale8(c.r, cc->temperature.r);
    c.g = scale8(c.g, cc->temperature.g);
    c.b = scale8(c.b, cc->temperature.b);
    c.r = pm_gamma8(c.r, cc->gamma);
    c.g = pm_gamma8(c.g, cc->gamma);
    c.b = pm_gamma8(c.b, cc->gamma);
    c = pm_rgb_scale(c, cc->brightness);
    return c;
}

pm_rgbw_t pm_rgb_to_rgbw(pm_rgb_t in, bool auto_white)
{
    pm_rgbw_t o = {in.r, in.g, in.b, 0};
    if (!auto_white) {
        return o;
    }
    uint8_t w = in.r;
    if (in.g < w) w = in.g;
    if (in.b < w) w = in.b;
    o.r = (uint8_t)(in.r - w);
    o.g = (uint8_t)(in.g - w);
    o.b = (uint8_t)(in.b - w);
    o.w = w;
    return o;
}

pm_rgbww_t pm_rgb_to_rgbww(pm_rgb_t in, bool auto_white, uint8_t warm_mix)
{
    pm_rgbw_t base = pm_rgb_to_rgbw(in, auto_white);
    pm_rgbww_t o = {base.r, base.g, base.b, 0, 0};
    o.w1 = scale8(base.w, 255 - warm_mix); /* cool */
    o.w2 = scale8(base.w, warm_mix);       /* warm */
    return o;
}

const char *pm_color_order_name(pm_color_order_t order)
{
    switch (order) {
    case PM_COLOR_ORDER_RGB: return "RGB";
    case PM_COLOR_ORDER_RBG: return "RBG";
    case PM_COLOR_ORDER_GRB: return "GRB";
    case PM_COLOR_ORDER_GBR: return "GBR";
    case PM_COLOR_ORDER_BRG: return "BRG";
    case PM_COLOR_ORDER_BGR: return "BGR";
    case PM_COLOR_ORDER_RGBW: return "RGBW";
    case PM_COLOR_ORDER_GRBW: return "GRBW";
    case PM_COLOR_ORDER_WRGB: return "WRGB";
    default: return "GRB";
    }
}

pm_color_order_t pm_color_order_from_name(const char *name)
{
    if (!name) return PM_COLOR_ORDER_GRB;
    if (order_ieq(name, "RGB")) return PM_COLOR_ORDER_RGB;
    if (order_ieq(name, "RBG")) return PM_COLOR_ORDER_RBG;
    if (order_ieq(name, "GRB")) return PM_COLOR_ORDER_GRB;
    if (order_ieq(name, "GBR")) return PM_COLOR_ORDER_GBR;
    if (order_ieq(name, "BRG")) return PM_COLOR_ORDER_BRG;
    if (order_ieq(name, "BGR")) return PM_COLOR_ORDER_BGR;
    if (order_ieq(name, "RGBW")) return PM_COLOR_ORDER_RGBW;
    if (order_ieq(name, "GRBW")) return PM_COLOR_ORDER_GRBW;
    if (order_ieq(name, "WRGB")) return PM_COLOR_ORDER_WRGB;
    return PM_COLOR_ORDER_GRB;
}

void pm_pack_pixel(const uint8_t *channels, int channel_count,
                   pm_color_order_t order, uint8_t *out, int *out_len)
{
    uint8_t r = channel_count > 0 ? channels[0] : 0;
    uint8_t g = channel_count > 1 ? channels[1] : 0;
    uint8_t b = channel_count > 2 ? channels[2] : 0;
    uint8_t w = channel_count > 3 ? channels[3] : 0;

    switch (order) {
    case PM_COLOR_ORDER_RGB: out[0]=r; out[1]=g; out[2]=b; *out_len=3; break;
    case PM_COLOR_ORDER_RBG: out[0]=r; out[1]=b; out[2]=g; *out_len=3; break;
    case PM_COLOR_ORDER_GRB: out[0]=g; out[1]=r; out[2]=b; *out_len=3; break;
    case PM_COLOR_ORDER_GBR: out[0]=g; out[1]=b; out[2]=r; *out_len=3; break;
    case PM_COLOR_ORDER_BRG: out[0]=b; out[1]=r; out[2]=g; *out_len=3; break;
    case PM_COLOR_ORDER_BGR: out[0]=b; out[1]=g; out[2]=r; *out_len=3; break;
    case PM_COLOR_ORDER_RGBW: out[0]=r; out[1]=g; out[2]=b; out[3]=w; *out_len=4; break;
    case PM_COLOR_ORDER_GRBW: out[0]=g; out[1]=r; out[2]=b; out[3]=w; *out_len=4; break;
    case PM_COLOR_ORDER_WRGB: out[0]=w; out[1]=r; out[2]=g; out[3]=b; *out_len=4; break;
    default: out[0]=r; out[1]=g; out[2]=b; *out_len=3; break;
    }
}
