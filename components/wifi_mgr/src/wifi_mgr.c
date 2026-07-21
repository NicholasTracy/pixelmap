#include "wifi_mgr.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "wifi";
static bool s_started;
static bool s_sta_up;
static bool s_ap_up;
static bool s_sta_connecting;
static char s_ip[16] = "0.0.0.0";
static bool s_ap_fallback;
static char s_ssid[33];
static char s_pass[65];
static char s_hostname[32];

static void start_ap(void)
{
    wifi_config_t ap = {0};
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "PixelMap-%02X%02X", mac[4], mac[5]);
    strncpy((char *)ap.ap.password, "pixelmap1", sizeof(ap.ap.password) - 1);
    ap.ap.ssid_len = strlen((char *)ap.ap.ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    s_ap_up = true;
    ESP_LOGI(TAG, "AP %s / pixelmap1", ap.ap.ssid);
    strncpy(s_ip, "192.168.4.1", sizeof(s_ip) - 1);
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
        if (s_ap_fallback) start_ap();
        if (s_ssid[0]) esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        s_sta_up = true;
        s_sta_connecting = false;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&e->ip_info.ip));
        ESP_LOGI(TAG, "STA IP %s", s_ip);
    }
}

esp_err_t pm_wifi_start(const pm_wifi_config_t *cfg)
{
    if (s_started) return pm_wifi_apply(cfg);

    s_ap_fallback = cfg->ap_fallback;
    memset(s_ssid, 0, sizeof(s_ssid));
    memset(s_pass, 0, sizeof(s_pass));
    memset(s_hostname, 0, sizeof(s_hostname));
    if (cfg->sta_ssid) strncpy(s_ssid, cfg->sta_ssid, sizeof(s_ssid) - 1);
    if (cfg->sta_pass) strncpy(s_pass, cfg->sta_pass, sizeof(s_pass) - 1);
    if (cfg->hostname) strncpy(s_hostname, cfg->hostname, sizeof(s_hostname) - 1);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(s_ssid[0] ? WIFI_MODE_STA : WIFI_MODE_AP));
    if (s_ssid[0]) {
        wifi_config_t sta = {0};
        strncpy((char *)sta.sta.ssid, s_ssid, sizeof(sta.sta.ssid) - 1);
        strncpy((char *)sta.sta.password, s_pass, sizeof(sta.sta.password) - 1);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
        s_sta_connecting = true;
    } else {
        start_ap();
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

    s_ap_fallback = cfg->ap_fallback;
    char new_ssid[33] = {0};
    char new_pass[65] = {0};
    char new_host[32] = {0};
    if (cfg->sta_ssid) strncpy(new_ssid, cfg->sta_ssid, sizeof(new_ssid) - 1);
    if (cfg->sta_pass) strncpy(new_pass, cfg->sta_pass, sizeof(new_pass) - 1);
    if (cfg->hostname) strncpy(new_host, cfg->hostname, sizeof(new_host) - 1);

    bool creds_changed = strcmp(new_ssid, s_ssid) != 0 || strcmp(new_pass, s_pass) != 0;
    bool host_changed = strcmp(new_host, s_hostname) != 0;

    strncpy(s_ssid, new_ssid, sizeof(s_ssid) - 1);
    strncpy(s_pass, new_pass, sizeof(s_pass) - 1);
    strncpy(s_hostname, new_host, sizeof(s_hostname) - 1);

    if (host_changed && s_hostname[0]) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) esp_netif_set_hostname(netif, s_hostname);
    }

    if (!creds_changed) return ESP_OK;

    s_sta_up = false;
    esp_wifi_disconnect();

    if (s_ssid[0]) {
        wifi_config_t sta = {0};
        strncpy((char *)sta.sta.ssid, s_ssid, sizeof(sta.sta.ssid) - 1);
        strncpy((char *)sta.sta.password, s_pass, sizeof(sta.sta.password) - 1);
        ESP_ERROR_CHECK(esp_wifi_set_mode(s_ap_up ? WIFI_MODE_APSTA : WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
        s_sta_connecting = true;
        esp_wifi_connect();
        ESP_LOGI(TAG, "STA credentials applied, reconnecting to %s", s_ssid);
    } else {
        s_sta_connecting = false;
        start_ap();
        ESP_LOGI(TAG, "STA cleared; AP fallback active");
    }
    return ESP_OK;
}

bool pm_wifi_sta_connected(void) { return s_sta_up; }
bool pm_wifi_ap_active(void) { return s_ap_up; }
bool pm_wifi_sta_connecting(void) { return s_sta_connecting && !s_sta_up; }
const char *pm_wifi_ip_str(void) { return s_ip; }
