#pragma once

#include "pixel_map.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t pm_map_store_mount(void);
esp_err_t pm_map_store_save(const pm_pixel_map_t *map);
esp_err_t pm_map_store_load(pm_pixel_map_t *map);

#ifdef __cplusplus
}
#endif
