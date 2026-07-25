#include "media_store.h"
#include "esp_log.h"
#include "esp_partition.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "media_store";
static const char *PART_LABEL = "storage";

static uint16_t *s_rgb565;
static pm_media_info_t s_info;
static bool s_loaded;

static const esp_partition_t *storage_part(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                    ESP_PARTITION_SUBTYPE_ANY,
                                    PART_LABEL);
}

static size_t pixel_bytes(const pm_media_info_t *info)
{
    return (size_t)info->w * (size_t)info->h * (size_t)info->frames * 2u;
}

static bool info_ok(const pm_media_info_t *info)
{
    if (!info) return false;
    if (info->w == 0 || info->h == 0 || info->frames == 0) return false;
    if (info->w > PM_MEDIA_MAX_W || info->h > PM_MEDIA_MAX_H) return false;
    if (info->frames > PM_MEDIA_MAX_FRAMES) return false;
    size_t need = PM_MEDIA_HDR_SIZE + pixel_bytes(info);
    return need <= PM_STORAGE_MEDIA_BYTES;
}

static void clear_ram(void)
{
    free(s_rgb565);
    s_rgb565 = NULL;
    memset(&s_info, 0, sizeof(s_info));
    s_loaded = false;
}

esp_err_t pm_media_store_init(void)
{
    const esp_partition_t *p = storage_part();
    if (!p) {
        ESP_LOGW(TAG, "storage partition not found");
        return ESP_ERR_NOT_FOUND;
    }
    if (p->size < PM_STORAGE_MEDIA_OFF + PM_STORAGE_MEDIA_BYTES) {
        ESP_LOGE(TAG, "storage too small for media region");
        return ESP_ERR_INVALID_SIZE;
    }
    return pm_media_store_load();
}

esp_err_t pm_media_store_load(void)
{
    clear_ram();
    const esp_partition_t *p = storage_part();
    if (!p) return ESP_ERR_NOT_FOUND;

    uint8_t hdr[PM_MEDIA_HDR_SIZE];
    esp_err_t err = esp_partition_read(p, PM_STORAGE_MEDIA_OFF, hdr, sizeof(hdr));
    if (err != ESP_OK) return err;

    uint32_t magic = 0, version = 0;
    memcpy(&magic, hdr + 0, 4);
    memcpy(&version, hdr + 4, 4);
    if (magic != PM_MEDIA_MAGIC || version != PM_MEDIA_VERSION) {
        return ESP_ERR_NOT_FOUND;
    }

    pm_media_info_t info = {0};
    memcpy(&info.w, hdr + 8, 2);
    memcpy(&info.h, hdr + 10, 2);
    memcpy(&info.frames, hdr + 12, 2);
    memcpy(&info.fps_x100, hdr + 14, 2);
    info.pixels = (uint32_t)info.w * info.h * info.frames;
    if (!info_ok(&info)) return ESP_ERR_INVALID_SIZE;

    size_t nbytes = pixel_bytes(&info);
    uint16_t *pix = (uint16_t *)malloc(nbytes);
    if (!pix) return ESP_ERR_NO_MEM;
    err = esp_partition_read(p, PM_STORAGE_MEDIA_OFF + PM_MEDIA_HDR_SIZE, pix, nbytes);
    if (err != ESP_OK) {
        free(pix);
        return err;
    }
    s_rgb565 = pix;
    s_info = info;
    s_loaded = true;
    ESP_LOGI(TAG, "media loaded %ux%u × %u frames",
             (unsigned)info.w, (unsigned)info.h, (unsigned)info.frames);
    return ESP_OK;
}

esp_err_t pm_media_store_erase(void)
{
    clear_ram();
    const esp_partition_t *p = storage_part();
    if (!p) return ESP_ERR_NOT_FOUND;
    return esp_partition_erase_range(p, PM_STORAGE_MEDIA_OFF, PM_STORAGE_MEDIA_BYTES);
}

bool pm_media_has_image(void)
{
    return s_loaded && s_rgb565 && s_info.pixels > 0;
}

void pm_media_get_info(pm_media_info_t *out)
{
    if (!out) return;
    *out = s_info;
}

esp_err_t pm_media_store_set_blob(const uint8_t *data, size_t len)
{
    if (!data || len < PM_MEDIA_HDR_SIZE) return ESP_ERR_INVALID_ARG;

    uint32_t magic = 0, version = 0;
    memcpy(&magic, data + 0, 4);
    memcpy(&version, data + 4, 4);
    if (magic != PM_MEDIA_MAGIC || version != PM_MEDIA_VERSION) {
        return ESP_ERR_INVALID_ARG;
    }

    pm_media_info_t info = {0};
    memcpy(&info.w, data + 8, 2);
    memcpy(&info.h, data + 10, 2);
    memcpy(&info.frames, data + 12, 2);
    memcpy(&info.fps_x100, data + 14, 2);
    info.pixels = (uint32_t)info.w * info.h * info.frames;
    if (!info_ok(&info)) return ESP_ERR_INVALID_SIZE;

    size_t nbytes = pixel_bytes(&info);
    if (len < PM_MEDIA_HDR_SIZE + nbytes) return ESP_ERR_INVALID_SIZE;
    if (PM_MEDIA_HDR_SIZE + nbytes > PM_STORAGE_MEDIA_BYTES) return ESP_ERR_INVALID_SIZE;

    const esp_partition_t *p = storage_part();
    if (!p) return ESP_ERR_NOT_FOUND;

    esp_err_t err = esp_partition_erase_range(p, PM_STORAGE_MEDIA_OFF, PM_STORAGE_MEDIA_BYTES);
    if (err != ESP_OK) return err;
    err = esp_partition_write(p, PM_STORAGE_MEDIA_OFF, data, PM_MEDIA_HDR_SIZE + nbytes);
    if (err != ESP_OK) return err;

    uint16_t *pix = (uint16_t *)malloc(nbytes);
    if (!pix) return ESP_ERR_NO_MEM;
    memcpy(pix, data + PM_MEDIA_HDR_SIZE, nbytes);
    free(s_rgb565);
    s_rgb565 = pix;
    s_info = info;
    s_loaded = true;
    ESP_LOGI(TAG, "media saved %ux%u × %u frames (%u bytes)",
             (unsigned)info.w, (unsigned)info.h, (unsigned)info.frames, (unsigned)nbytes);
    return ESP_OK;
}

size_t pm_media_blob_size(void)
{
    if (!pm_media_has_image()) return 0;
    return PM_MEDIA_HDR_SIZE + pixel_bytes(&s_info);
}

esp_err_t pm_media_export_blob(uint8_t *dst, size_t dst_cap, size_t *out_len)
{
    if (!dst || !out_len) return ESP_ERR_INVALID_ARG;
    if (!pm_media_has_image()) return ESP_ERR_NOT_FOUND;
    size_t need = pm_media_blob_size();
    if (dst_cap < need) return ESP_ERR_INVALID_SIZE;

    uint32_t magic = PM_MEDIA_MAGIC;
    uint32_t version = PM_MEDIA_VERSION;
    memcpy(dst + 0, &magic, 4);
    memcpy(dst + 4, &version, 4);
    memcpy(dst + 8, &s_info.w, 2);
    memcpy(dst + 10, &s_info.h, 2);
    memcpy(dst + 12, &s_info.frames, 2);
    memcpy(dst + 14, &s_info.fps_x100, 2);
    memcpy(dst + PM_MEDIA_HDR_SIZE, s_rgb565, pixel_bytes(&s_info));
    *out_len = need;
    return ESP_OK;
}

static void rgb565_to_rgb(uint16_t c, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t r5 = (uint8_t)((c >> 11) & 0x1f);
    uint8_t g6 = (uint8_t)((c >> 5) & 0x3f);
    uint8_t b5 = (uint8_t)(c & 0x1f);
    *r = (uint8_t)((r5 << 3) | (r5 >> 2));
    *g = (uint8_t)((g6 << 2) | (g6 >> 4));
    *b = (uint8_t)((b5 << 3) | (b5 >> 2));
}

bool pm_media_sample(float u, float v, float t_sec, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (!pm_media_has_image() || !r || !g || !b) return false;
    if (u < 0.f) u = 0.f;
    if (u > 0.999f) u = 0.999f;
    if (v < 0.f) v = 0.f;
    if (v > 0.999f) v = 0.999f;

    uint16_t frame = 0;
    if (s_info.frames > 1) {
        float fps = s_info.fps_x100 > 0 ? (s_info.fps_x100 / 100.f) : 10.f;
        float f = t_sec * fps;
        if (f < 0.f) f = 0.f;
        frame = (uint16_t)((uint32_t)f % (uint32_t)s_info.frames);
    }

    int x = (int)(u * (float)s_info.w);
    int y = (int)(v * (float)s_info.h);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= (int)s_info.w) x = (int)s_info.w - 1;
    if (y >= (int)s_info.h) y = (int)s_info.h - 1;

    size_t idx = ((size_t)frame * (size_t)s_info.w * (size_t)s_info.h)
               + ((size_t)y * (size_t)s_info.w) + (size_t)x;
    rgb565_to_rgb(s_rgb565[idx], r, g, b);
    return true;
}
