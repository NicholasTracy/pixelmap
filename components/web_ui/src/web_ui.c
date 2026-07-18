#include "web_ui.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_check.h"
#include "cJSON.h"
#include "led_chipsets.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "web_ui";
static httpd_handle_t s_server;
static pm_web_ui_hooks_t s_hooks;

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

static esp_err_t send_json(httpd_req_t *req, cJSON *obj)
{
    char *s = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!s) return ESP_ERR_NO_MEM;
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, s, strlen(s));
    free(s);
    return err;
}

static cJSON *read_body_json(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > 64 * 1024) return NULL;
    char *buf = malloc((size_t)total + 1);
    if (!buf) return NULL;
    int got = 0;
    while (got < total) {
        int r = httpd_req_recv(req, buf + got, total - got);
        if (r <= 0) {
            free(buf);
            return NULL;
        }
        got += r;
    }
    buf[total] = 0;
    cJSON *j = cJSON_Parse(buf);
    free(buf);
    return j;
}

static esp_err_t h_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)index_html_start,
                           index_html_end - index_html_start);
}

static esp_err_t h_get_config(httpd_req_t *req)
{
    pm_app_config_t *c = s_hooks.cfg;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "ssid", c->sta_ssid);
    cJSON_AddStringToObject(o, "pass", c->sta_pass);
    cJSON_AddStringToObject(o, "host", c->hostname);
    cJSON_AddNumberToObject(o, "gpio", c->gpio_data);
    cJSON_AddNumberToObject(o, "sled", c->gpio_status_led);
    cJSON_AddBoolToObject(o, "sledh", c->status_led_active_high);
    cJSON_AddNumberToObject(o, "count", c->pixel_count);
    cJSON_AddNumberToObject(o, "bri", c->brightness);
    cJSON_AddNumberToObject(o, "gamma", c->gamma);
    cJSON_AddStringToObject(o, "chip", pm_chipset_name(c->chipset));
    cJSON_AddNumberToObject(o, "fx", c->effect_id);
    cJSON_AddNumberToObject(o, "speed", c->effect_speed);
    cJSON_AddNumberToObject(o, "aun", c->artnet_universe);
    cJSON_AddNumberToObject(o, "sun", c->sacn_universe);
    cJSON_AddBoolToObject(o, "aen", c->artnet_enable);
    cJSON_AddBoolToObject(o, "sen", c->sacn_enable);
    cJSON_AddNumberToObject(o, "mw", c->map_width);
    cJSON_AddNumberToObject(o, "mh", c->map_height);
    cJSON_AddBoolToObject(o, "pove", c->pov_enable);
    cJSON_AddNumberToObject(o, "povm", c->pov_mode);
    cJSON_AddNumberToObject(o, "poyl", c->pov_layout);
    cJSON_AddNumberToObject(o, "povrpm", c->pov_rpm);
    cJSON_AddNumberToObject(o, "povspd", c->pov_linear_speed_mps);
    cJSON_AddNumberToObject(o, "povrad", c->pov_radius_m);
    cJSON_AddNumberToObject(o, "povpath", c->pov_path_length_m);
    return send_json(req, o);
}

static esp_err_t h_post_config(httpd_req_t *req)
{
    cJSON *j = read_body_json(req);
    if (!j) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
        return ESP_FAIL;
    }
    pm_app_config_t *c = s_hooks.cfg;
    cJSON *v;
    if ((v = cJSON_GetObjectItem(j, "ssid")) && cJSON_IsString(v))
        strncpy(c->sta_ssid, v->valuestring, sizeof(c->sta_ssid) - 1);
    if ((v = cJSON_GetObjectItem(j, "pass")) && cJSON_IsString(v))
        strncpy(c->sta_pass, v->valuestring, sizeof(c->sta_pass) - 1);
    if ((v = cJSON_GetObjectItem(j, "host")) && cJSON_IsString(v))
        strncpy(c->hostname, v->valuestring, sizeof(c->hostname) - 1);
    if ((v = cJSON_GetObjectItem(j, "gpio")) && cJSON_IsNumber(v)) c->gpio_data = (int)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "sled")) && cJSON_IsNumber(v)) c->gpio_status_led = (int)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "sledh"))) c->status_led_active_high = cJSON_IsTrue(v);
    if ((v = cJSON_GetObjectItem(j, "count")) && cJSON_IsNumber(v)) c->pixel_count = (uint16_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "bri")) && cJSON_IsNumber(v)) c->brightness = (uint8_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "gamma")) && cJSON_IsNumber(v)) c->gamma = (uint8_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "chip")) && cJSON_IsString(v)) c->chipset = pm_chipset_from_name(v->valuestring);
    if ((v = cJSON_GetObjectItem(j, "fx")) && cJSON_IsNumber(v)) c->effect_id = (pm_effect_id_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "speed")) && cJSON_IsNumber(v)) c->effect_speed = (float)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "aun")) && cJSON_IsNumber(v)) c->artnet_universe = (uint16_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "sun")) && cJSON_IsNumber(v)) c->sacn_universe = (uint16_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "aen"))) c->artnet_enable = cJSON_IsTrue(v);
    if ((v = cJSON_GetObjectItem(j, "sen"))) c->sacn_enable = cJSON_IsTrue(v);
    if ((v = cJSON_GetObjectItem(j, "mw")) && cJSON_IsNumber(v)) c->map_width = (uint16_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "mh")) && cJSON_IsNumber(v)) c->map_height = (uint16_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "pove"))) c->pov_enable = cJSON_IsTrue(v);
    if ((v = cJSON_GetObjectItem(j, "povm")) && cJSON_IsNumber(v)) c->pov_mode = (pm_pov_mode_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "poyl")) && cJSON_IsNumber(v)) c->pov_layout = (pm_pov_layout_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "povrpm")) && cJSON_IsNumber(v)) c->pov_rpm = (float)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "povspd")) && cJSON_IsNumber(v)) c->pov_linear_speed_mps = (float)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "povrad")) && cJSON_IsNumber(v)) c->pov_radius_m = (float)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "povpath")) && cJSON_IsNumber(v)) c->pov_path_length_m = (float)v->valuedouble;
    cJSON_Delete(j);

    pm_config_save(c);
    if (s_hooks.on_config_changed) s_hooks.on_config_changed();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t h_get_map(httpd_req_t *req)
{
    size_t need = (size_t)pm_pixel_map_count(s_hooks.map) * 64 + 4;
    char *buf = malloc(need);
    if (!buf) return ESP_ERR_NO_MEM;
    size_t out = 0;
    esp_err_t err = pm_pixel_map_export_json(s_hooks.map, buf, need, &out);
    if (err != ESP_OK) {
        free(buf);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "export");
        return err;
    }
    httpd_resp_set_type(req, "application/json");
    err = httpd_resp_send(req, buf, out);
    free(buf);
    return err;
}

static esp_err_t h_post_map(httpd_req_t *req)
{
    cJSON *j = read_body_json(req);
    if (!j) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
        return ESP_FAIL;
    }
    char *printed = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!printed) return ESP_ERR_NO_MEM;
    esp_err_t err = pm_pixel_map_import_json(s_hooks.map, printed);
    free(printed);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "import");
        return err;
    }
    if (s_hooks.on_map_changed) s_hooks.on_map_changed();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t h_post_grid(httpd_req_t *req)
{
    cJSON *j = read_body_json(req);
    if (!j) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
        return ESP_FAIL;
    }
    int w = cJSON_GetObjectItem(j, "w") ? (int)cJSON_GetObjectItem(j, "w")->valuedouble : 10;
    int h = cJSON_GetObjectItem(j, "h") ? (int)cJSON_GetObjectItem(j, "h")->valuedouble : 6;
    cJSON_Delete(j);
    esp_err_t err = pm_pixel_map_build_grid(s_hooks.map, (uint16_t)w, (uint16_t)h, 1.0f, 0);
    if (err == ESP_OK) {
        pm_pixel_map_normalize(s_hooks.map);
        s_hooks.cfg->map_width = (uint16_t)w;
        s_hooks.cfg->map_height = (uint16_t)h;
        s_hooks.cfg->pixel_count = (uint16_t)(w * h);
        pm_config_save(s_hooks.cfg);
        if (s_hooks.on_map_changed) s_hooks.on_map_changed();
        if (s_hooks.on_config_changed) s_hooks.on_config_changed();
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, err == ESP_OK ? "{\"ok\":true}" : "{\"ok\":false}");
}

esp_err_t pm_web_ui_start(const pm_web_ui_hooks_t *hooks)
{
    if (!hooks || !hooks->cfg || !hooks->map) return ESP_ERR_INVALID_ARG;
    s_hooks = *hooks;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12;
    config.stack_size = 8192;
    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), TAG, "httpd");

    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = h_index},
        {.uri = "/api/config", .method = HTTP_GET, .handler = h_get_config},
        {.uri = "/api/config", .method = HTTP_POST, .handler = h_post_config},
        {.uri = "/api/map", .method = HTTP_GET, .handler = h_get_map},
        {.uri = "/api/map", .method = HTTP_POST, .handler = h_post_map},
        {.uri = "/api/map/grid", .method = HTTP_POST, .handler = h_post_grid},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        httpd_register_uri_handler(s_server, &routes[i]);
    }
    ESP_LOGI(TAG, "UI on http://%s/", "0.0.0.0");
    return ESP_OK;
}

void pm_web_ui_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
