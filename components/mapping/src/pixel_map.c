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

static void ensure_grid_corners(pm_pixel_map_t *map, uint16_t *count, uint16_t limit,
                                uint16_t w, uint16_t h, uint16_t d, float spacing, uint8_t group);

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
                                  float spacing, uint8_t group, uint16_t max_count)
{
    if (!map || w == 0 || h == 0 || max_count == 0) return ESP_ERR_INVALID_ARG;
    if (spacing < 1e-6f) spacing = 1.0f;
    uint16_t limit = max_count < map->capacity ? max_count : map->capacity;
    /* Stride-sample when the lattice exceeds the strip so W×H extent is kept
     * instead of filling only the first rows in scan order. */
    uint32_t total = (uint32_t)w * (uint32_t)h;
    uint32_t stride = total > limit ? (total + limit - 1) / limit : 1;
    uint16_t i = 0;
    uint32_t seen = 0;
    for (uint16_t y = 0; y < h && i < limit; ++y) {
        for (uint16_t x = 0; x < w && i < limit; ++x) {
            if ((seen++ % stride) != 0) continue;
            map->pixels[i] = (pm_mapped_pixel_t){
                .index = i,
                .pos = {x * spacing, y * spacing, 0.0f},
                .group = group,
            };
            ++i;
        }
    }
    ensure_grid_corners(map, &i, limit, w, h, 1, spacing, group);
    map->count = i;
    return ESP_OK;
}

esp_err_t pm_pixel_map_build_grid3d(pm_pixel_map_t *map, uint16_t w, uint16_t h, uint16_t d,
                                    float spacing, uint8_t group, uint16_t max_count)
{
    if (!map || w == 0 || h == 0 || d == 0 || max_count == 0) return ESP_ERR_INVALID_ARG;
    if (spacing < 1e-6f) spacing = 1.0f;
    uint16_t limit = max_count < map->capacity ? max_count : map->capacity;
    /* Stride-sample so a short strip still spans the full W×H×D box. */
    uint32_t total = (uint32_t)w * (uint32_t)h * (uint32_t)d;
    uint32_t stride = total > limit ? (total + limit - 1) / limit : 1;
    uint16_t i = 0;
    uint32_t seen = 0;
    for (uint16_t z = 0; z < d && i < limit; ++z) {
        for (uint16_t y = 0; y < h && i < limit; ++y) {
            for (uint16_t x = 0; x < w && i < limit; ++x) {
                if ((seen++ % stride) != 0) continue;
                map->pixels[i] = (pm_mapped_pixel_t){
                    .index = i,
                    .pos = {x * spacing, y * spacing, z * spacing},
                    .group = group,
                };
                ++i;
            }
        }
    }
    ensure_grid_corners(map, &i, limit, w, h, d, spacing, group);
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

static uint16_t count_disk_lattice(float radius, float spacing)
{
    if (radius < 0.0f) return 0;
    int n = (int)ceilf(radius / spacing + 1e-4f);
    uint16_t c = 0;
    float r2 = radius * radius + 1e-4f;
    for (int yi = -n; yi <= n; ++yi) {
        for (int xi = -n; xi <= n; ++xi) {
            float x = (float)xi * spacing;
            float y = (float)yi * spacing;
            if (x * x + y * y <= r2) ++c;
        }
    }
    return c;
}

static uint16_t count_ball_lattice(float radius, float spacing)
{
    if (radius < 0.0f) return 0;
    int n = (int)ceilf(radius / spacing + 1e-4f);
    uint16_t c = 0;
    float r2 = radius * radius + 1e-4f;
    for (int zi = -n; zi <= n; ++zi) {
        for (int yi = -n; yi <= n; ++yi) {
            for (int xi = -n; xi <= n; ++xi) {
                float x = (float)xi * spacing;
                float y = (float)yi * spacing;
                float z = (float)zi * spacing;
                if (x * x + y * y + z * z <= r2) ++c;
            }
        }
    }
    return c;
}

static bool push_map_px(pm_pixel_map_t *map, uint16_t *i, uint16_t max_count,
                        float x, float y, float z, uint8_t group)
{
    if (*i >= max_count || *i >= map->capacity) return false;
    map->pixels[*i] = (pm_mapped_pixel_t){
        .index = *i,
        .pos = {x, y, z},
        .group = group,
    };
    (*i)++;
    return true;
}

static bool pos_is_corner(float x, float y, float z, uint16_t w, uint16_t h, uint16_t d, float spacing)
{
    const float eps = 1e-3f;
    float x1 = (float)(w - 1) * spacing, y1 = (float)(h - 1) * spacing, z1 = (float)(d - 1) * spacing;
    bool xe = fabsf(x) <= eps || fabsf(x - x1) <= eps;
    bool ye = fabsf(y) <= eps || fabsf(y - y1) <= eps;
    bool ze = (d <= 1) ? true : (fabsf(z) <= eps || fabsf(z - z1) <= eps);
    return xe && ye && ze;
}

/** Make sure subsampled lattices still include AABB corners (full extent). */
static void ensure_grid_corners(pm_pixel_map_t *map, uint16_t *count, uint16_t limit,
                                uint16_t w, uint16_t h, uint16_t d, float spacing, uint8_t group)
{
    if (!map || !count || w == 0 || h == 0 || d == 0 || limit == 0) return;
    uint16_t n = *count;
    uint16_t xs[2] = {0, (uint16_t)(w - 1)};
    uint16_t ys[2] = {0, (uint16_t)(h - 1)};
    uint16_t zs[2] = {0, (uint16_t)(d - 1)};
    for (int zi = 0; zi < (d > 1 ? 2 : 1); ++zi) {
        for (int yi = 0; yi < 2; ++yi) {
            for (int xi = 0; xi < 2; ++xi) {
                float x = (float)xs[xi] * spacing;
                float y = (float)ys[yi] * spacing;
                float z = (float)zs[zi] * spacing;
                bool have = false;
                for (uint16_t k = 0; k < n; ++k) {
                    const pm_vec3_t *p = &map->pixels[k].pos;
                    if (fabsf(p->x - x) <= 1e-3f && fabsf(p->y - y) <= 1e-3f
                        && fabsf(p->z - z) <= 1e-3f) {
                        have = true;
                        break;
                    }
                }
                if (have) continue;
                uint16_t slot = UINT16_MAX;
                if (n < limit && n < map->capacity) {
                    slot = n++;
                } else {
                    /* Steal a non-corner sample so later faces are not wiped. */
                    for (uint16_t k = n; k > 0; --k) {
                        const pm_vec3_t *p = &map->pixels[k - 1].pos;
                        if (!pos_is_corner(p->x, p->y, p->z, w, h, d, spacing)) {
                            slot = (uint16_t)(k - 1);
                            break;
                        }
                    }
                }
                if (slot == UINT16_MAX) continue;
                map->pixels[slot] = (pm_mapped_pixel_t){
                    .index = slot,
                    .pos = {x, y, z},
                    .group = group,
                };
            }
        }
    }
    *count = n;
}

static float diam_to_radius(uint16_t diam_px, float spacing)
{
    if (diam_px < 2) return 0.0f;
    return 0.5f * (float)(diam_px - 1) * spacing;
}

static uint16_t shell_count_for_radius(float radius, float spacing, float area_scale)
{
    if (radius < 1e-6f || spacing < 1e-6f) return 1;
    float n = area_scale * (radius / spacing) * (radius / spacing);
    if (n < 1.0f) n = 1.0f;
    if (n > 65535.0f) n = 65535.0f;
    return (uint16_t)(n + 0.5f);
}

esp_err_t pm_pixel_map_build_circle(pm_pixel_map_t *map, uint16_t diam_px,
                                    float spacing, uint8_t group, uint16_t max_count,
                                    uint8_t fill_mode)
{
    if (!map || max_count == 0) return ESP_ERR_INVALID_ARG;
    if (max_count > map->capacity) max_count = map->capacity;
    if (spacing < 1e-6f) spacing = 1.0f;
    if (diam_px < 1) diam_px = 1;

    float radius = diam_to_radius(diam_px, spacing);
    uint16_t i = 0;

    if (fill_mode == 1) {
        int n = (int)ceilf(radius / spacing + 1e-4f);
        float r2 = radius * radius + 1e-4f;
        uint16_t total = count_disk_lattice(radius, spacing);
        uint32_t stride = total > max_count ? (total + max_count - 1) / max_count : 1;
        uint32_t seen = 0;
        for (int yi = -n; yi <= n && i < max_count; ++yi) {
            for (int xi = -n; xi <= n && i < max_count; ++xi) {
                float x = (float)xi * spacing;
                float y = (float)yi * spacing;
                if (x * x + y * y > r2) continue;
                if ((seen++ % stride) != 0) continue;
                if (!push_map_px(map, &i, max_count, x, y, 0, group)) break;
            }
        }
    } else {
        /* Concentric rings out to diameter radius; skip incomplete outer rings */
        if (!push_map_px(map, &i, max_count, 0, 0, 0, group)) {
            map->count = i;
            return ESP_OK;
        }
        uint16_t rings = 0;
        if (radius > 1e-6f && spacing > 1e-6f) {
            rings = (uint16_t)floorf(radius / spacing + 1e-4f);
        }
        for (uint16_t ring = 1; ring <= rings && i < max_count; ++ring) {
            float r = (float)ring * spacing;
            uint16_t on = (uint16_t)(2.0f * (float)M_PI * r / spacing + 0.5f);
            if (on < 6) on = 6;
            if (on > max_count - i) break; /* keep rings complete */
            for (uint16_t k = 0; k < on; ++k) {
                float ang = (float)(2.0 * M_PI * k / on);
                if (!push_map_px(map, &i, max_count, cosf(ang) * r, sinf(ang) * r, 0, group)) break;
            }
        }
    }
    map->count = i;
    return ESP_OK;
}

esp_err_t pm_pixel_map_build_sphere(pm_pixel_map_t *map, uint16_t diam_px,
                                    float spacing, uint8_t group, uint16_t max_count,
                                    uint8_t fill_mode)
{
    if (!map || max_count == 0) return ESP_ERR_INVALID_ARG;
    if (max_count > map->capacity) max_count = map->capacity;
    if (spacing < 1e-6f) spacing = 1.0f;
    if (diam_px < 1) diam_px = 1;

    float radius = diam_to_radius(diam_px, spacing);
    if (radius < 1e-6f) radius = spacing;
    uint16_t i = 0;

    if (fill_mode == 1) {
        int n = (int)ceilf(radius / spacing + 1e-4f);
        float r2 = radius * radius + 1e-4f;
        uint16_t total = count_ball_lattice(radius, spacing);
        uint32_t stride = total > max_count ? (total + max_count - 1) / max_count : 1;
        uint32_t seen = 0;
        for (int zi = -n; zi <= n && i < max_count; ++zi) {
            for (int yi = -n; yi <= n && i < max_count; ++yi) {
                for (int xi = -n; xi <= n && i < max_count; ++xi) {
                    float x = (float)xi * spacing;
                    float y = (float)yi * spacing;
                    float z = (float)zi * spacing;
                    if (x * x + y * y + z * z > r2) continue;
                    if ((seen++ % stride) != 0) continue;
                    if (!push_map_px(map, &i, max_count, x, y, z, group)) break;
                }
            }
        }
    } else {
        uint16_t want = shell_count_for_radius(radius, spacing, 4.0f * (float)M_PI);
        if (want > max_count) want = max_count;
        const float ga = (float)M_PI * (3.0f - sqrtf(5.0f));
        for (uint16_t k = 0; k < want; ++k) {
            float t = (want <= 1) ? 0.0f : ((float)k + 0.5f) / (float)want;
            float y = 1.0f - 2.0f * t;
            float r_xz = sqrtf(fmaxf(0.0f, 1.0f - y * y));
            float th = ga * (float)k;
            if (!push_map_px(map, &i, max_count,
                             cosf(th) * r_xz * radius, y * radius, sinf(th) * r_xz * radius,
                             group)) break;
        }
    }
    map->count = i;
    return ESP_OK;
}

/** Partition shell voxel to one face: z0,z1,y0,y1,x0,x1. -1 = not on shell. */
static int box_shell_face(uint16_t x, uint16_t y, uint16_t z,
                          uint16_t w, uint16_t h, uint16_t d, bool open_tb)
{
    if (z == 0) return 0;
    if (d > 1 && z == d - 1) return 1;
    if (!open_tb && y == 0) return 2;
    if (!open_tb && h > 1 && y == h - 1) return 3;
    if (x == 0) return 4;
    if (w > 1 && x == w - 1) return 5;
    return -1;
}

esp_err_t pm_pixel_map_build_box(pm_pixel_map_t *map, uint16_t w, uint16_t h, uint16_t d,
                                 float spacing, uint8_t group, uint16_t max_count,
                                 uint8_t fill_mode, bool open_tb)
{
    if (!map || w == 0 || h == 0 || d == 0 || max_count == 0) return ESP_ERR_INVALID_ARG;
    if (spacing < 1e-6f) spacing = 1.0f;
    if (max_count > map->capacity) max_count = map->capacity;

    if (fill_mode == 1) {
        return pm_pixel_map_build_grid3d(map, w, h, d, spacing, group, max_count);
    }

    /* Corners first (guarantees full W×H×D extent), then per-face budgets. */
    uint16_t i = 0;
    uint16_t xs_c[2] = {0, (uint16_t)(w > 1 ? w - 1 : 0)};
    uint16_t ys_c[2] = {0, (uint16_t)(h > 1 ? h - 1 : 0)};
    uint16_t zs_c[2] = {0, (uint16_t)(d > 1 ? d - 1 : 0)};
    int nx = w > 1 ? 2 : 1, ny = h > 1 ? 2 : 1, nz = d > 1 ? 2 : 1;
    for (int zi = 0; zi < nz && i < max_count; ++zi) {
        for (int yi = 0; yi < ny && i < max_count; ++yi) {
            for (int xi = 0; xi < nx && i < max_count; ++xi) {
                if (!push_map_px(map, &i, max_count,
                                 (float)xs_c[xi] * spacing, (float)ys_c[yi] * spacing,
                                 (float)zs_c[zi] * spacing, group)) {
                    map->count = i;
                    return ESP_OK;
                }
            }
        }
    }

    uint32_t fc[6] = {0};
    for (uint16_t zi = 0; zi < d; ++zi) {
        for (uint16_t yi = 0; yi < h; ++yi) {
            for (uint16_t xi = 0; xi < w; ++xi) {
                bool corner = (xi == 0 || xi == w - 1) && (yi == 0 || yi == h - 1)
                              && (d <= 1 || zi == 0 || zi == d - 1);
                if (corner) continue;
                int f = box_shell_face(xi, yi, zi, w, h, d, open_tb);
                if (f >= 0) fc[f]++;
            }
        }
    }
    uint32_t total = 0;
    uint16_t faces_nz = 0;
    for (int f = 0; f < 6; ++f) {
        total += fc[f];
        if (fc[f]) faces_nz++;
    }
    uint16_t budget = max_count > i ? (uint16_t)(max_count - i) : 0;
    if (budget == 0 || total == 0) {
        map->count = i;
        return ESP_OK;
    }

    uint16_t allot[6] = {0};
    if (budget >= faces_nz) {
        uint32_t assigned = 0;
        for (int f = 0; f < 6; ++f) {
            if (fc[f]) {
                allot[f] = 1;
                assigned++;
            }
        }
        uint32_t left = budget - assigned;
        uint32_t rem_cells = total > faces_nz ? total - (uint32_t)faces_nz : 0;
        if (left > 0 && rem_cells > 0) {
            uint32_t given = 0;
            for (int f = 0; f < 6; ++f) {
                if (fc[f] <= 1) continue;
                uint32_t extra = (uint32_t)((left * (uint64_t)(fc[f] - 1)) / rem_cells);
                if (allot[f] + extra > fc[f]) extra = fc[f] - allot[f];
                allot[f] = (uint16_t)(allot[f] + extra);
                given += extra;
            }
            for (int f = 0; f < 6 && given < left; ++f) {
                if (fc[f] > allot[f]) {
                    allot[f]++;
                    given++;
                }
            }
        }
    } else {
        uint32_t assigned = 0;
        for (int f = 0; f < 6 && assigned < budget; ++f) {
            if (fc[f]) {
                allot[f] = 1;
                assigned++;
            }
        }
    }

    uint32_t stride[6];
    for (int f = 0; f < 6; ++f) {
        if (allot[f] == 0 || fc[f] <= allot[f]) stride[f] = 1;
        else stride[f] = (fc[f] + allot[f] - 1) / allot[f];
    }

    uint32_t seen[6] = {0};
    uint16_t got[6] = {0};
    for (uint16_t zi = 0; zi < d && i < max_count; ++zi) {
        for (uint16_t yi = 0; yi < h && i < max_count; ++yi) {
            for (uint16_t xi = 0; xi < w && i < max_count; ++xi) {
                bool corner = (xi == 0 || xi == w - 1) && (yi == 0 || yi == h - 1)
                              && (d <= 1 || zi == 0 || zi == d - 1);
                if (corner) continue;
                int f = box_shell_face(xi, yi, zi, w, h, d, open_tb);
                if (f < 0 || got[f] >= allot[f]) continue;
                if ((seen[f]++ % stride[f]) != 0) continue;
                if (!push_map_px(map, &i, max_count,
                                 (float)xi * spacing, (float)yi * spacing, (float)zi * spacing,
                                 group)) {
                    map->count = i;
                    return ESP_OK;
                }
                got[f]++;
            }
        }
    }
    map->count = i;
    return ESP_OK;
}

esp_err_t pm_pixel_map_build_cylinder(pm_pixel_map_t *map, uint16_t diam_px, uint16_t height_px,
                                      float spacing, uint8_t group, uint16_t max_count,
                                      bool open_tb)
{
    if (!map || max_count == 0) return ESP_ERR_INVALID_ARG;
    if (max_count > map->capacity) max_count = map->capacity;
    if (spacing < 1e-6f) spacing = 1.0f;
    if (diam_px < 2) diam_px = 2;
    if (height_px < 1) height_px = 1;

    float radius = diam_to_radius(diam_px, spacing);
    if (radius < spacing) radius = spacing;
    uint16_t circ = (uint16_t)fmaxf(6.0f, roundf((float)M_PI * (float)(diam_px - 1)));
    uint16_t rings = height_px;
    float height = (rings > 1) ? (float)(rings - 1) * spacing : 0.0f;

    uint16_t i = 0;
    for (uint16_t r = 0; r < rings && i < max_count; ++r) {
        float y = (rings <= 1) ? 0.0f : ((float)r / (float)(rings - 1) - 0.5f) * height;
        for (uint16_t c = 0; c < circ && i < max_count; ++c) {
            float a = (2.0f * (float)M_PI * (float)c) / (float)circ;
            if (!push_map_px(map, &i, max_count, cosf(a) * radius, y, sinf(a) * radius, group)) break;
        }
    }

    if (!open_tb) {
        for (int cap = 0; cap < 2 && i < max_count; ++cap) {
            float y = (cap == 0) ? -0.5f * height : 0.5f * height;
            int n = (int)ceilf(radius / spacing + 1e-4f);
            float r2 = radius * radius + 1e-4f;
            for (int yi = -n; yi <= n && i < max_count; ++yi) {
                for (int xi = -n; xi <= n && i < max_count; ++xi) {
                    float x = (float)xi * spacing;
                    float z = (float)yi * spacing;
                    if (x * x + z * z > r2) continue;
                    if (!push_map_px(map, &i, max_count, x, y, z, group)) break;
                }
            }
        }
    }
    map->count = i;
    return ESP_OK;
}

esp_err_t pm_pixel_map_build_dome(pm_pixel_map_t *map, uint16_t diam_px,
                                  float spacing, uint8_t group, uint16_t max_count)
{
    if (!map || max_count == 0) return ESP_ERR_INVALID_ARG;
    if (max_count > map->capacity) max_count = map->capacity;
    if (spacing < 1e-6f) spacing = 1.0f;
    if (diam_px < 2) diam_px = 2;

    float radius = diam_to_radius(diam_px, spacing);
    if (radius < spacing) radius = spacing;
    uint16_t want = shell_count_for_radius(radius, spacing, 2.0f * (float)M_PI);
    if (want > max_count) want = max_count;

    const float ga = (float)M_PI * (3.0f - sqrtf(5.0f));
    uint16_t i = 0;
    uint16_t gen = (uint16_t)(want * 2 + 8);
    for (uint16_t k = 0; k < gen && i < want; ++k) {
        float t = ((float)k + 0.5f) / (float)gen;
        float y = 1.0f - 2.0f * t;
        if (y < -1e-4f) continue;
        float r_xz = sqrtf(fmaxf(0.0f, 1.0f - y * y));
        float th = ga * (float)k;
        if (!push_map_px(map, &i, max_count,
                         cosf(th) * r_xz * radius, y * radius, sinf(th) * r_xz * radius,
                         group)) break;
    }
    map->count = i;
    return ESP_OK;
}

esp_err_t pm_pixel_map_build_pyramid(pm_pixel_map_t *map, uint16_t base_px, uint16_t height_px,
                                     float spacing, uint8_t group, uint16_t max_count)
{
    if (!map || max_count == 0) return ESP_ERR_INVALID_ARG;
    if (max_count > map->capacity) max_count = map->capacity;
    if (spacing < 1e-6f) spacing = 1.0f;
    if (base_px < 2) base_px = 2;
    if (height_px < 2) height_px = 2;

    uint16_t edge = base_px;
    uint16_t rise = height_px;
    float half = 0.5f * (float)(edge - 1) * spacing;
    float apex_y = (float)(rise - 1) * spacing;
    pm_vec3_t apex = {0, apex_y, 0};
    pm_vec3_t corners[4] = {
        {-half, 0, -half}, { half, 0, -half},
        { half, 0,  half}, {-half, 0,  half},
    };

    uint16_t i = 0;
    for (int e = 0; e < 4 && i < max_count; ++e) {
        pm_vec3_t a = corners[e];
        pm_vec3_t b = corners[(e + 1) & 3];
        for (uint16_t s = 0; s < edge && i < max_count; ++s) {
            if (e > 0 && s == 0) continue;
            float t = (edge <= 1) ? 0.0f : (float)s / (float)(edge - 1);
            if (!push_map_px(map, &i, max_count,
                             a.x + (b.x - a.x) * t, 0, a.z + (b.z - a.z) * t, group)) break;
        }
    }
    for (int e = 0; e < 4 && i < max_count; ++e) {
        pm_vec3_t a = corners[e];
        for (uint16_t s = 1; s < rise && i < max_count; ++s) {
            float t = (float)s / (float)(rise - 1);
            if (!push_map_px(map, &i, max_count,
                             a.x + (apex.x - a.x) * t,
                             a.y + (apex.y - a.y) * t,
                             a.z + (apex.z - a.z) * t, group)) break;
        }
    }
    uint16_t rows = rise;
    if (rows < 2) rows = 2;
    for (int e = 0; e < 4 && i < max_count; ++e) {
        pm_vec3_t a = corners[e];
        pm_vec3_t b = corners[(e + 1) & 3];
        for (uint16_t r = 1; r < rows && i < max_count; ++r) {
            float v = (float)r / (float)rows;
            uint16_t cols = edge - (r * (edge - 1)) / rows;
            if (cols < 1) cols = 1;
            for (uint16_t c = 0; c < cols && i < max_count; ++c) {
                float u = (cols <= 1) ? 0.5f : (float)c / (float)(cols - 1);
                float bx = a.x + (b.x - a.x) * u;
                float bz = a.z + (b.z - a.z) * u;
                if (!push_map_px(map, &i, max_count,
                                 bx + (apex.x - bx) * v,
                                 apex_y * v,
                                 bz + (apex.z - bz) * v, group)) break;
            }
        }
    }
    map->count = i;
    return ESP_OK;
}

static void aabb(const pm_pixel_map_t *map, float *minx, float *maxx,
                 float *miny, float *maxy, float *minz, float *maxz)
{
    *minx = *maxx = map->pixels[0].pos.x;
    *miny = *maxy = map->pixels[0].pos.y;
    *minz = *maxz = map->pixels[0].pos.z;
    for (uint16_t i = 1; i < map->count; ++i) {
        pm_vec3_t p = map->pixels[i].pos;
        if (p.x < *minx) *minx = p.x;
        if (p.x > *maxx) *maxx = p.x;
        if (p.y < *miny) *miny = p.y;
        if (p.y > *maxy) *maxy = p.y;
        if (p.z < *minz) *minz = p.z;
        if (p.z > *maxz) *maxz = p.z;
    }
}

void pm_pixel_map_normalize(pm_pixel_map_t *map)
{
    if (!map || map->count == 0) return;
    float minx, maxx, miny, maxy, minz, maxz;
    aabb(map, &minx, &maxx, &miny, &maxy, &minz, &maxz);
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

void pm_pixel_map_normalize_uniform(pm_pixel_map_t *map)
{
    if (!map || map->count == 0) return;
    float minx, maxx, miny, maxy, minz, maxz;
    aabb(map, &minx, &maxx, &miny, &maxy, &minz, &maxz);
    float dx = maxx - minx;
    float dy = maxy - miny;
    float dz = maxz - minz;
    float s = dx;
    if (dy > s) s = dy;
    if (dz > s) s = dz;
    if (s < 1e-6f) s = 1.0f;
    float cx = 0.5f * (minx + maxx);
    float cy = 0.5f * (miny + maxy);
    float cz = 0.5f * (minz + maxz);
    for (uint16_t i = 0; i < map->count; ++i) {
        map->pixels[i].pos.x = (map->pixels[i].pos.x - cx) / s + 0.5f;
        map->pixels[i].pos.y = (map->pixels[i].pos.y - cy) / s + 0.5f;
        map->pixels[i].pos.z = (map->pixels[i].pos.z - cz) / s + 0.5f;
    }
    map->width = dx / s;
    map->height = dy / s;
    map->depth = dz / s;
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
