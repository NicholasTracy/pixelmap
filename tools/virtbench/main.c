/**
 * Host-side virtual LED bench — map → effect → encode → assert.
 * No ESP-IDF / RMT. Run from CI or: cmake -S tools/virtbench -B build-virtbench && cmake --build ...
 */
#include "pixel_map.h"
#include "effects.h"
#include "color_engine.h"
#include "led_strip.h"
#include "pov.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

static int g_fails;

static void expect_true(bool cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        g_fails++;
    } else {
        printf("  ok: %s\n", msg);
    }
}

static void expect_eq_u8(uint8_t a, uint8_t b, const char *msg)
{
    if (a != b) {
        fprintf(stderr, "FAIL: %s (got %u want %u)\n", msg, (unsigned)a, (unsigned)b);
        g_fails++;
    } else {
        printf("  ok: %s\n", msg);
    }
}

static void expect_eq_sz(size_t a, size_t b, const char *msg)
{
    if (a != b) {
        fprintf(stderr, "FAIL: %s (got %zu want %zu)\n", msg, a, b);
        g_fails++;
    } else {
        printf("  ok: %s\n", msg);
    }
}

typedef struct {
    uint8_t *rgb; /* count * 3 */
    uint16_t count;
} virt_strip_t;

static void set_px(void *user, uint16_t strip_index, pm_rgb_t color)
{
    virt_strip_t *s = user;
    if (!s || strip_index >= s->count) return;
    s->rgb[(size_t)strip_index * 3 + 0] = color.r;
    s->rgb[(size_t)strip_index * 3 + 1] = color.g;
    s->rgb[(size_t)strip_index * 3 + 2] = color.b;
}

static pm_color_correction_t linear_cc(uint8_t bri)
{
    return (pm_color_correction_t){
        .correction = {255, 255, 255},
        .temperature = {255, 255, 255},
        .gamma = 100, /* linear */
        .brightness = bri,
        .auto_white = false,
        .warm_mix = 128,
    };
}

static pm_effect_params_t base_params(pm_effect_id_t id)
{
    pm_effect_params_t p = {0};
    p.id = id;
    p.speed = 1.0f;
    p.scale = 1.0f;
    p.intensity = 1.0f;
    p.primary = (pm_hsv_t){0, 255, 255};
    p.secondary = (pm_hsv_t){128, 255, 255};
    p.pos[0] = p.pos[1] = p.pos[2] = 0.0f;
    p.rot[0] = p.rot[1] = p.rot[2] = 0.0f;
    return p;
}

static void test_solid_grb_pack(void)
{
    printf("scenario: solid GRB pack\n");
    pm_pixel_map_t *map = NULL;
    pm_pixel_map_config_t mc = {.capacity = 16, .width = 1, .height = 1, .depth = 1};
    expect_true(pm_pixel_map_create(&mc, &map) == ESP_OK, "create map");
    expect_true(pm_pixel_map_build_grid(map, 4, 4, 1.0f, 0, 16) == ESP_OK, "build 4x4");
    pm_pixel_map_normalize_uniform(map);
    expect_eq_sz(pm_pixel_map_count(map), 16, "16 mapped pixels");

    virt_strip_t strip = {.count = 16, .rgb = calloc(16 * 3, 1)};
    expect_true(strip.rgb != NULL, "alloc strip");

    pm_effect_context_t ctx = {
        .map = map,
        .params = base_params(PM_EFFECT_SOLID),
        .time_ms = 0,
        .pov_enabled = false,
        .strip_len = 16,
    };
    pm_effect_render(&ctx, set_px, &strip);

    /* HSV(0,255,255) → RGB(255,0,0); linear encode GRB → 00 FF 00 */
    bool solid_ok = true;
    for (uint16_t i = 0; i < 16; ++i) {
        if (strip.rgb[i * 3] != 255 || strip.rgb[i * 3 + 1] != 0 || strip.rgb[i * 3 + 2] != 0) {
            solid_ok = false;
            break;
        }
    }
    expect_true(solid_ok, "all pixels solid RGB(255,0,0)");

    uint8_t packed[16 * 3];
    pm_color_correction_t cc = linear_cc(255);
    size_t n = pm_led_encode_frame(strip.rgb, 16, 3, PM_COLOR_ORDER_GRB, &cc, packed, sizeof(packed));
    expect_eq_sz(n, 48, "encoded 48 bytes");
    expect_eq_u8(packed[0], 0, "GRB G");
    expect_eq_u8(packed[1], 255, "GRB R");
    expect_eq_u8(packed[2], 0, "GRB B");

    free(strip.rgb);
    pm_pixel_map_destroy(map);
}

static void test_color_order_differs(void)
{
    printf("scenario: color order RGB vs GRB\n");
    uint8_t px[3] = {255, 0, 0};
    uint8_t out_rgb[3], out_grb[3];
    pm_color_correction_t cc = linear_cc(255);
    size_t n1 = pm_led_encode_frame(px, 1, 3, PM_COLOR_ORDER_RGB, &cc, out_rgb, 3);
    size_t n2 = pm_led_encode_frame(px, 1, 3, PM_COLOR_ORDER_GRB, &cc, out_grb, 3);
    expect_eq_sz(n1, 3, "RGB len");
    expect_eq_sz(n2, 3, "GRB len");
    expect_true(memcmp(out_rgb, out_grb, 3) != 0, "RGB pack != GRB pack");
    expect_eq_u8(out_rgb[0], 255, "RGB[0]=R");
    expect_eq_u8(out_grb[0], 0, "GRB[0]=G");
}

static void test_checkerboard_spatial(void)
{
    printf("scenario: checkerboard spatial variation\n");
    pm_pixel_map_t *map = NULL;
    pm_pixel_map_config_t mc = {.capacity = 64, .width = 1, .height = 1, .depth = 1};
    expect_true(pm_pixel_map_create(&mc, &map) == ESP_OK, "create map");
    expect_true(pm_pixel_map_build_grid(map, 8, 8, 1.0f, 0, 64) == ESP_OK, "build 8x8");
    pm_pixel_map_normalize_uniform(map);

    virt_strip_t strip = {.count = 64, .rgb = calloc(64 * 3, 1)};
    expect_true(strip.rgb != NULL, "alloc strip");

    pm_effect_context_t ctx = {
        .map = map,
        .params = base_params(PM_EFFECT_CHECKERBOARD),
        .time_ms = 0,
        .pov_enabled = false,
        .strip_len = 64,
    };
    pm_effect_render(&ctx, set_px, &strip);

    bool any_diff = false;
    for (uint16_t i = 1; i < 64; ++i) {
        if (memcmp(&strip.rgb[0], &strip.rgb[i * 3], 3) != 0) {
            any_diff = true;
            break;
        }
    }
    expect_true(any_diff, "checkerboard not flat");

    free(strip.rgb);
    pm_pixel_map_destroy(map);
}

static void test_rainbow_deterministic(void)
{
    printf("scenario: rainbow spatial deterministic\n");
    pm_pixel_map_t *map = NULL;
    pm_pixel_map_config_t mc = {.capacity = 8, .width = 1, .height = 1, .depth = 1};
    expect_true(pm_pixel_map_create(&mc, &map) == ESP_OK, "create map");
    expect_true(pm_pixel_map_build_ring(map, 8, 1.0f, 0.0f, 0) == ESP_OK, "build ring");
    pm_pixel_map_normalize_uniform(map);

    virt_strip_t a = {.count = 8, .rgb = calloc(8 * 3, 1)};
    virt_strip_t b = {.count = 8, .rgb = calloc(8 * 3, 1)};
    expect_true(a.rgb && b.rgb, "alloc strips");

    pm_effect_context_t ctx = {
        .map = map,
        .params = base_params(PM_EFFECT_RAINBOW_SPATIAL),
        .time_ms = 1234,
        .pov_enabled = false,
        .strip_len = 8,
    };
    pm_effect_render(&ctx, set_px, &a);
    pm_effect_render(&ctx, set_px, &b);
    expect_true(memcmp(a.rgb, b.rgb, 8 * 3) == 0, "same t → same frame");

    bool any_diff = false;
    for (uint16_t i = 1; i < 8; ++i) {
        if (memcmp(&a.rgb[0], &a.rgb[i * 3], 3) != 0) {
            any_diff = true;
            break;
        }
    }
    expect_true(any_diff, "rainbow varies along ring");

    uint8_t packed[24];
    pm_color_correction_t cc = linear_cc(128);
    size_t n = pm_led_encode_frame(a.rgb, 8, 3, PM_COLOR_ORDER_GRB, &cc, packed, sizeof(packed));
    expect_eq_sz(n, 24, "encoded ring frame");

    free(a.rgb);
    free(b.rgb);
    pm_pixel_map_destroy(map);
}

static void test_pov_positions_move(void)
{
    printf("scenario: POV world positions move with time\n");
    pm_pov_params_t pov;
    pm_pov_params_set_defaults(&pov);
    pov.mode = PM_POV_ROTATION;
    pov.blade_count = 1;
    pov.rpm = 600.0f;
    pov.radius_m = 0.25f;

    pm_vec3_t p0 = pm_pov_world_pos(&pov, 0, 10, 0);
    pm_vec3_t p1 = pm_pov_world_pos(&pov, 0, 10, 100);
    expect_true(p0.x != p1.x || p0.y != p1.y, "POV angle advances");
}

static void test_circle_map_rings(void)
{
    printf("scenario: circle rings map non-empty\n");
    pm_pixel_map_t *map = NULL;
    pm_pixel_map_config_t mc = {.capacity = 128, .width = 1, .height = 1, .depth = 1};
    expect_true(pm_pixel_map_create(&mc, &map) == ESP_OK, "create map");
    expect_true(pm_pixel_map_build_circle(map, 7, 1.0f, 0, 128, 0) == ESP_OK, "circle rings");
    expect_true(pm_pixel_map_count(map) > 0, "circle has points");
    pm_pixel_map_destroy(map);
}

int main(void)
{
    printf("PixelMap virtbench\n");
    test_solid_grb_pack();
    test_color_order_differs();
    test_checkerboard_spatial();
    test_rainbow_deterministic();
    test_pov_positions_move();
    test_circle_map_rings();

    if (g_fails) {
        fprintf(stderr, "\n%d assertion(s) failed\n", g_fails);
        return 1;
    }
    printf("\nAll virtbench scenarios passed.\n");
    return 0;
}
