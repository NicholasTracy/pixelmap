#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_MEDIA_MAGIC     0x44454D50u /* 'PMED' little-endian */
#define PM_MEDIA_VERSION   1u
#define PM_MEDIA_MAX_W     96u
#define PM_MEDIA_MAX_H     96u
#define PM_MEDIA_MAX_FRAMES 8u
#define PM_MEDIA_HDR_SIZE  16u

/** Shared `storage` partition: map uses [0, MAP_BYTES), media uses [MEDIA_OFF, …). */
#define PM_STORAGE_MAP_BYTES    0x40000u /* 256 KiB */
#define PM_STORAGE_MEDIA_OFF    0x40000u
#define PM_STORAGE_MEDIA_BYTES  0x20000u /* 128 KiB */

typedef struct {
    uint16_t w;
    uint16_t h;
    uint16_t frames;
    uint16_t fps_x100; /* 1000 = 10 fps */
    uint32_t pixels;   /* RGB565 count = w*h*frames */
} pm_media_info_t;

esp_err_t pm_media_store_init(void);
esp_err_t pm_media_store_load(void);
esp_err_t pm_media_store_erase(void);

bool pm_media_has_image(void);
void pm_media_get_info(pm_media_info_t *out);

/**
 * Replace media from a complete PMED blob (header + RGB565 pixels).
 * Validates size limits and persists to flash.
 */
esp_err_t pm_media_store_set_blob(const uint8_t *data, size_t len);

/** Bytes needed for a full PMED export (0 if no media). */
size_t pm_media_blob_size(void);

/** Copy full PMED blob into dst. */
esp_err_t pm_media_export_blob(uint8_t *dst, size_t dst_cap, size_t *out_len);

/** Sample media at UV in 0..1 (v=0 top). t_sec drives frame animation. Returns false if empty. */
bool pm_media_sample(float u, float v, float t_sec, uint8_t *r, uint8_t *g, uint8_t *b);

#ifdef __cplusplus
}
#endif
