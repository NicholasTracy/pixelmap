#pragma once

#include "config_store.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_PRESET_SLOTS 4
#define PM_PRESET_NAME_MAX 24

typedef struct {
    char name[PM_PRESET_NAME_MAX];
    bool used;
    pm_effect_id_t effect_id;
    float effect_speed;
    float effect_scale;
    uint8_t effect_intensity;
    uint8_t effect_primary_h, effect_primary_s, effect_primary_v;
    uint8_t effect_secondary_h, effect_secondary_s, effect_secondary_v;
    uint8_t effect_p[PM_FX_P_COUNT];
    uint8_t effect_pos[3];
    uint8_t effect_rot[3];
} pm_preset_t;

esp_err_t pm_presets_load(void);
esp_err_t pm_presets_save_slot(uint8_t slot, const char *name, const pm_app_config_t *cfg);
esp_err_t pm_presets_apply_slot(uint8_t slot, pm_app_config_t *cfg);
esp_err_t pm_presets_clear_slot(uint8_t slot);
const pm_preset_t *pm_presets_get(uint8_t slot);

#ifdef __cplusplus
}
#endif
