#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pm_sacn_dmx_cb_t)(uint16_t universe, const uint8_t *data, uint16_t len, void *user);

typedef struct {
    uint16_t universe_start;
    uint16_t universe_count;
    bool join_multicast;
    pm_sacn_dmx_cb_t on_dmx;
    void *user;
} pm_sacn_config_t;

esp_err_t pm_sacn_start(const pm_sacn_config_t *cfg);
void pm_sacn_stop(void);
bool pm_sacn_active(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
