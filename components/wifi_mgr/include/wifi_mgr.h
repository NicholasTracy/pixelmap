#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *sta_ssid;
    const char *sta_pass;
    const char *hostname;
    /** Keep SoftAP up alongside STA (APSTA) or alone when no STA. */
    bool ap_enable;
    /** If STA drops and AP is not forced on, bring SoftAP up for recovery. */
    bool ap_fallback;
    /** SoftAP SSID; empty → PixelMap-XXXX from MAC. */
    const char *ap_ssid;
    /** SoftAP password; empty → "pixelmap1". Must be ≥8 chars for WPA2. */
    const char *ap_pass;
} pm_wifi_config_t;

typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t authmode; /* wifi_auth_mode_t */
    bool open;
} pm_wifi_scan_ap_t;

typedef struct {
    bool sta_connected;
    bool sta_connecting;
    bool ap_active;
    bool ap_enable;
    bool ap_fallback;
    char sta_ip[16];
    char ap_ip[16];
    char sta_ssid[33];
    char ap_ssid[33];
    char hostname[32];
    const char *mode; /* "STA" | "AP" | "APSTA" | "OFF" */
} pm_wifi_status_t;

esp_err_t pm_wifi_start(const pm_wifi_config_t *cfg);
/** Apply STA/AP credentials and mode without full stack re-init. */
esp_err_t pm_wifi_apply(const pm_wifi_config_t *cfg);
/** Advertise hostname.local and HTTP service via mDNS. */
esp_err_t pm_wifi_mdns_start(const char *hostname);

bool pm_wifi_sta_connected(void);
bool pm_wifi_ap_active(void);
bool pm_wifi_sta_connecting(void);
/** Prefer STA IP when connected, else SoftAP IP (192.168.4.1). */
const char *pm_wifi_ip_str(void);
void pm_wifi_get_status(pm_wifi_status_t *out);

/**
 * Blocking scan (a few seconds). Caller provides array; returns count written.
 * Device should be in STA or APSTA mode.
 */
esp_err_t pm_wifi_scan(pm_wifi_scan_ap_t *out, size_t max_out, size_t *count);

#ifdef __cplusplus
}
#endif
