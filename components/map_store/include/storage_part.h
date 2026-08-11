#pragma once

/**
 * Resolve the data partition used for maps + media.
 *
 * Prefer PixelMap's native `storage` label. After a WLED→PixelMap OTA
 * install the table is still WLED's, so also accept `spiffs` / LittleFS
 * labels and any SPIFFS-subtype data partition large enough for maps.
 */

#include "esp_partition.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Minimum size for map (256 KiB) + media (128 KiB) regions. */
#define PM_STORAGE_MIN_BYTES 0x60000u

static inline const esp_partition_t *pm_storage_part_find(void)
{
    static const char *const labels[] = {
        "storage", "spiffs", "littlefs", "ffat", NULL
    };
    for (int i = 0; labels[i]; ++i) {
        const esp_partition_t *p = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, labels[i]);
        if (p && p->size >= PM_STORAGE_MIN_BYTES) {
            return p;
        }
    }

    const esp_partition_t *p = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
    if (p && p->size >= PM_STORAGE_MIN_BYTES) {
        return p;
    }

    /* Largest non-system data partition (WLED custom FS labels vary). */
    const esp_partition_t *best = NULL;
    esp_partition_iterator_t it = esp_partition_find(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it) {
        const esp_partition_t *cur = esp_partition_get(it);
        if (cur
            && cur->subtype != ESP_PARTITION_SUBTYPE_DATA_NVS
            && cur->subtype != ESP_PARTITION_SUBTYPE_DATA_OTA
            && cur->subtype != ESP_PARTITION_SUBTYPE_DATA_PHY
            && cur->subtype != ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS
            && cur->size >= PM_STORAGE_MIN_BYTES
            && (!best || cur->size > best->size)) {
            best = cur;
        }
        it = esp_partition_next(it);
    }
    return best;
}

#ifdef __cplusplus
}
#endif
