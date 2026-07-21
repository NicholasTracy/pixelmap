#include "presets.h"
#include "nvs.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "presets";
static const char *NS = "pm_preset";
static pm_preset_t s_slots[PM_PRESET_SLOTS];

esp_err_t pm_presets_load(void)
{
    memset(s_slots, 0, sizeof(s_slots));
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    for (int i = 0; i < PM_PRESET_SLOTS; ++i) {
        char key[8];
        snprintf(key, sizeof(key), "p%d", i);
        size_t len = sizeof(s_slots[i]);
        if (nvs_get_blob(h, key, &s_slots[i], &len) == ESP_OK && len == sizeof(s_slots[i])) {
            /* ok */
        } else {
            memset(&s_slots[i], 0, sizeof(s_slots[i]));
        }
    }
    nvs_close(h);
    return ESP_OK;
}

static esp_err_t persist_all(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    for (int i = 0; i < PM_PRESET_SLOTS; ++i) {
        char key[8];
        snprintf(key, sizeof(key), "p%d", i);
        if (s_slots[i].used) {
            nvs_set_blob(h, key, &s_slots[i], sizeof(s_slots[i]));
        } else {
            nvs_erase_key(h, key);
        }
    }
    err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t pm_presets_save_slot(uint8_t slot, const char *name, const pm_app_config_t *cfg)
{
    if (slot >= PM_PRESET_SLOTS || !cfg) return ESP_ERR_INVALID_ARG;
    pm_preset_t *p = &s_slots[slot];
    memset(p, 0, sizeof(*p));
    p->used = true;
    if (name && name[0]) {
        strncpy(p->name, name, sizeof(p->name) - 1);
    } else {
        snprintf(p->name, sizeof(p->name), "Preset %u", (unsigned)(slot + 1));
    }
    p->effect_id = cfg->effect_id;
    p->effect_speed = cfg->effect_speed;
    p->effect_scale = cfg->effect_scale;
    p->effect_intensity = cfg->effect_intensity;
    p->effect_primary_h = cfg->effect_primary_h;
    p->effect_primary_s = cfg->effect_primary_s;
    p->effect_primary_v = cfg->effect_primary_v;
    p->effect_secondary_h = cfg->effect_secondary_h;
    p->effect_secondary_s = cfg->effect_secondary_s;
    p->effect_secondary_v = cfg->effect_secondary_v;
    memcpy(p->effect_p, cfg->effect_p, sizeof(p->effect_p));
    memcpy(p->effect_pos, cfg->effect_pos, sizeof(p->effect_pos));
    memcpy(p->effect_rot, cfg->effect_rot, sizeof(p->effect_rot));
    ESP_LOGI(TAG, "saved slot %u '%s'", (unsigned)slot, p->name);
    return persist_all();
}

esp_err_t pm_presets_apply_slot(uint8_t slot, pm_app_config_t *cfg)
{
    if (slot >= PM_PRESET_SLOTS || !cfg) return ESP_ERR_INVALID_ARG;
    const pm_preset_t *p = &s_slots[slot];
    if (!p->used) return ESP_ERR_NOT_FOUND;
    cfg->effect_id = p->effect_id;
    cfg->effect_speed = p->effect_speed;
    cfg->effect_scale = p->effect_scale;
    cfg->effect_intensity = p->effect_intensity;
    cfg->effect_primary_h = p->effect_primary_h;
    cfg->effect_primary_s = p->effect_primary_s;
    cfg->effect_primary_v = p->effect_primary_v;
    cfg->effect_secondary_h = p->effect_secondary_h;
    cfg->effect_secondary_s = p->effect_secondary_s;
    cfg->effect_secondary_v = p->effect_secondary_v;
    memcpy(cfg->effect_p, p->effect_p, sizeof(cfg->effect_p));
    memcpy(cfg->effect_pos, p->effect_pos, sizeof(cfg->effect_pos));
    memcpy(cfg->effect_rot, p->effect_rot, sizeof(cfg->effect_rot));
    return ESP_OK;
}

esp_err_t pm_presets_clear_slot(uint8_t slot)
{
    if (slot >= PM_PRESET_SLOTS) return ESP_ERR_INVALID_ARG;
    memset(&s_slots[slot], 0, sizeof(s_slots[slot]));
    return persist_all();
}

const pm_preset_t *pm_presets_get(uint8_t slot)
{
    if (slot >= PM_PRESET_SLOTS) return NULL;
    return &s_slots[slot];
}
