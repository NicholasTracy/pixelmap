#include "wifi_mgr.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "mdns.h"
#include "nvs_flash.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "wifi";
static bool s_started;
static bool s_sta_up;
static bool s_ap_up;
static bool s_sta_connecting;
static char s_sta_ip[16] = "0.0.0.0";
static char s_ap_ip[16] = "192.168.4.1";
static bool s_ap_enable;
static bool s_ap_fallback;
static char s_ssid[33];
static char s_pass[65];
static char s_hostname[32];
static char s_ap_ssid[33];
static char s_ap_pass[65];

static void apply_ap_config(void)
{
    wifi_config_t ap = {0};
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, mac);

    if (s_ap_ssid[0]) {
        strncpy((char *)ap.ap.ssid, s_ap_ssid, sizeof(ap.ap.ssid) - 1);
    } else {
        snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "PixelMap-%02X%02X", mac[4], mac[5]);
    }

    const char *pass = s_ap_pass[0] ? s_ap_pass : "pixelmap1";
    size_t plen = strlen(pass);
    if (plen > 0 && plen < 8) {
        ESP_LOGW(TAG, "AP password < 8 chars; using default pixelmap1");
        pass = "pixelmap1";
        plen = 9;
    }
    strncpy((char *)ap.ap.password, pass, sizeof(ap.ap.password) - 1);
    ap.ap.ssid_len = strlen((char *)ap.ap.ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = (plen == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    s_ap_up = true;
    strncpy(s_ap_ip, "192.168.4.1", sizeof(s_ap_ip) - 1);
    ESP_LOGI(TAG, "AP SSID=%s", ap.ap.ssid);
}

static void set_mode_for_state(void)
{
    bool want_sta = s_ssid[0] != 0;
    bool want_ap = s_ap_enable || !want_sta || (s_ap_fallback && !s_sta_up);

    wifi_mode_t mode;
    if (want_sta && want_ap) mode = WIFI_MODE_APSTA;
    else if (want_sta) mode = WIFI_MODE_STA;
    else mode = WIFI_MODE_AP;

    ESP_ERROR_CHECK(esp_wifi_set_mode(mode));
    if (want_ap) {
        apply_ap_config();
    } else {
        s_ap_up = false;
    }
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_ssid[0]) {
            s_sta_connecting = true;
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_sta_up = false;
        s_sta_connecting = s_ssid[0] != 0;
        if (s_ap_fallback || s_ap_enable || !s_ssid[0]) {
            set_mode_for_state();
        }
        if (s_ssid[0]) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_START) {
        s_ap_up = true;
        strncpy(s_ap_ip, "192.168.4.1", sizeof(s_ap_ip) - 1);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STOP) {
        if (!s_ap_enable && !(s_ap_fallback && !s_sta_up) && s_ssid[0]) {
            s_ap_up = false;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        s_sta_up = true;
        s_sta_connecting = false;
        snprintf(s_sta_ip, sizeof(s_sta_ip), IPSTR, IP2STR(&e->ip_info.ip));
        ESP_LOGI(TAG, "STA IP %s", s_sta_ip);
        /* If AP was only for fallback and not forced, can leave APSTA when connected */
        if (!s_ap_enable && s_ap_fallback) {
            /* Keep APSTA so clients already on SoftAP are not kicked; user can disable AP. */
        }
    }
}

static void copy_cfg(const pm_wifi_config_t *cfg)
{
    s_ap_enable = cfg->ap_enable;
    s_ap_fallback = cfg->ap_fallback;
    memset(s_ssid, 0, sizeof(s_ssid));
    memset(s_pass, 0, sizeof(s_pass));
    memset(s_hostname, 0, sizeof(s_hostname));
    memset(s_ap_ssid, 0, sizeof(s_ap_ssid));
    memset(s_ap_pass, 0, sizeof(s_ap_pass));
    if (cfg->sta_ssid) strncpy(s_ssid, cfg->sta_ssid, sizeof(s_ssid) - 1);
    if (cfg->sta_pass) strncpy(s_pass, cfg->sta_pass, sizeof(s_pass) - 1);
    if (cfg->hostname) strncpy(s_hostname, cfg->hostname, sizeof(s_hostname) - 1);
    if (cfg->ap_ssid) strncpy(s_ap_ssid, cfg->ap_ssid, sizeof(s_ap_ssid) - 1);
    if (cfg->ap_pass) strncpy(s_ap_pass, cfg->ap_pass, sizeof(s_ap_pass) - 1);
}

esp_err_t pm_wifi_start(const pm_wifi_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    if (s_started) return pm_wifi_apply(cfg);

    copy_cfg(cfg);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));

    set_mode_for_state();
    if (s_ssid[0]) {
        wifi_config_t sta = {0};
        strncpy((char *)sta.sta.ssid, s_ssid, sizeof(sta.sta.ssid) - 1);
        strncpy((char *)sta.sta.password, s_pass, sizeof(sta.sta.password) - 1);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
        s_sta_connecting = true;
    }
    if (s_hostname[0]) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) esp_netif_set_hostname(netif, s_hostname);
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    s_started = true;
    return ESP_OK;
}

esp_err_t pm_wifi_apply(const pm_wifi_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    if (!s_started) return pm_wifi_start(cfg);

    char old_ssid[33];
    char old_pass[65];
    char old_host[32];
    char old_ap_ssid[33];
    char old_ap_pass[65];
    bool old_ap_en = s_ap_enable;
    bool old_ap_fb = s_ap_fallback;
    strncpy(old_ssid, s_ssid, sizeof(old_ssid));
    strncpy(old_pass, s_pass, sizeof(old_pass));
    strncpy(old_host, s_hostname, sizeof(old_host));
    strncpy(old_ap_ssid, s_ap_ssid, sizeof(old_ap_ssid));
    strncpy(old_ap_pass, s_ap_pass, sizeof(old_ap_pass));

    copy_cfg(cfg);

    bool host_changed = strcmp(old_host, s_hostname) != 0;
    bool sta_changed = strcmp(old_ssid, s_ssid) != 0 || strcmp(old_pass, s_pass) != 0;
    bool ap_changed = old_ap_en != s_ap_enable || old_ap_fb != s_ap_fallback ||
                      strcmp(old_ap_ssid, s_ap_ssid) != 0 || strcmp(old_ap_pass, s_ap_pass) != 0;

    if (host_changed && s_hostname[0]) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) esp_netif_set_hostname(netif, s_hostname);
    }

    if (sta_changed || ap_changed) {
        if (sta_changed) {
            s_sta_up = false;
            esp_wifi_disconnect();
        }
        set_mode_for_state();
        if (s_ssid[0]) {
            wifi_config_t sta = {0};
            strncpy((char *)sta.sta.ssid, s_ssid, sizeof(sta.sta.ssid) - 1);
            strncpy((char *)sta.sta.password, s_pass, sizeof(sta.sta.password) - 1);
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
            s_sta_connecting = true;
            esp_wifi_connect();
            ESP_LOGI(TAG, "STA → %s", s_ssid);
        } else {
            s_sta_connecting = false;
            ESP_LOGI(TAG, "STA cleared");
        }
    }
    return ESP_OK;
}

esp_err_t pm_wifi_mdns_start(const char *hostname)
{
    const char *host = (hostname && hostname[0]) ? hostname : "pixelmap";
    esp_err_t err = mdns_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "mdns_init: %s", esp_err_to_name(err));
        return err;
    }
    mdns_hostname_set(host);
    mdns_instance_name_set("PixelMap");
    mdns_service_add("PixelMap", "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS http://%s.local/", host);
    return ESP_OK;
}

bool pm_wifi_sta_connected(void) { return s_sta_up; }
bool pm_wifi_ap_active(void) { return s_ap_up; }
bool pm_wifi_sta_connecting(void) { return s_sta_connecting && !s_sta_up; }

const char *pm_wifi_ip_str(void)
{
    if (s_sta_up && s_sta_ip[0] && strcmp(s_sta_ip, "0.0.0.0") != 0) return s_sta_ip;
    if (s_ap_up) return s_ap_ip;
    return s_sta_ip;
}

void pm_wifi_get_status(pm_wifi_status_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->sta_connected = s_sta_up;
    out->sta_connecting = s_sta_connecting && !s_sta_up;
    out->ap_active = s_ap_up;
    out->ap_enable = s_ap_enable;
    out->ap_fallback = s_ap_fallback;
    strncpy(out->sta_ip, s_sta_ip, sizeof(out->sta_ip) - 1);
    strncpy(out->ap_ip, s_ap_ip, sizeof(out->ap_ip) - 1);
    strncpy(out->sta_ssid, s_ssid, sizeof(out->sta_ssid) - 1);
    strncpy(out->hostname, s_hostname, sizeof(out->hostname) - 1);
    if (s_ap_ssid[0]) {
        strncpy(out->ap_ssid, s_ap_ssid, sizeof(out->ap_ssid) - 1);
    } else {
        uint8_t mac[6] = {0};
        esp_wifi_get_mac(WIFI_IF_AP, mac);
        snprintf(out->ap_ssid, sizeof(out->ap_ssid), "PixelMap-%02X%02X", mac[4], mac[5]);
    }
    bool want_sta = s_ssid[0] != 0;
    bool want_ap = s_ap_up || s_ap_enable || (!want_sta);
    if (want_sta && want_ap) out->mode = "APSTA";
    else if (want_sta) out->mode = "STA";
    else if (want_ap) out->mode = "AP";
    else out->mode = "OFF";
}

esp_err_t pm_wifi_scan(pm_wifi_scan_ap_t *out, size_t max_out, size_t *count)
{
    if (!out || !count || max_out == 0) return ESP_ERR_INVALID_ARG;
    *count = 0;
    if (!s_started) return ESP_ERR_INVALID_STATE;

    /* Scanning needs STA interface up */
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_AP) {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        apply_ap_config();
    }

    wifi_scan_config_t sc = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {.active = {.min = 100, .max = 300}},
    };
    esp_err_t err = esp_wifi_scan_start(&sc, true);
    if (err != ESP_OK) return err;

    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n == 0) return ESP_OK;
    if (n > 40) n = 40;
    wifi_ap_record_t *recs = calloc(n, sizeof(*recs));
    if (!recs) return ESP_ERR_NO_MEM;
    uint16_t got = n;
    err = esp_wifi_scan_get_ap_records(&got, recs);
    if (err != ESP_OK) {
        free(recs);
        return err;
    }

    size_t w = 0;
    for (uint16_t i = 0; i < got && w < max_out; ++i) {
        /* Deduplicate by SSID, keep strongest */
        bool dup = false;
        for (size_t j = 0; j < w; ++j) {
            if (strcmp(out[j].ssid, (const char *)recs[i].ssid) == 0) {
                if (recs[i].rssi > out[j].rssi) {
                    out[j].rssi = recs[i].rssi;
                    out[j].authmode = (uint8_t)recs[i].authmode;
                    out[j].open = recs[i].authmode == WIFI_AUTH_OPEN;
                }
                dup = true;
                break;
            }
        }
        if (dup) continue;
        memset(&out[w], 0, sizeof(out[w]));
        strncpy(out[w].ssid, (const char *)recs[i].ssid, sizeof(out[w].ssid) - 1);
        out[w].rssi = recs[i].rssi;
        out[w].authmode = (uint8_t)recs[i].authmode;
        out[w].open = recs[i].authmode == WIFI_AUTH_OPEN;
        w++;
    }
    free(recs);
    *count = w;
    return ESP_OK;
}
