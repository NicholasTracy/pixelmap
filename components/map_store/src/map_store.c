#include "map_store.h"
#include "esp_log.h"
#include "esp_partition.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "map_store";
static const char *PART_LABEL = "storage";

/* On-disk layout: magic(4) + version(4) + json_len(4) + json bytes */
static const uint32_t MAP_MAGIC = 0x504D4D31u; /* 'PMM1' */
static const uint32_t MAP_VERSION = 1;

static const esp_partition_t *storage_part(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                    ESP_PARTITION_SUBTYPE_ANY,
                                    PART_LABEL);
}

esp_err_t pm_map_store_mount(void)
{
    const esp_partition_t *p = storage_part();
    if (!p) {
        ESP_LOGW(TAG, "storage partition not found");
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "storage partition %s size=%u", p->label, (unsigned)p->size);
    return ESP_OK;
}

esp_err_t pm_map_store_save(const pm_pixel_map_t *map)
{
    if (!map) return ESP_ERR_INVALID_ARG;
    const esp_partition_t *p = storage_part();
    if (!p) return ESP_ERR_NOT_FOUND;

    size_t need = (size_t)pm_pixel_map_count(map) * 64u + 32u;
    if (need < 64) need = 64;
    char *buf = malloc(need);
    if (!buf) return ESP_ERR_NO_MEM;

    size_t json_len = 0;
    esp_err_t err = pm_pixel_map_export_json(map, buf, need, &json_len);
    if (err != ESP_OK) {
        free(buf);
        return err;
    }

    size_t total = 12u + json_len;
    if (total > p->size) {
        ESP_LOGE(TAG, "map JSON too large (%u > %u)", (unsigned)total, (unsigned)p->size);
        free(buf);
        return ESP_ERR_INVALID_SIZE;
    }

    err = esp_partition_erase_range(p, 0, p->size);
    if (err != ESP_OK) {
        free(buf);
        return err;
    }

    uint32_t hdr[3] = {MAP_MAGIC, MAP_VERSION, (uint32_t)json_len};
    err = esp_partition_write(p, 0, hdr, sizeof(hdr));
    if (err == ESP_OK) {
        err = esp_partition_write(p, sizeof(hdr), buf, json_len);
    }
    free(buf);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "map persisted (%u points, %u bytes)",
                 (unsigned)pm_pixel_map_count(map), (unsigned)json_len);
    } else {
        ESP_LOGW(TAG, "map save failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t pm_map_store_load(pm_pixel_map_t *map)
{
    if (!map) return ESP_ERR_INVALID_ARG;
    const esp_partition_t *p = storage_part();
    if (!p) return ESP_ERR_NOT_FOUND;

    uint32_t hdr[3] = {0};
    esp_err_t err = esp_partition_read(p, 0, hdr, sizeof(hdr));
    if (err != ESP_OK) return err;
    if (hdr[0] != MAP_MAGIC || hdr[1] != MAP_VERSION) return ESP_ERR_NOT_FOUND;
    uint32_t json_len = hdr[2];
    if (json_len == 0 || json_len > p->size - 12u || json_len > 512u * 1024u) {
        return ESP_ERR_INVALID_SIZE;
    }

    char *buf = malloc((size_t)json_len + 1u);
    if (!buf) return ESP_ERR_NO_MEM;
    err = esp_partition_read(p, 12, buf, json_len);
    if (err != ESP_OK) {
        free(buf);
        return err;
    }
    buf[json_len] = '\0';
    err = pm_pixel_map_import_json(map, buf);
    free(buf);
    return err;
}
