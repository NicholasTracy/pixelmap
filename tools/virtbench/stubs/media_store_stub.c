#include "media_store.h"
#include <string.h>

esp_err_t pm_media_store_init(void) { return ESP_OK; }
esp_err_t pm_media_store_load(void) { return ESP_ERR_NOT_FOUND; }
esp_err_t pm_media_store_erase(void) { return ESP_OK; }

bool pm_media_has_image(void) { return false; }

void pm_media_get_info(pm_media_info_t *out)
{
    if (out) memset(out, 0, sizeof(*out));
}

esp_err_t pm_media_store_set_blob(const uint8_t *data, size_t len)
{
    (void)data;
    (void)len;
    return ESP_ERR_NOT_SUPPORTED;
}

size_t pm_media_blob_size(void) { return 0; }

esp_err_t pm_media_export_blob(uint8_t *dst, size_t dst_cap, size_t *out_len)
{
    (void)dst;
    (void)dst_cap;
    (void)out_len;
    return ESP_ERR_NOT_FOUND;
}

bool pm_media_sample(float u, float v, float t_sec, uint8_t *r, uint8_t *g, uint8_t *b)
{
    (void)u;
    (void)v;
    (void)t_sec;
    (void)r;
    (void)g;
    (void)b;
    return false;
}
