#include "map_store.h"
#include "esp_log.h"
#include "esp_vfs_spiffs.h"

static const char *TAG = "map_store";

esp_err_t pm_map_store_mount(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 4,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t pm_map_store_save(const pm_pixel_map_t *map)
{
    esp_err_t err = pm_pixel_map_save_path(map, PM_MAP_STORE_PATH);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "map save failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "map persisted (%u points)", (unsigned)pm_pixel_map_count(map));
    }
    return err;
}

esp_err_t pm_map_store_load(pm_pixel_map_t *map)
{
    return pm_pixel_map_load_path(map, PM_MAP_STORE_PATH);
}
