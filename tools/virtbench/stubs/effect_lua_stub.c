#include "effect_lua.h"

esp_err_t pm_effect_lua_init(void) { return ESP_OK; }
esp_err_t pm_effect_lua_set_script(const char *src)
{
    (void)src;
    return ESP_OK;
}
const char *pm_effect_lua_get_script(void) { return ""; }
bool pm_effect_lua_ready(void) { return false; }
const char *pm_effect_lua_last_error(void) { return "stub"; }

pm_rgb_t pm_effect_lua_eval(const pm_lua_effect_inputs_t *in,
                            uint16_t i, uint16_t count,
                            float x, float y, float z, float t)
{
    (void)in;
    (void)i;
    (void)count;
    (void)x;
    (void)y;
    (void)z;
    (void)t;
    return (pm_rgb_t){0, 0, 0};
}
