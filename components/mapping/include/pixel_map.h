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

/* Build helpers */
esp_err_t pm_pixel_map_build_line(pm_pixel_map_t *map, uint16_t count,
                                  pm_vec3_t a, pm_vec3_t b, uint8_t group);
esp_err_t pm_pixel_map_build_grid(pm_pixel_map_t *map, uint16_t w, uint16_t h,
                                  float spacing, uint8_t group);
esp_err_t pm_pixel_map_build_ring(pm_pixel_map_t *map, uint16_t count,
                                  float radius, float z, uint8_t group);

/* Normalize positions into 0..1 cube based on AABB */
void pm_pixel_map_normalize(pm_pixel_map_t *map);

/* JSON import/export (compact array of {i,x,y,z,g}) */
esp_err_t pm_pixel_map_import_json(pm_pixel_map_t *map, const char *json);
esp_err_t pm_pixel_map_export_json(const pm_pixel_map_t *map, char *buf, size_t buflen, size_t *out_len);

#ifdef __cplusplus
}
#endif
