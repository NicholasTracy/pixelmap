#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Begin streaming an OTA image (call once per update). */
esp_err_t pm_ota_begin(size_t image_size);

/** Write the next chunk of the firmware image. */
esp_err_t pm_ota_write(const void *data, size_t len);

/** Finish, validate, and set boot partition. Reboot separately. */
esp_err_t pm_ota_finish(void);

/** Abort an in-progress update and free resources. */
void pm_ota_abort(void);

bool pm_ota_in_progress(void);
const char *pm_ota_running_label(void);

#ifdef __cplusplus
}
#endif
