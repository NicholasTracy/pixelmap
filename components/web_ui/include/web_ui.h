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
    /**
     * Optional: copy live DMX pixel colors as RGB triplets into dst (3*pixels bytes).
     * Returns pixel count written. Fills *active if non-NULL (stream recently received).
     */
    size_t (*dmx_rgb_snapshot)(uint8_t *dst, size_t dst_cap, bool *active);
} pm_web_ui_hooks_t;

esp_err_t pm_web_ui_start(const pm_web_ui_hooks_t *hooks);
void pm_web_ui_stop(void);

#ifdef __cplusplus
}
#endif
