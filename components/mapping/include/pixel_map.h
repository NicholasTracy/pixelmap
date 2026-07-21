#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float x, y, z;
} pm_vec3_t;

typedef struct {
    uint16_t index;     /* strip pixel index */
    pm_vec3_t pos;      /* world meters or normalized units */
    uint8_t group;      /* fixture / segment id */
} pm_mapped_pixel_t;

typedef struct pm_pixel_map pm_pixel_map_t;

typedef struct {
    uint16_t capacity;
    float width;
    float height;
    float depth;
} pm_pixel_map_config_t;

esp_err_t pm_pixel_map_create(const pm_pixel_map_config_t *cfg, pm_pixel_map_t **out);
void pm_pixel_map_destroy(pm_pixel_map_t *map);

esp_err_t pm_pixel_map_set(pm_pixel_map_t *map, uint16_t slot, const pm_mapped_pixel_t *px);
const pm_mapped_pixel_t *pm_pixel_map_get(const pm_pixel_map_t *map, uint16_t slot);
uint16_t pm_pixel_map_count(const pm_pixel_map_t *map);
uint16_t pm_pixel_map_capacity(const pm_pixel_map_t *map);

/** Grow map storage in place (pointer to map stays valid). */
esp_err_t pm_pixel_map_ensure_capacity(pm_pixel_map_t *map, uint16_t capacity);

/** Persist / restore map JSON via stdio path (e.g. /spiffs/map.json). */
esp_err_t pm_pixel_map_save_path(const pm_pixel_map_t *map, const char *path);
esp_err_t pm_pixel_map_load_path(pm_pixel_map_t *map, const char *path);

/* Build helpers. max_count = strip length (oversized lattices are stride-subsampled
 * so full dimensions are kept; unused strip pixels stay dark). */
esp_err_t pm_pixel_map_build_line(pm_pixel_map_t *map, uint16_t count,
                                  pm_vec3_t a, pm_vec3_t b, uint8_t group);
esp_err_t pm_pixel_map_build_grid(pm_pixel_map_t *map, uint16_t w, uint16_t h,
                                  float spacing, uint8_t group, uint16_t max_count);
esp_err_t pm_pixel_map_build_grid3d(pm_pixel_map_t *map, uint16_t w, uint16_t h, uint16_t d,
                                    float spacing, uint8_t group, uint16_t max_count);
esp_err_t pm_pixel_map_build_ring(pm_pixel_map_t *map, uint16_t count,
                                  float radius, float z, uint8_t group);

/* Circle: diam_px = LEDs across diameter. fill 0=rings, 1=disk lattice */
esp_err_t pm_pixel_map_build_circle(pm_pixel_map_t *map, uint16_t diam_px,
                                    float spacing, uint8_t group, uint16_t max_count,
                                    uint8_t fill_mode);
/* Sphere: diam_px = LEDs across diameter. fill 0=shell, 1=solid lattice */
esp_err_t pm_pixel_map_build_sphere(pm_pixel_map_t *map, uint16_t diam_px,
                                    float spacing, uint8_t group, uint16_t max_count,
                                    uint8_t fill_mode);
/* Box: W×H×D in LEDs. fill 0=faces, 1=solid. open_tb only for faces. */
esp_err_t pm_pixel_map_build_box(pm_pixel_map_t *map, uint16_t w, uint16_t h, uint16_t d,
                                 float spacing, uint8_t group, uint16_t max_count,
                                 uint8_t fill_mode, bool open_tb);
/* Cylinder: diam_px across, height_px along Y (ring layers). */
esp_err_t pm_pixel_map_build_cylinder(pm_pixel_map_t *map, uint16_t diam_px, uint16_t height_px,
                                      float spacing, uint8_t group, uint16_t max_count,
                                      bool open_tb);
/* Dome: diam_px across hemisphere. */
esp_err_t pm_pixel_map_build_dome(pm_pixel_map_t *map, uint16_t diam_px,
                                  float spacing, uint8_t group, uint16_t max_count);
/* Pyramid: base_px along base edge, height_px vertical steps. */
esp_err_t pm_pixel_map_build_pyramid(pm_pixel_map_t *map, uint16_t base_px, uint16_t height_px,
                                     float spacing, uint8_t group, uint16_t max_count);

/* Normalize positions into 0..1 cube based on AABB (per-axis) */
void pm_pixel_map_normalize(pm_pixel_map_t *map);
/* Uniform scale — preserves aspect / relative spacing */
void pm_pixel_map_normalize_uniform(pm_pixel_map_t *map);

/* JSON import/export (compact array of {i,x,y,z,g}) */
esp_err_t pm_pixel_map_import_json(pm_pixel_map_t *map, const char *json);
esp_err_t pm_pixel_map_export_json(const pm_pixel_map_t *map, char *buf, size_t buflen, size_t *out_len);

#ifdef __cplusplus
}
#endif
