#include "ota_update.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_system.h"
#include <string.h>

static const char *TAG = "ota";
static esp_ota_handle_t s_handle;
static const esp_partition_t *s_part;
static bool s_active;
static size_t s_written;

esp_err_t pm_ota_begin(size_t image_size)
{
    if (s_active) return ESP_ERR_INVALID_STATE;
    s_part = esp_ota_get_next_update_partition(NULL);
    if (!s_part) {
        ESP_LOGE(TAG, "no OTA partition");
        return ESP_ERR_NOT_FOUND;
    }
    if (image_size > 0 && image_size > s_part->size) {
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t err = esp_ota_begin(s_part, image_size > 0 ? image_size : OTA_WITH_SEQUENTIAL_WRITES, &s_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ota_begin: %s", esp_err_to_name(err));
        return err;
    }
    s_active = true;
    s_written = 0;
    ESP_LOGI(TAG, "OTA begin → %s (size hint %u)", s_part->label, (unsigned)image_size);
    return ESP_OK;
}

esp_err_t pm_ota_write(const void *data, size_t len)
{
    if (!s_active || !data || len == 0) return ESP_ERR_INVALID_STATE;
    esp_err_t err = esp_ota_write(s_handle, data, len);
    if (err == ESP_OK) s_written += len;
    return err;
}

esp_err_t pm_ota_finish(void)
{
    if (!s_active) return ESP_ERR_INVALID_STATE;
    esp_err_t err = esp_ota_end(s_handle);
    s_active = false;
    s_handle = 0;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ota_end: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_ota_set_boot_partition(s_part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_boot: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "OTA ok, wrote %u bytes; reboot to apply", (unsigned)s_written);
    return ESP_OK;
}

void pm_ota_abort(void)
{
    if (!s_active) return;
    esp_ota_abort(s_handle);
    s_active = false;
    s_handle = 0;
    s_part = NULL;
    ESP_LOGW(TAG, "OTA aborted");
}

bool pm_ota_in_progress(void) { return s_active; }

const char *pm_ota_running_label(void)
{
    const esp_partition_t *p = esp_ota_get_running_partition();
    return p ? p->label : "?";
}
