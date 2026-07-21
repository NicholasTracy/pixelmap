#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *sta_ssid;
    const char *sta_pass;
    const char *hostname;
    bool ap_fallback;
} pm_wifi_config_t;

esp_err_t pm_wifi_start(const pm_wifi_config_t *cfg);
/** Apply new STA credentials / hostname without full stack re-init. */
esp_err_t pm_wifi_apply(const pm_wifi_config_t *cfg);
bool pm_wifi_sta_connected(void);
bool pm_wifi_ap_active(void);
bool pm_wifi_sta_connecting(void);
const char *pm_wifi_ip_str(void);

#ifdef __cplusplus
}
#endif
