#include "pixel_map.h"
#include "cJSON.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct pm_pixel_map {
    pm_mapped_pixel_t *pixels;
    uint16_t capacity;
    uint16_t count;
    float width, height, depth;
};

esp_err_t pm_pixel_map_create(const pm_pixel_map_config_t *cfg, pm_pixel_map_t **out)
{
    if (!cfg || !out || cfg->capacity == 0) return ESP_ERR_INVALID_ARG;
    pm_pixel_map_t *m = calloc(1, sizeof(*m));
    if (!m) return ESP_ERR_NO_MEM;
    m->pixels = calloc(cfg->capacity, sizeof(pm_mapped_pixel_t));
    if (!m->pixels) {
        free(m);
        return ESP_ERR_NO_MEM;
    }
    m->capacity = cfg->capacity;
    m->width = cfg->width > 0 ? cfg->width : 1.0f;
    m->height = cfg->height > 0 ? cfg->height : 1.0f;
    m->depth = cfg->depth > 0 ? cfg->depth : 1.0f;
    *out = m;
    return ESP_OK;
}

void pm_pixel_map_destroy(pm_pixel_map_t *map)
{
    if (!map) return;
    free(map->pixels);
    free(map);
}

esp_err_t pm_pixel_map_set(pm_pixel_map_t *map, uint16_t slot, const pm_mapped_pixel_t *px)
{
    if (!map || !px || slot >= map->capacity) return ESP_ERR_INVALID_ARG;
    map->pixels[slot] = *px;
    if (slot + 1 > map->count) map->count = slot + 1;
    return ESP_OK;
}

const pm_mapped_pixel_t *pm_pixel_map_get(const pm_pixel_map_t *map, uint16_t slot)
{
    if (!map || slot >= map->count) return NULL;
    return &map->pixels[slot];
}

uint16_t pm_pixel_map_count(const pm_pixel_map_t *map)
{
    return map ? map->count : 0;
}

esp_err_t pm_pixel_map_build_line(pm_pixel_map_t *map, uint16_t count,
                                  pm_vec3_t a, pm_vec3_t b, uint8_t group)
{
    if (!map || count == 0 || count > map->capacity) return ESP_ERR_INVALID_ARG;
    for (uint16_t i = 0; i < count; ++i) {
        float t = count == 1 ? 0.0f : (float)i / (float)(count - 1);
        pm_mapped_pixel_t px = {
            .index = i,
            .pos = {
                a.x + (b.x - a.x) * t,
                a.y + (b.y - a.y) * t,
                a.z + (b.z - a.z) * t,
            },
            .group = group,
        };
        map->pixels[i] = px;
    }
    map->count = count;
    return ESP_OK;
}

esp_err_t pm_pixel_map_build_grid(pm_pixel_map_t *map, uint16_t w, uint16_t h,
                                  float spacing, uint8_t group)
{
    if (!map || w == 0 || h == 0) return ESP_ERR_INVALID_ARG;
    uint32_t n = (uint32_t)w * h;
    if (n > map->capacity) return ESP_ERR_INVALID_SIZE;
    uint16_t i = 0;
    for (uint16_t y = 0; y < h; ++y) {
        for (uint16_t x = 0; x < w; ++x) {
            map->pixels[i] = (pm_mapped_pixel_t){
                .index = i,
                .pos = {x * spacing, y * spacing, 0.0f},
                .group = group,
            };
            ++i;
        }
    }
    map->count = i;
    return ESP_OK;
}

esp_err_t pm_pixel_map_build_ring(pm_pixel_map_t *map, uint16_t count,
                                  float radius, float z, uint8_t group)
{
    if (!map || count == 0 || count > map->capacity) return ESP_ERR_INVALID_ARG;
    for (uint16_t i = 0; i < count; ++i) {
        float ang = (float)(2.0 * M_PI * i / count);
        map->pixels[i] = (pm_mapped_pixel_t){
            .index = i,
            .pos = {cosf(ang) * radius, sinf(ang) * radius, z},
            .group = group,
        };
    }
    map->count = count;
    return ESP_OK;
}

void pm_pixel_map_normalize(pm_pixel_map_t *map)
{
    if (!map || map->count == 0) return;
    float minx = map->pixels[0].pos.x, maxx = minx;
    float miny = map->pixels[0].pos.y, maxy = miny;
    float minz = map->pixels[0].pos.z, maxz = minz;
    for (uint16_t i = 1; i < map->count; ++i) {
        pm_vec3_t p = map->pixels[i].pos;
        if (p.x < minx) minx = p.x; if (p.x > maxx) maxx = p.x;
        if (p.y < miny) miny = p.y; if (p.y > maxy) maxy = p.y;
        if (p.z < minz) minz = p.z; if (p.z > maxz) maxz = p.z;
    }
    float dx = maxx - minx; if (dx < 1e-6f) dx = 1.0f;
    float dy = maxy - miny; if (dy < 1e-6f) dy = 1.0f;
    float dz = maxz - minz; if (dz < 1e-6f) dz = 1.0f;
    for (uint16_t i = 0; i < map->count; ++i) {
        map->pixels[i].pos.x = (map->pixels[i].pos.x - minx) / dx;
        map->pixels[i].pos.y = (map->pixels[i].pos.y - miny) / dy;
        map->pixels[i].pos.z = (map->pixels[i].pos.z - minz) / dz;
    }
    map->width = map->height = map->depth = 1.0f;
}

esp_err_t pm_pixel_map_import_json(pm_pixel_map_t *map, const char *json)
{
    if (!map || !json) return ESP_ERR_INVALID_ARG;
    cJSON *root = cJSON_Parse(json);
    if (!root || !cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    int n = cJSON_GetArraySize(root);
    if (n > map->capacity) n = map->capacity;
    for (int i = 0; i < n; ++i) {
        cJSON *item = cJSON_GetArrayItem(root, i);
        pm_mapped_pixel_t px = {0};
        px.index = (uint16_t)(cJSON_GetObjectItem(item, "i") ? cJSON_GetObjectItem(item, "i")->valuedouble : i);
        px.pos.x = (float)(cJSON_GetObjectItem(item, "x") ? cJSON_GetObjectItem(item, "x")->valuedouble : 0);
        px.pos.y = (float)(cJSON_GetObjectItem(item, "y") ? cJSON_GetObjectItem(item, "y")->valuedouble : 0);
        px.pos.z = (float)(cJSON_GetObjectItem(item, "z") ? cJSON_GetObjectItem(item, "z")->valuedouble : 0);
        px.group = (uint8_t)(cJSON_GetObjectItem(item, "g") ? cJSON_GetObjectItem(item, "g")->valuedouble : 0);
        map->pixels[i] = px;
    }
    map->count = (uint16_t)n;
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t pm_pixel_map_export_json(const pm_pixel_map_t *map, char *buf, size_t buflen, size_t *out_len)
{
    if (!map || !buf || buflen < 3) return ESP_ERR_INVALID_ARG;
    cJSON *root = cJSON_CreateArray();
    if (!root) return ESP_ERR_NO_MEM;
    for (uint16_t i = 0; i < map->count; ++i) {
        const pm_mapped_pixel_t *px = &map->pixels[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "i", px->index);
        cJSON_AddNumberToObject(o, "x", px->pos.x);
        cJSON_AddNumberToObject(o, "y", px->pos.y);
        cJSON_AddNumberToObject(o, "z", px->pos.z);
        cJSON_AddNumberToObject(o, "g", px->group);
        cJSON_AddItemToArray(root, o);
    }
    char *printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) return ESP_ERR_NO_MEM;
    size_t len = strlen(printed);
    if (len + 1 > buflen) {
        free(printed);
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(buf, printed, len + 1);
    free(printed);
    if (out_len) *out_len = len;
    return ESP_OK;
}
