#pragma once

#include "led_chipsets.h"
#include "effects.h"
#include "pov.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_WIFI_SSID_MAX 32
#define PM_WIFI_PASS_MAX 64
#define PM_STRIP_MAX 8

typedef enum {
    PM_MAP_DIM_2D = 0,
    PM_MAP_DIM_3D = 1,
} pm_map_dim_t;

typedef enum {
    PM_MAP_LAYOUT_GRID = 0,
    PM_MAP_LAYOUT_CIRCLE = 1,    /* 2D disk: rings or filled lattice */
    PM_MAP_LAYOUT_SPHERE = 2,    /* 3D ball: shell or solid lattice */
    PM_MAP_LAYOUT_BOX = 3,       /* 3D box: face shell or solid lattice */
    PM_MAP_LAYOUT_CYLINDER = 4,  /* cylinder wall (+ optional caps) */
    PM_MAP_LAYOUT_DOME = 5,      /* hemisphere shell */
    PM_MAP_LAYOUT_PYRAMID = 6,   /* square pyramid shell */
    PM_MAP_LAYOUT_CUSTOM = 7,     /* formula / imported point list */
} pm_map_layout_t;

typedef enum {
    /* Circle: rings. Sphere/box/cyl/dome/pyramid: outer surface only. */
    PM_MAP_FILL_SURFACE = 0,
    /* Circle: filled disk grid. Others: solid / volumetric lattice. */
    PM_MAP_FILL_SOLID = 1,
} pm_map_fill_t;

/* Legacy aliases (circle UI) */
#define PM_MAP_FILL_RINGS PM_MAP_FILL_SURFACE
#define PM_MAP_FILL_GRID  PM_MAP_FILL_SOLID

/** How Art-Net / sACN data is interpreted when a stream is active. */
typedef enum {
    PM_DMX_MODE_PIXELS = 0, /* RGB bytes → strip (overrides local effects) */
    PM_DMX_MODE_PARAMS = 1, /* bound channels → effect sliders; effects still render */
} pm_dmx_mode_t;

/** Built-in LFO shapes for per-slider control modulators (Captivate-style). */
typedef enum {
    PM_FXMOD_OFF = 0,
    PM_FXMOD_SINE,
    PM_FXMOD_RAMP,
    PM_FXMOD_SQUARE,
    PM_FXMOD_SAW,
    PM_FXMOD_NOISE,
} pm_fxmod_shape_t;

/** Per-parameter modulator (packed for NVS blob). */
typedef struct {
    uint8_t shape; /* pm_fxmod_shape_t */
    uint8_t depth; /* 0..255 → bipolar depth around base */
    uint8_t rate;  /* 0..255 → ~0.05..8 Hz */
    uint8_t phase; /* 0..255 → phase offset */
} pm_fxmod_t;

typedef struct {
    char sta_ssid[PM_WIFI_SSID_MAX];
    char sta_pass[PM_WIFI_PASS_MAX];
    char hostname[32];
    bool ap_fallback;

    int gpio_data;                  /* alias of strip_gpio[0] (legacy / single-strip) */
    int gpio_clock;
    int gpio_status_led;      /* WLED-style onboard status LED; <0 disables */
    bool status_led_active_high;
    pm_chipset_t chipset;
    pm_color_order_t color_order;
    uint16_t pixel_count;           /* total LEDs = sum of strip_len[0..strip_count) */
    uint8_t strip_count;            /* 1..PM_STRIP_MAX separate data GPIOs */
    uint16_t strip_len[PM_STRIP_MAX];
    int strip_gpio[PM_STRIP_MAX];   /* data pin per strip (WLED-style multi-bus) */
    uint8_t brightness;
    uint8_t gamma;
    bool auto_white;

    pm_effect_id_t effect_id;
    float effect_speed;
    float effect_scale;
    uint8_t effect_intensity; /* 0-255 */
    uint8_t effect_primary_h, effect_primary_s, effect_primary_v;
    uint8_t effect_secondary_h, effect_secondary_s, effect_secondary_v;
    uint8_t effect_p[PM_FX_P_COUNT]; /* 0-255 generic knobs */
    uint8_t effect_pos[3]; /* 0-255 → −0.5..+0.5 (128 = center) */
    uint8_t effect_rot[3]; /* 0-255 → 0..360° */
    /** 1-based absolute DMX channel in the merge buffer; 0 = unbound */
    uint16_t effect_param_ch[PM_FXPARAM_COUNT];
    pm_fxmod_t effect_mod[PM_FXPARAM_COUNT];
    pm_dmx_mode_t dmx_mode;

    uint16_t artnet_universe;
    uint16_t sacn_universe;
    uint16_t universe_count;
    bool artnet_enable;
    bool sacn_enable;

    uint16_t map_width;
    uint16_t map_height;
    uint16_t map_depth;
    pm_map_dim_t map_dim;
    pm_map_layout_t map_layout;
    pm_map_fill_t map_fill; /* surface/shell vs solid; circle: rings vs filled grid */
    bool map_open_tb;       /* box/cylinder: omit top & bottom faces/caps */
    float map_spacing; /* distance between adjacent lattice neighbors */

    /* Persistence-of-vision mapping (fan / wand) */
    bool pov_enable;
    pm_pov_mode_t pov_mode;
    pm_pov_layout_t pov_layout;
    uint8_t pov_blade_count;    /* 1..10 evenly spaced blades */
    float pov_rpm;              /* fixed rotation speed for now */
    float pov_linear_speed_mps; /* wand sweep speed */
    float pov_radius_m;
    float pov_path_length_m;
} pm_app_config_t;

void pm_config_set_defaults(pm_app_config_t *cfg);
/** WLED-friendly default data GPIO for strip index (0-based). */
int pm_config_default_strip_gpio(uint8_t strip_index);
/** Clamp strip_count / lengths / GPIOs; pixel_count = sum(strip_len); gpio_data = strip_gpio[0]. */
void pm_config_sync_strips(pm_app_config_t *cfg);
/** Fill runtime effect params from config (no DMX overlay). */
void pm_config_fill_effect_params(const pm_app_config_t *cfg, pm_effect_params_t *out);
/**
 * Overlay bound DMX channels onto a mutable copy of cfg effect fields.
 * Channels are 1-based indexes into merge[0..merge_len).
 */
void pm_config_apply_fx_dmx(pm_app_config_t *cfg, const uint8_t *merge, size_t merge_len);
/**
 * Apply per-parameter LFOs onto effect fields (after DMX overlay).
 * Center-anchored: output = base ± depth * half_range * bipolar(wave).
 */
void pm_config_apply_fx_mods(pm_app_config_t *cfg, uint32_t time_ms);
esp_err_t pm_config_load(pm_app_config_t *cfg);
esp_err_t pm_config_save(const pm_app_config_t *cfg);

#ifdef __cplusplus
}
#endif
