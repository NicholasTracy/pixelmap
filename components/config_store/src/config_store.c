#include "config_store.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float fxmod_rate_hz(uint8_t rate)
{
    return 0.05f + (rate / 255.0f) * 7.95f;
}

static float fxmod_hash01(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return (x & 0x00ffffffu) / (float)0x01000000u;
}

static float fxmod_wave(pm_fxmod_shape_t shape, float phase01, uint8_t seed)
{
    phase01 = phase01 - floorf(phase01);
    switch (shape) {
    case PM_FXMOD_SINE:
        return 0.5f + 0.5f * sinf(phase01 * 6.2831853f);
    case PM_FXMOD_RAMP:
        return phase01;
    case PM_FXMOD_SQUARE:
        return phase01 < 0.5f ? 1.0f : 0.0f;
    case PM_FXMOD_SAW: {
        float p = phase01 + 0.25f;
        p = p - floorf(p);
        float tri = (p < 0.5f) ? (p * 2.0f) : ((1.0f - p) * 2.0f);
        return tri;
    }
    case PM_FXMOD_NOISE: {
        uint32_t step = (uint32_t)floorf(phase01 * 32.0f);
        return fxmod_hash01(step + 1u + (uint32_t)seed * 131u);
    }
    default:
        return 0.5f;
    }
}

static float fxmod_mix(float base, float lo, float hi, float wave, float depth)
{
    if (hi <= lo) return base;
    float span = hi - lo;
    float center = (base - lo) / span;
    float out_n = clampf(center + (wave - 0.5f) * 2.0f * depth, 0.0f, 1.0f);
    return lo + out_n * span;
}

static uint8_t fxmod_u8(float v)
{
    if (v < 0.0f) return 0;
    if (v > 255.0f) return 255;
    return (uint8_t)(v + 0.5f);
}

#define NS "pixelmap"

/* Common WLED / ESP32 multi-output data pins (safe-ish defaults; custom OK in UI). */
static const int k_default_strip_gpio[PM_STRIP_MAX] = {
    16, 2, 4, 13, 14, 15, 18, 19
};

int pm_config_default_strip_gpio(uint8_t strip_index)
{
    if (strip_index >= PM_STRIP_MAX) strip_index = PM_STRIP_MAX - 1;
    return k_default_strip_gpio[strip_index];
}

void pm_config_set_defaults(pm_app_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->hostname, "pixelmap", sizeof(cfg->hostname) - 1);
    cfg->ap_fallback = true;
    cfg->gpio_data = pm_config_default_strip_gpio(0);
    cfg->gpio_clock = 14;
    cfg->gpio_status_led = 2; /* ESP32 DevKit / common WLED onboard LED */
    cfg->status_led_active_high = true;
    cfg->chipset = PM_CHIPSET_WS2812B;
    cfg->color_order = PM_COLOR_ORDER_GRB;
    cfg->pixel_count = 60;
    cfg->strip_count = 1;
    cfg->strip_len[0] = 60;
    cfg->strip_gpio[0] = pm_config_default_strip_gpio(0);
    for (int i = 1; i < PM_STRIP_MAX; ++i) {
        cfg->strip_len[i] = 0;
        cfg->strip_gpio[i] = pm_config_default_strip_gpio((uint8_t)i);
    }
    cfg->brightness = 128;
    cfg->gamma = 220;
    cfg->auto_white = true;
    cfg->effect_id = PM_EFFECT_RAINBOW_SPATIAL;
    cfg->effect_speed = 1.0f;
    cfg->effect_scale = 1.0f;
    cfg->effect_intensity = 255;
    cfg->effect_primary_h = 0;
    cfg->effect_primary_s = 255;
    cfg->effect_primary_v = 255;
    cfg->effect_secondary_h = 160;
    cfg->effect_secondary_s = 255;
    cfg->effect_secondary_v = 255;
    for (int i = 0; i < PM_FX_P_COUNT; ++i) {
        cfg->effect_p[i] = (i == 0) ? 128 : (i == 1) ? 64 : 0;
    }
    for (int i = 0; i < 3; ++i) {
        cfg->effect_pos[i] = 128; /* center */
        cfg->effect_rot[i] = 0;
    }
    /* Fixed Art-Net/sACN map: 1..23 → speed..p8, pos xyz, rot xyz */
    for (int i = 0; i < PM_FXPARAM_COUNT; ++i) {
        cfg->effect_param_ch[i] = (uint16_t)(i + 1);
        cfg->effect_mod[i] = (pm_fxmod_t){
            .shape = PM_FXMOD_OFF,
            .depth = 128,
            .rate = 40, /* ~1.3 Hz */
            .phase = 0,
        };
    }
    cfg->dmx_mode = PM_DMX_MODE_PARAMS;
    cfg->artnet_universe = 0;
    cfg->sacn_universe = 0;
    cfg->universe_count = 4;
    cfg->artnet_enable = false;
    cfg->sacn_enable = false;
    cfg->map_width = 10;
    cfg->map_height = 6;
    cfg->map_depth = 1;
    cfg->map_dim = PM_MAP_DIM_2D;
    cfg->map_layout = PM_MAP_LAYOUT_GRID;
    cfg->map_fill = PM_MAP_FILL_SURFACE;
    cfg->map_open_tb = false;
    cfg->map_spacing = 1.0f;

    cfg->pov_enable = false;
    cfg->pov_mode = PM_POV_ROTATION;
    cfg->pov_layout = PM_POV_LAYOUT_DIAMETER;
    cfg->pov_blade_count = 2;
    cfg->pov_rpm = 600.0f;
    cfg->pov_linear_speed_mps = 4.0f;
    cfg->pov_radius_m = 0.25f;
    cfg->pov_path_length_m = 1.0f;
}

void pm_config_fill_effect_params(const pm_app_config_t *cfg, pm_effect_params_t *out)
{
    if (!cfg || !out) return;
    *out = (pm_effect_params_t){0};
    out->id = cfg->effect_id;
    out->speed = cfg->effect_speed;
    out->scale = cfg->effect_scale > 1e-4f ? cfg->effect_scale : 1.0f;
    out->intensity = cfg->effect_intensity / 255.0f;
    out->primary = (pm_hsv_t){
        cfg->effect_primary_h, cfg->effect_primary_s, cfg->effect_primary_v
    };
    out->secondary = (pm_hsv_t){
        cfg->effect_secondary_h, cfg->effect_secondary_s, cfg->effect_secondary_v
    };
    out->palette_blend = 0;
    for (int i = 0; i < PM_FX_P_COUNT; ++i) {
        out->p[i] = cfg->effect_p[i] / 255.0f;
    }
    for (int i = 0; i < 3; ++i) {
        out->pos[i] = (cfg->effect_pos[i] / 255.0f) - 0.5f;
        out->rot[i] = (cfg->effect_rot[i] / 255.0f) * 360.0f;
    }
}

void pm_config_apply_fx_dmx(pm_app_config_t *cfg, const uint8_t *merge, size_t merge_len)
{
    if (!cfg || !merge || merge_len == 0) return;
    for (int i = 0; i < PM_FXPARAM_COUNT; ++i) {
        uint16_t ch = cfg->effect_param_ch[i];
        if (ch == 0 || (size_t)ch > merge_len) continue;
        uint8_t b = merge[ch - 1];
        switch ((pm_fxparam_id_t)i) {
        case PM_FXPARAM_SPEED:
            cfg->effect_speed = (b / 255.0f) * 10.0f;
            break;
        case PM_FXPARAM_SCALE:
            cfg->effect_scale = 0.1f + (b / 255.0f) * 7.9f;
            break;
        case PM_FXPARAM_INTENSITY:
            cfg->effect_intensity = b;
            break;
        case PM_FXPARAM_PRIMARY_H: cfg->effect_primary_h = b; break;
        case PM_FXPARAM_PRIMARY_S: cfg->effect_primary_s = b; break;
        case PM_FXPARAM_PRIMARY_V: cfg->effect_primary_v = b; break;
        case PM_FXPARAM_SECONDARY_H: cfg->effect_secondary_h = b; break;
        case PM_FXPARAM_SECONDARY_S: cfg->effect_secondary_s = b; break;
        case PM_FXPARAM_SECONDARY_V: cfg->effect_secondary_v = b; break;
        case PM_FXPARAM_P1: case PM_FXPARAM_P2: case PM_FXPARAM_P3: case PM_FXPARAM_P4:
        case PM_FXPARAM_P5: case PM_FXPARAM_P6: case PM_FXPARAM_P7: case PM_FXPARAM_P8:
            cfg->effect_p[i - PM_FXPARAM_P1] = b;
            break;
        case PM_FXPARAM_POS_X: case PM_FXPARAM_POS_Y: case PM_FXPARAM_POS_Z:
            cfg->effect_pos[i - PM_FXPARAM_POS_X] = b;
            break;
        case PM_FXPARAM_ROT_X: case PM_FXPARAM_ROT_Y: case PM_FXPARAM_ROT_Z:
            cfg->effect_rot[i - PM_FXPARAM_ROT_X] = b;
            break;
        default:
            break;
        }
    }
}

void pm_config_apply_fx_mods(pm_app_config_t *cfg, uint32_t time_ms)
{
    if (!cfg) return;
    float t = time_ms * 0.001f;
    for (int i = 0; i < PM_FXPARAM_COUNT; ++i) {
        const pm_fxmod_t *m = &cfg->effect_mod[i];
        if (m->shape == PM_FXMOD_OFF || m->depth == 0) continue;
        float hz = fxmod_rate_hz(m->rate);
        float phase = t * hz + (m->phase / 255.0f);
        float wave = fxmod_wave((pm_fxmod_shape_t)m->shape, phase, (uint8_t)(i * 17u + m->phase));
        float depth = m->depth / 255.0f;
        switch ((pm_fxparam_id_t)i) {
        case PM_FXPARAM_SPEED:
            cfg->effect_speed = fxmod_mix(cfg->effect_speed, 0.0f, 10.0f, wave, depth);
            break;
        case PM_FXPARAM_SCALE:
            cfg->effect_scale = fxmod_mix(cfg->effect_scale, 0.1f, 8.0f, wave, depth);
            break;
        case PM_FXPARAM_INTENSITY:
            cfg->effect_intensity = fxmod_u8(
                fxmod_mix((float)cfg->effect_intensity, 0.0f, 255.0f, wave, depth));
            break;
        case PM_FXPARAM_PRIMARY_H:
            cfg->effect_primary_h = fxmod_u8(
                fxmod_mix((float)cfg->effect_primary_h, 0.0f, 255.0f, wave, depth));
            break;
        case PM_FXPARAM_PRIMARY_S:
            cfg->effect_primary_s = fxmod_u8(
                fxmod_mix((float)cfg->effect_primary_s, 0.0f, 255.0f, wave, depth));
            break;
        case PM_FXPARAM_PRIMARY_V:
            cfg->effect_primary_v = fxmod_u8(
                fxmod_mix((float)cfg->effect_primary_v, 0.0f, 255.0f, wave, depth));
            break;
        case PM_FXPARAM_SECONDARY_H:
            cfg->effect_secondary_h = fxmod_u8(
                fxmod_mix((float)cfg->effect_secondary_h, 0.0f, 255.0f, wave, depth));
            break;
        case PM_FXPARAM_SECONDARY_S:
            cfg->effect_secondary_s = fxmod_u8(
                fxmod_mix((float)cfg->effect_secondary_s, 0.0f, 255.0f, wave, depth));
            break;
        case PM_FXPARAM_SECONDARY_V:
            cfg->effect_secondary_v = fxmod_u8(
                fxmod_mix((float)cfg->effect_secondary_v, 0.0f, 255.0f, wave, depth));
            break;
        case PM_FXPARAM_P1: case PM_FXPARAM_P2: case PM_FXPARAM_P3: case PM_FXPARAM_P4:
        case PM_FXPARAM_P5: case PM_FXPARAM_P6: case PM_FXPARAM_P7: case PM_FXPARAM_P8: {
            int pi = i - PM_FXPARAM_P1;
            cfg->effect_p[pi] = fxmod_u8(
                fxmod_mix((float)cfg->effect_p[pi], 0.0f, 255.0f, wave, depth));
            break;
        }
        case PM_FXPARAM_POS_X: case PM_FXPARAM_POS_Y: case PM_FXPARAM_POS_Z: {
            int pi = i - PM_FXPARAM_POS_X;
            cfg->effect_pos[pi] = fxmod_u8(
                fxmod_mix((float)cfg->effect_pos[pi], 0.0f, 255.0f, wave, depth));
            break;
        }
        case PM_FXPARAM_ROT_X: case PM_FXPARAM_ROT_Y: case PM_FXPARAM_ROT_Z: {
            int pi = i - PM_FXPARAM_ROT_X;
            cfg->effect_rot[pi] = fxmod_u8(
                fxmod_mix((float)cfg->effect_rot[pi], 0.0f, 255.0f, wave, depth));
            break;
        }
        default:
            break;
        }
    }
}

void pm_config_sync_strips(pm_app_config_t *cfg)
{
    if (!cfg) return;
    if (cfg->strip_count < 1) cfg->strip_count = 1;
    if (cfg->strip_count > PM_STRIP_MAX) cfg->strip_count = PM_STRIP_MAX;
    uint32_t sum = 0;
    for (uint8_t i = 0; i < PM_STRIP_MAX; ++i) {
        if (i >= cfg->strip_count) {
            cfg->strip_len[i] = 0;
            continue;
        }
        if (cfg->strip_len[i] < 1) cfg->strip_len[i] = 1;
        if (cfg->strip_len[i] > 4096) cfg->strip_len[i] = 4096;
        sum += cfg->strip_len[i];
        /* Valid ESP32 GPIO range; allow -1 only as “unused” (clamped to default) */
        if (cfg->strip_gpio[i] < 0 || cfg->strip_gpio[i] > 48) {
            cfg->strip_gpio[i] = pm_config_default_strip_gpio(i);
        }
    }
    if (sum < 1) sum = 1;
    if (sum > 4096) sum = 4096;
    cfg->pixel_count = (uint16_t)sum;
    cfg->gpio_data = cfg->strip_gpio[0];
}

static void coerce_unsupported_chipset(pm_app_config_t *cfg)
{
    /* APA102/SK9822 SPI path is not implemented yet — fall back safely. */
    if (cfg->chipset == PM_CHIPSET_APA102 || cfg->chipset == PM_CHIPSET_SK9822 ||
        cfg->chipset == PM_CHIPSET_CUSTOM) {
        cfg->chipset = PM_CHIPSET_WS2812B;
    }
}

esp_err_t pm_config_load(pm_app_config_t *cfg)
{
    pm_config_set_defaults(cfg);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    /* First boot / erased NVS: keep defaults and succeed. */
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;

    size_t len = sizeof(cfg->sta_ssid);
    nvs_get_str(h, "ssid", cfg->sta_ssid, &len);
    len = sizeof(cfg->sta_pass);
    nvs_get_str(h, "pass", cfg->sta_pass, &len);
    len = sizeof(cfg->hostname);
    nvs_get_str(h, "host", cfg->hostname, &len);

    int32_t v;
    if (nvs_get_i32(h, "gpio", &v) == ESP_OK) cfg->gpio_data = v;
    if (nvs_get_i32(h, "clk", &v) == ESP_OK) cfg->gpio_clock = v;
    if (nvs_get_i32(h, "sled", &v) == ESP_OK) cfg->gpio_status_led = v;
    if (nvs_get_i32(h, "sledh", &v) == ESP_OK) cfg->status_led_active_high = v != 0;
    if (nvs_get_i32(h, "chip", &v) == ESP_OK) cfg->chipset = (pm_chipset_t)v;
    if (nvs_get_i32(h, "order", &v) == ESP_OK) cfg->color_order = (pm_color_order_t)v;
    if (nvs_get_i32(h, "count", &v) == ESP_OK) cfg->pixel_count = (uint16_t)v;
    if (nvs_get_i32(h, "scnt", &v) == ESP_OK) cfg->strip_count = (uint8_t)v;
    bool have_sl = false;
    bool have_sg = false;
    for (int i = 0; i < PM_STRIP_MAX; ++i) {
        char key[8];
        key[0] = 's'; key[1] = 'l'; key[2] = (char)('0' + i); key[3] = '\0';
        if (nvs_get_i32(h, key, &v) == ESP_OK) {
            cfg->strip_len[i] = (uint16_t)v;
            have_sl = true;
        }
        key[1] = 'g';
        if (nvs_get_i32(h, key, &v) == ESP_OK) {
            cfg->strip_gpio[i] = (int)v;
            have_sg = true;
        }
    }
    if (!have_sl) {
        cfg->strip_count = 1;
        cfg->strip_len[0] = cfg->pixel_count ? cfg->pixel_count : 60;
    }
    if (!have_sg) {
        cfg->strip_gpio[0] = cfg->gpio_data;
        for (int i = 1; i < PM_STRIP_MAX; ++i) {
            cfg->strip_gpio[i] = pm_config_default_strip_gpio((uint8_t)i);
        }
    }
    pm_config_sync_strips(cfg);
    if (nvs_get_i32(h, "bri", &v) == ESP_OK) cfg->brightness = (uint8_t)v;
    if (nvs_get_i32(h, "gamma", &v) == ESP_OK) cfg->gamma = (uint8_t)v;
    if (nvs_get_i32(h, "aw", &v) == ESP_OK) cfg->auto_white = v != 0;
    if (nvs_get_i32(h, "fx", &v) == ESP_OK) {
        cfg->effect_id = (v >= 0 && v < (int)PM_EFFECT_COUNT)
                             ? (pm_effect_id_t)v
                             : PM_EFFECT_RAINBOW_SPATIAL;
    }
    if (nvs_get_i32(h, "fxint", &v) == ESP_OK) cfg->effect_intensity = (uint8_t)v;
    if (nvs_get_i32(h, "fxspd", &v) == ESP_OK) cfg->effect_speed = (float)v / 100.0f;
    if (nvs_get_i32(h, "fxscl", &v) == ESP_OK) cfg->effect_scale = (float)v / 100.0f;
    if (nvs_get_i32(h, "fxph", &v) == ESP_OK) cfg->effect_primary_h = (uint8_t)v;
    if (nvs_get_i32(h, "fxps", &v) == ESP_OK) cfg->effect_primary_s = (uint8_t)v;
    if (nvs_get_i32(h, "fxpv", &v) == ESP_OK) cfg->effect_primary_v = (uint8_t)v;
    if (nvs_get_i32(h, "fxsh", &v) == ESP_OK) cfg->effect_secondary_h = (uint8_t)v;
    if (nvs_get_i32(h, "fxss", &v) == ESP_OK) cfg->effect_secondary_s = (uint8_t)v;
    if (nvs_get_i32(h, "fxsv", &v) == ESP_OK) cfg->effect_secondary_v = (uint8_t)v;
    {
        size_t blen = sizeof(cfg->effect_p);
        nvs_get_blob(h, "fxp", cfg->effect_p, &blen);
    }
    {
        size_t blen = sizeof(cfg->effect_pos);
        nvs_get_blob(h, "fxpos", cfg->effect_pos, &blen);
    }
    {
        size_t blen = sizeof(cfg->effect_rot);
        nvs_get_blob(h, "fxrot", cfg->effect_rot, &blen);
    }
    /* Channel map is fixed — ignore any legacy per-slider bindings in NVS */
    for (int i = 0; i < PM_FXPARAM_COUNT; ++i) {
        cfg->effect_param_ch[i] = (uint16_t)(i + 1);
    }
    {
        size_t blen = sizeof(cfg->effect_mod);
        nvs_get_blob(h, "fxmod", cfg->effect_mod, &blen);
        for (int i = 0; i < PM_FXPARAM_COUNT; ++i) {
            if (cfg->effect_mod[i].shape > PM_FXMOD_NOISE) {
                cfg->effect_mod[i].shape = PM_FXMOD_OFF;
            }
        }
    }
    if (nvs_get_i32(h, "dmxmode", &v) == ESP_OK) {
        cfg->dmx_mode = (v == (int)PM_DMX_MODE_PIXELS) ? PM_DMX_MODE_PIXELS : PM_DMX_MODE_PARAMS;
    }
    if (nvs_get_i32(h, "aun", &v) == ESP_OK) cfg->artnet_universe = (uint16_t)v;
    if (nvs_get_i32(h, "sun", &v) == ESP_OK) cfg->sacn_universe = (uint16_t)v;
    /* Shared universe — prefer Art-Net value when both exist */
    cfg->sacn_universe = cfg->artnet_universe;
    if (nvs_get_i32(h, "ucnt", &v) == ESP_OK) cfg->universe_count = (uint16_t)v;
    if (nvs_get_i32(h, "aen", &v) == ESP_OK) cfg->artnet_enable = v != 0;
    if (nvs_get_i32(h, "sen", &v) == ESP_OK) cfg->sacn_enable = v != 0;
    if (cfg->artnet_enable && cfg->sacn_enable) cfg->sacn_enable = false;
    if (nvs_get_i32(h, "mw", &v) == ESP_OK) cfg->map_width = (uint16_t)v;
    if (nvs_get_i32(h, "mh", &v) == ESP_OK) cfg->map_height = (uint16_t)v;
    if (nvs_get_i32(h, "md", &v) == ESP_OK) cfg->map_depth = (uint16_t)v;
    if (nvs_get_i32(h, "mdim", &v) == ESP_OK) cfg->map_dim = (pm_map_dim_t)v;
    if (nvs_get_i32(h, "mlay", &v) == ESP_OK) cfg->map_layout = (pm_map_layout_t)v;
    if (nvs_get_i32(h, "mfill", &v) == ESP_OK) cfg->map_fill = (pm_map_fill_t)(v != 0 ? 1 : 0);
    if (nvs_get_i32(h, "mopentb", &v) == ESP_OK) cfg->map_open_tb = v != 0;
    if (nvs_get_i32(h, "mspc", &v) == ESP_OK) {
        cfg->map_spacing = (float)v / 1000.0f;
        if (cfg->map_spacing < 1e-4f) cfg->map_spacing = 1.0f;
    }

    if (nvs_get_i32(h, "pove", &v) == ESP_OK) cfg->pov_enable = v != 0;
    if (nvs_get_i32(h, "povm", &v) == ESP_OK) cfg->pov_mode = (pm_pov_mode_t)v;
    if (nvs_get_i32(h, "poyl", &v) == ESP_OK) cfg->pov_layout = (pm_pov_layout_t)v;
    if (nvs_get_i32(h, "povbl", &v) == ESP_OK) cfg->pov_blade_count = pm_pov_clamp_blades((uint8_t)v);
    if (nvs_get_i32(h, "povrpm", &v) == ESP_OK) cfg->pov_rpm = (float)v / 100.0f;
    if (nvs_get_i32(h, "povspd", &v) == ESP_OK) cfg->pov_linear_speed_mps = (float)v / 100.0f;
    if (nvs_get_i32(h, "povrad", &v) == ESP_OK) cfg->pov_radius_m = (float)v / 1000.0f;
    if (nvs_get_i32(h, "povpath", &v) == ESP_OK) cfg->pov_path_length_m = (float)v / 1000.0f;

    nvs_close(h);
    coerce_unsupported_chipset(cfg);
    pm_config_sync_strips(cfg);
    return ESP_OK;
}

esp_err_t pm_config_save(const pm_app_config_t *cfg)
{
    pm_app_config_t local = *cfg;
    pm_config_sync_strips(&local);
    if (local.artnet_enable && local.sacn_enable) local.sacn_enable = false;
    local.sacn_universe = local.artnet_universe;
    cfg = &local;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    nvs_set_str(h, "ssid", cfg->sta_ssid);
    nvs_set_str(h, "pass", cfg->sta_pass);
    nvs_set_str(h, "host", cfg->hostname);
    nvs_set_i32(h, "gpio", cfg->gpio_data);
    nvs_set_i32(h, "clk", cfg->gpio_clock);
    nvs_set_i32(h, "sled", cfg->gpio_status_led);
    nvs_set_i32(h, "sledh", cfg->status_led_active_high ? 1 : 0);
    nvs_set_i32(h, "chip", (int32_t)cfg->chipset);
    nvs_set_i32(h, "order", (int32_t)cfg->color_order);
    nvs_set_i32(h, "count", cfg->pixel_count);
    nvs_set_i32(h, "scnt", cfg->strip_count);
    for (int i = 0; i < PM_STRIP_MAX; ++i) {
        char key[8];
        key[0] = 's'; key[1] = 'l'; key[2] = (char)('0' + i); key[3] = '\0';
        nvs_set_i32(h, key, (int32_t)cfg->strip_len[i]);
        key[1] = 'g';
        nvs_set_i32(h, key, (int32_t)cfg->strip_gpio[i]);
    }
    nvs_set_i32(h, "bri", cfg->brightness);
    nvs_set_i32(h, "gamma", cfg->gamma);
    nvs_set_i32(h, "aw", cfg->auto_white ? 1 : 0);
    nvs_set_i32(h, "fx", (int32_t)cfg->effect_id);
    nvs_set_i32(h, "fxint", cfg->effect_intensity);
    nvs_set_i32(h, "fxspd", (int32_t)(cfg->effect_speed * 100.0f));
    nvs_set_i32(h, "fxscl", (int32_t)(cfg->effect_scale * 100.0f));
    nvs_set_i32(h, "fxph", cfg->effect_primary_h);
    nvs_set_i32(h, "fxps", cfg->effect_primary_s);
    nvs_set_i32(h, "fxpv", cfg->effect_primary_v);
    nvs_set_i32(h, "fxsh", cfg->effect_secondary_h);
    nvs_set_i32(h, "fxss", cfg->effect_secondary_s);
    nvs_set_i32(h, "fxsv", cfg->effect_secondary_v);
    nvs_set_blob(h, "fxp", cfg->effect_p, sizeof(cfg->effect_p));
    nvs_set_blob(h, "fxpos", cfg->effect_pos, sizeof(cfg->effect_pos));
    nvs_set_blob(h, "fxrot", cfg->effect_rot, sizeof(cfg->effect_rot));
    nvs_set_blob(h, "fxch", cfg->effect_param_ch, sizeof(cfg->effect_param_ch));
    nvs_set_blob(h, "fxmod", cfg->effect_mod, sizeof(cfg->effect_mod));
    nvs_set_i32(h, "dmxmode", (int32_t)cfg->dmx_mode);
    nvs_set_i32(h, "aun", cfg->artnet_universe);
    nvs_set_i32(h, "sun", cfg->sacn_universe);
    nvs_set_i32(h, "ucnt", cfg->universe_count);
    nvs_set_i32(h, "aen", cfg->artnet_enable ? 1 : 0);
    nvs_set_i32(h, "sen", cfg->sacn_enable ? 1 : 0);
    nvs_set_i32(h, "mw", cfg->map_width);
    nvs_set_i32(h, "mh", cfg->map_height);
    nvs_set_i32(h, "md", cfg->map_depth);
    nvs_set_i32(h, "mdim", (int32_t)cfg->map_dim);
    nvs_set_i32(h, "mlay", (int32_t)cfg->map_layout);
    nvs_set_i32(h, "mfill", (int32_t)cfg->map_fill);
    nvs_set_i32(h, "mopentb", cfg->map_open_tb ? 1 : 0);
    nvs_set_i32(h, "mspc", (int32_t)(cfg->map_spacing * 1000.0f));
    nvs_set_i32(h, "pove", cfg->pov_enable ? 1 : 0);
    nvs_set_i32(h, "povm", (int32_t)cfg->pov_mode);
    nvs_set_i32(h, "poyl", (int32_t)cfg->pov_layout);
    nvs_set_i32(h, "povbl", (int32_t)pm_pov_clamp_blades(cfg->pov_blade_count));
    nvs_set_i32(h, "povrpm", (int32_t)(cfg->pov_rpm * 100.0f));
    nvs_set_i32(h, "povspd", (int32_t)(cfg->pov_linear_speed_mps * 100.0f));
    nvs_set_i32(h, "povrad", (int32_t)(cfg->pov_radius_m * 1000.0f));
    nvs_set_i32(h, "povpath", (int32_t)(cfg->pov_path_length_m * 1000.0f));
    err = nvs_commit(h);
    nvs_close(h);
    return err;
}
