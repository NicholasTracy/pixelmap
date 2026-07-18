#include "wifi_mgr.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "wifi";
static bool s_sta_up;
static char s_ip[16] = "0.0.0.0";
static bool s_ap_fallback;
static char s_ssid[33];
static char s_pass[65];

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
    ESP_LOGI(TAG, "AP %s / pixelmap1", ap.ap.ssid);
    strncpy(s_ip, "192.168.4.1", sizeof(s_ip) - 1);
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_ssid[0]) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_sta_up = false;
        if (s_ap_fallback) start_ap();
        if (s_ssid[0]) esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        s_sta_up = true;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&e->ip_info.ip));
        ESP_LOGI(TAG, "STA IP %s", s_ip);
    }
}

esp_err_t pm_wifi_start(const pm_wifi_config_t *cfg)
{
    s_ap_fallback = cfg->ap_fallback;
    if (cfg->sta_ssid) strncpy(s_ssid, cfg->sta_ssid, sizeof(s_ssid) - 1);
    if (cfg->sta_pass) strncpy(s_pass, cfg->sta_pass, sizeof(s_pass) - 1);

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
    } else {
        start_ap();
    }
    if (cfg->hostname && cfg->hostname[0]) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) esp_netif_set_hostname(netif, cfg->hostname);
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    return ESP_OK;
}

bool pm_wifi_sta_connected(void) { return s_sta_up; }
const char *pm_wifi_ip_str(void) { return s_ip; }
