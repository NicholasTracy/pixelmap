#pragma once

#include "config_store.h"
#include "pixel_map.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    pm_app_config_t *cfg;
    pm_pixel_map_t *map;
    void (*on_config_changed)(void);
    void (*on_map_changed)(void);
} pm_web_ui_hooks_t;

esp_err_t pm_web_ui_start(const pm_web_ui_hooks_t *hooks);
void pm_web_ui_stop(void);

#ifdef __cplusplus
}
#endif
