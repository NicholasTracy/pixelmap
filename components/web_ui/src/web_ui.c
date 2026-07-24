#include "web_ui.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "led_chipsets.h"
#include "color_engine.h"
#include "pm_version.h"
#include "pov.h"
#include "effect_lua.h"
#include "ota_update.h"
#include "presets.h"
#include "map_store.h"
#include "audio.h"
#include "wifi_mgr.h"
#include "pm_security.h"
#include "esp_random.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

static const char *TAG = "web_ui";
static httpd_handle_t s_server;
static pm_web_ui_hooks_t s_hooks;
static char s_session[33];
static int64_t s_session_at_ms;
static int s_auth_fails;
static int64_t s_auth_lock_until_ms;

#define PM_SESSION_TTL_MS (8LL * 60 * 60 * 1000)
#define PM_AUTH_MAX_FAILS 5
#define PM_AUTH_LOCK_MS (60LL * 1000)

static const char *LOGIN_HTML =
    "<!DOCTYPE html><html><head><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>PixelMap Login</title><link rel=icon href=/logo.svg type=image/svg+xml><style>"
    "body{font-family:system-ui,sans-serif;background:#1a1d21;color:#e8eaed;display:flex;min-height:100vh;align-items:center;justify-content:center;margin:0}"
    ".card{background:#23272c;padding:1.5rem;border-radius:12px;width:min(22rem,92vw)}"
    ".brand{display:flex;align-items:center;gap:.65rem;margin:0 0 .5rem}"
    ".brand img{width:2.5rem;height:2.5rem}h1{font-size:1.25rem;margin:0}p{color:#9aa0a6;font-size:.9rem;margin:0 0 1rem}"
    "input{width:100%;box-sizing:border-box;padding:.65rem .75rem;border-radius:8px;border:1px solid #3c4043;background:#121417;color:#e8eaed}"
    "button{margin-top:.85rem;width:100%;padding:.7rem;border:0;border-radius:8px;background:#3d6b8c;color:#fff;font-weight:600;cursor:pointer}"
    ".err{color:#f28b82;font-size:.85rem;margin-top:.6rem;min-height:1.2em}"
    "</style></head><body><div class=card><div class=brand><img src=/logo.svg alt=\"\"><h1>PixelMap</h1></div>"
    "<p>Enter the web UI password to continue.</p>"
    "<input id=p type=password autocomplete=current-password placeholder=\"Password\">"
    "<button id=b type=button>Sign in</button><div class=err id=e></div></div>"
    "<script>async function go(){e.textContent='';const r=await fetch('/api/auth',{method:'POST',credentials:'same-origin',"
    "headers:{'Content-Type':'application/json'},body:JSON.stringify({pass:p.value})});"
    "const j=await r.json().catch(()=>({}));if(r.ok&&j.ok){try{sessionStorage.setItem('pm_auth',j.token)}catch(_){}"
    "location.href='/'}else e.textContent=j.error||'Login failed'}b.onclick=go;p.onkeydown=ev=>{if(ev.key==='Enter')go()}</script></body></html>";

/* Compact first-boot SoftAP wizard (no serial required). */
static const char *SETUP_HTML =
    "<!DOCTYPE html><html><head><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>PixelMap Setup</title><link rel=icon href=/logo.svg type=image/svg+xml><style>"
    "body{font-family:system-ui,sans-serif;background:#1a1d21;color:#e8eaed;margin:0;padding:1rem}"
    ".wrap{max-width:28rem;margin:0 auto}.brand{display:flex;align-items:center;gap:.65rem;margin:0 0 .35rem}"
    ".brand img{width:2.5rem;height:2.5rem}h1{font-size:1.35rem;margin:0}"
    "p,.hint{color:#9aa0a6;font-size:.9rem;line-height:1.4}label{display:block;margin:.75rem 0 .3rem;font-size:.85rem}"
    "input[type=text],input[type=password],select{width:100%;box-sizing:border-box;padding:.65rem .75rem;border-radius:8px;"
    "border:1px solid #3c4043;background:#121417;color:#e8eaed}"
    ".row{display:flex;gap:.5rem;align-items:center;margin:.5rem 0}.row input{width:auto}"
    "button{margin-top:.6rem;padding:.7rem 1rem;border:0;border-radius:8px;background:#3d6b8c;color:#fff;font-weight:600;cursor:pointer;width:100%}"
    "button.sec{background:#3c4043}button:disabled{opacity:.5}.err{color:#f28b82;min-height:1.2em;margin-top:.5rem}"
    ".warn{background:#3d2e12;border:1px solid #8a6d1a;color:#f0d78c;padding:.65rem .75rem;border-radius:8px;font-size:.85rem;margin:.75rem 0}"
    ".step{display:none}.step.on{display:block}.scan{max-height:10rem;overflow:auto;margin:.5rem 0}"
    ".scan button{width:100%;text-align:left;background:#2a2f36;margin:.25rem 0;font-weight:500}"
    "</style></head><body><div class=wrap>"
    "<div class=brand><img src=/logo.svg alt=\"\"><h1>PixelMap setup</h1></div>"
    "<p class=hint>You are on the SoftAP portal. Complete this wizard to finish first-time setup.</p>"
    "<div id=s1 class='step on'><p>1 — Join your Wi‑Fi (optional — you can stay on SoftAP only).</p>"
    "<button type=button class=sec id=scanBtn>Scan networks</button><div class=scan id=scanBox></div>"
    "<label>Network SSID</label><input id=ssid list=ssidList><datalist id=ssidList></datalist>"
    "<label>Wi‑Fi password</label><input id=pass type=password autocomplete=new-password>"
    "<label>Hostname</label><input id=host value=pixelmap>"
    "<button type=button id=n1>Next</button></div>"
    "<div id=s2 class=step><p>2 — SoftAP options</p>"
    "<div class=row><input type=checkbox id=apen checked><label for=apen style=margin:0>Keep SoftAP on with Wi‑Fi (APSTA)</label></div>"
    "<div class=row><input type=checkbox id=apfb><label for=apfb style=margin:0>SoftAP fallback if Wi‑Fi drops</label></div>"
    "<label>SoftAP SSID (blank = PixelMap-XXXX)</label><input id=apssid>"
    "<label>SoftAP password</label><input id=appass type=password placeholder=\"Leave blank to keep pixelmap1\" autocomplete=new-password>"
    "<div class=warn>Default SoftAP password is <b>pixelmap1</b>. Change it for shared venues (min 12 chars).</div>"
    "<button type=button class=sec id=b2>Back</button><button type=button id=n2>Next</button></div>"
    "<div id=s3 class=step><p>3 — Web UI access</p>"
    "<label>Web UI password (min 12) — leave blank for open UI</label>"
    "<input id=webpass type=password autocomplete=new-password>"
    "<div class=row><input type=checkbox id=openAck><label for=openAck style=margin:0>I understand open UI means anyone on the network can change settings / OTA</label></div>"
    "<div class=warn>Recommended: set a password. Open UI is fine for trusted private networks only.</div>"
    "<button type=button class=sec id=b3>Back</button><button type=button id=finish>Finish setup</button></div>"
    "<div class=err id=e></div></div><script>"
    "const $=id=>document.getElementById(id);const show=n=>{[1,2,3].forEach(i=>$('s'+i).classList.toggle('on',i===n))};"
    "$('n1').onclick=()=>show(2);$('b2').onclick=()=>show(1);$('n2').onclick=()=>show(3);$('b3').onclick=()=>show(2);"
    "$('scanBtn').onclick=async()=>{e.textContent='Scanning…';const r=await fetch('/api/setup/scan');const j=await r.json();e.textContent='';"
    "scanBox.innerHTML='';ssidList.innerHTML='';(j.aps||[]).forEach(a=>{const o=document.createElement('option');o.value=a.ssid;ssidList.appendChild(o);"
    "const b=document.createElement('button');b.type='button';b.textContent=a.ssid+' ('+a.rssi+' dBm)';b.onclick=()=>ssid.value=a.ssid;scanBox.appendChild(b)})};"
    "$('finish').onclick=async()=>{e.textContent='';const web=webpass.value;const open=!web;"
    "if(open&&!openAck.checked){e.textContent='Check the open-UI acknowledgement, or set a password.';return}"
    "if(web&&web.length<12){e.textContent='Web password must be at least 12 characters.';return}"
    "if(appass.value&&appass.value.length<12){e.textContent='SoftAP password must be at least 12 characters (or leave blank).';return}"
    "const body={ssid:ssid.value.trim(),pass:pass.value,host:host.value.trim()||'pixelmap',"
    "apen:apen.checked,apfb:apfb.checked,apssid:apssid.value.trim(),appass:appass.value,"
    "webpass:web,openUi:open,ackOpen:openAck.checked,ackDefaultAp:!appass.value};"
    "const r=await fetch('/api/setup/complete',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});"
    "const j=await r.json().catch(()=>({}));if(!r.ok||!j.ok){e.textContent=j.error||'Setup failed';return}"
    "if(j.token)try{sessionStorage.setItem('pm_auth',j.token)}catch(_){}"
    "e.textContent='Saved — opening UI…';location.href='/'};"
    "</script></body></html>";

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
/* ESP-IDF EMBED_TXTFILES symbols use the basename only (not the vendor/ path). */
extern const uint8_t bootstrap_css_start[] asm("_binary_bootstrap_min_css_start");
extern const uint8_t bootstrap_css_end[] asm("_binary_bootstrap_min_css_end");
extern const uint8_t bootstrap_js_start[] asm("_binary_bootstrap_bundle_min_js_start");
extern const uint8_t bootstrap_js_end[] asm("_binary_bootstrap_bundle_min_js_end");
extern const uint8_t pixelmap_logo_svg_start[] asm("_binary_PixelMapLogo_svg_start");
extern const uint8_t pixelmap_logo_svg_end[] asm("_binary_PixelMapLogo_svg_end");

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

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static const char *stored_web_secret(void)
{
    pm_app_config_t *c = s_hooks.cfg;
    if (!c) return "";
    if (c->web_pass[0]) return c->web_pass;
    return c->ui_pin;
}

static bool setup_done(void)
{
    return s_hooks.cfg && s_hooks.cfg->setup_complete;
}

/** Auth required only when enabled and a password hash is stored. */
static bool web_auth_enabled(void)
{
    pm_app_config_t *c = s_hooks.cfg;
    return c && c->web_auth && stored_web_secret()[0] != '\0';
}

static bool extract_cookie_value(const char *cookie, const char *name, char *out, size_t out_len)
{
    if (!cookie || !name || !out || out_len < 2) return false;
    out[0] = 0;
    size_t nlen = strlen(name);
    const char *p = cookie;
    while (p && *p) {
        while (*p == ' ' || *p == ';') p++;
        if (strncmp(p, name, nlen) == 0 && p[nlen] == '=') {
            p += nlen + 1;
            size_t i = 0;
            while (p[i] && p[i] != ';' && i + 1 < out_len) {
                out[i] = p[i];
                i++;
            }
            out[i] = 0;
            return out[0] != 0;
        }
        p = strchr(p, ';');
    }
    return false;
}

static bool extract_session_token(httpd_req_t *req, char *out, size_t out_len)
{
    if (!out || out_len < 2) return false;
    out[0] = 0;
    if (httpd_req_get_hdr_value_str(req, "X-PixelMap-Auth", out, out_len) == ESP_OK && out[0]) {
        return true;
    }
    char cookie[192] = {0};
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie, sizeof(cookie)) != ESP_OK) return false;
    return extract_cookie_value(cookie, "pm_sess", out, out_len);
}

static bool session_ok(httpd_req_t *req)
{
    if (!web_auth_enabled()) return true; /* open UI */
    if (!s_session[0]) return false;
    if (now_ms() - s_session_at_ms > PM_SESSION_TTL_MS) {
        s_session[0] = '\0';
        return false;
    }
    char tok[40] = {0};
    if (!extract_session_token(req, tok, sizeof(tok))) return false;
    size_t n = strlen(s_session);
    if (strlen(tok) != n) return false;
    if (!pm_security_ct_eq(tok, s_session, n)) return false;
    s_session_at_ms = now_ms(); /* sliding idle window */
    return true;
}

static bool auth_ok(httpd_req_t *req, bool mutating)
{
    (void)mutating;
    if (!setup_done()) return false; /* only setup routes while wizard pending */
    return session_ok(req);
}

static bool pin_ok(httpd_req_t *req) { return auth_ok(req, true); }

static esp_err_t reject_pin(httpd_req_t *req)
{
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"auth required\"}");
    return ESP_FAIL;
}

static esp_err_t reject_auth_page(httpd_req_t *req)
{
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, LOGIN_HTML, HTTPD_RESP_USE_STRLEN);
}

static void mint_session(char *out, size_t out_len)
{
    uint8_t raw[16];
    esp_fill_random(raw, sizeof(raw));
    static const char *hex = "0123456789abcdef";
    size_t n = (out_len - 1) / 2;
    if (n > sizeof(raw)) n = sizeof(raw);
    for (size_t i = 0; i < n; ++i) {
        out[i * 2] = hex[(raw[i] >> 4) & 0xf];
        out[i * 2 + 1] = hex[raw[i] & 0xf];
    }
    out[n * 2] = 0;
    s_session_at_ms = now_ms();
}

static void set_session_cookie(httpd_req_t *req, const char *token)
{
    char cookie[128];
    if (token && token[0]) {
        snprintf(cookie, sizeof(cookie),
                 "pm_sess=%s; Path=/; Max-Age=28800; HttpOnly; SameSite=Lax", token);
    } else {
        snprintf(cookie, sizeof(cookie), "pm_sess=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax");
    }
    httpd_resp_set_hdr(req, "Set-Cookie", cookie);
}

static void reboot_soon_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(600));
    esp_restart();
}

static void schedule_reboot(void)
{
    xTaskCreate(reboot_soon_task, "reboot", 2048, NULL, 5, NULL);
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
    if (!setup_done()) {
        httpd_resp_set_type(req, "text/html");
        return httpd_resp_send(req, SETUP_HTML, HTTPD_RESP_USE_STRLEN);
    }
    if (web_auth_enabled() && !session_ok(req)) {
        return reject_auth_page(req);
    }
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)index_html_start,
                           index_html_end - index_html_start);
}

static esp_err_t h_bootstrap_css(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    return httpd_resp_send(req, (const char *)bootstrap_css_start,
                           bootstrap_css_end - bootstrap_css_start);
}

static esp_err_t h_bootstrap_js(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    return httpd_resp_send(req, (const char *)bootstrap_js_start,
                           bootstrap_js_end - bootstrap_js_start);
}

static esp_err_t h_logo_svg(httpd_req_t *req)
{
    httpd_resp_set_type(req, "image/svg+xml");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    return httpd_resp_send(req, (const char *)pixelmap_logo_svg_start,
                           pixelmap_logo_svg_end - pixelmap_logo_svg_start);
}

static esp_err_t h_get_config(httpd_req_t *req)
{
    if (!auth_ok(req, false)) return reject_pin(req);
    pm_app_config_t *c = s_hooks.cfg;
    pm_config_lock();
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "ver", PIXELMAP_VERSION);
    cJSON_AddStringToObject(o, "ssid", c->sta_ssid);
    /* Never echo Wi‑Fi password; UI only sends a new pass when the user types one. */
    cJSON_AddBoolToObject(o, "passSet", c->sta_pass[0] != '\0');
    cJSON_AddBoolToObject(o, "setupComplete", c->setup_complete);
    cJSON_AddBoolToObject(o, "webAuth", c->web_auth && stored_web_secret()[0] != '\0');
    cJSON_AddBoolToObject(o, "webPassSet", stored_web_secret()[0] != '\0');
    cJSON_AddBoolToObject(o, "webPassRotate", c->web_pass_rotate);
    cJSON_AddBoolToObject(o, "pinSet", stored_web_secret()[0] != '\0'); /* legacy alias */
    cJSON_AddNumberToObject(o, "webPassMin", PM_SECURITY_PASS_MIN);
    cJSON_AddBoolToObject(o, "apDefaultPass", strcmp(c->ap_pass, PM_SETUP_AP_PASS) == 0);
    cJSON_AddBoolToObject(o, "apen", c->ap_enable);
    cJSON_AddBoolToObject(o, "apfb", c->ap_fallback);
    cJSON_AddStringToObject(o, "apssid", c->ap_ssid);
    cJSON_AddBoolToObject(o, "apPassSet", c->ap_pass[0] != '\0');
    cJSON_AddStringToObject(o, "host", c->hostname);
    cJSON_AddStringToObject(o, "otaPart", pm_ota_running_label());
    cJSON_AddNumberToObject(o, "maled", c->ma_per_led);
    cJSON_AddNumberToObject(o, "sminp", c->sacn_min_priority);
    cJSON_AddBoolToObject(o, "auden", c->audio_enable);
    cJSON_AddNumberToObject(o, "audws", c->audio_gpio_ws);
    cJSON_AddNumberToObject(o, "audck", c->audio_gpio_sck);
    cJSON_AddNumberToObject(o, "audsd", c->audio_gpio_sd);
    cJSON_AddNumberToObject(o, "audgn", c->audio_gain);
    cJSON_AddNumberToObject(o, "audsq", c->audio_squelch);
    cJSON_AddBoolToObject(o, "audmod", c->audio_modulate);
    cJSON_AddNumberToObject(o, "gpio", c->gpio_data);
    cJSON_AddNumberToObject(o, "clk", c->gpio_clock);
    cJSON_AddNumberToObject(o, "sled", c->gpio_status_led);
    cJSON_AddBoolToObject(o, "sledh", c->status_led_active_high);
    cJSON_AddNumberToObject(o, "count", c->pixel_count);
    cJSON_AddNumberToObject(o, "scnt", c->strip_count);
    {
        cJSON *slens = cJSON_CreateArray();
        cJSON *sgpios = cJSON_CreateArray();
        for (int i = 0; i < (int)c->strip_count && i < PM_STRIP_MAX; ++i) {
            cJSON_AddItemToArray(slens, cJSON_CreateNumber(c->strip_len[i]));
            cJSON_AddItemToArray(sgpios, cJSON_CreateNumber(c->strip_gpio[i]));
        }
        cJSON_AddItemToObject(o, "slens", slens);
        cJSON_AddItemToObject(o, "sgpios", sgpios);
    }
    cJSON_AddNumberToObject(o, "bri", c->brightness);
    cJSON_AddNumberToObject(o, "gamma", c->gamma);
    cJSON_AddBoolToObject(o, "aw", c->auto_white);
    cJSON_AddStringToObject(o, "chip", pm_chipset_name(c->chipset));
    cJSON_AddStringToObject(o, "order", pm_color_order_name(c->color_order));
    cJSON_AddNumberToObject(o, "fx", c->effect_id);
    cJSON_AddNumberToObject(o, "speed", c->effect_speed);
    cJSON_AddNumberToObject(o, "scale", c->effect_scale);
    cJSON_AddNumberToObject(o, "fxint", c->effect_intensity);
    cJSON_AddNumberToObject(o, "ph", c->effect_primary_h);
    cJSON_AddNumberToObject(o, "ps", c->effect_primary_s);
    cJSON_AddNumberToObject(o, "pv", c->effect_primary_v);
    cJSON_AddNumberToObject(o, "sh", c->effect_secondary_h);
    cJSON_AddNumberToObject(o, "ss", c->effect_secondary_s);
    cJSON_AddNumberToObject(o, "sv", c->effect_secondary_v);
    {
        cJSON *fp = cJSON_CreateArray();
        cJSON *fpos = cJSON_CreateArray();
        cJSON *frot = cJSON_CreateArray();
        cJSON *fch = cJSON_CreateArray();
        cJSON *fmod = cJSON_CreateArray();
        for (int i = 0; i < PM_FX_P_COUNT; ++i) {
            cJSON_AddItemToArray(fp, cJSON_CreateNumber(c->effect_p[i]));
        }
        for (int i = 0; i < 3; ++i) {
            cJSON_AddItemToArray(fpos, cJSON_CreateNumber(c->effect_pos[i]));
            cJSON_AddItemToArray(frot, cJSON_CreateNumber(c->effect_rot[i]));
        }
        for (int i = 0; i < PM_FXPARAM_COUNT; ++i) {
            cJSON_AddItemToArray(fch, cJSON_CreateNumber(c->effect_param_ch[i]));
            cJSON *mo = cJSON_CreateObject();
            cJSON_AddNumberToObject(mo, "shape", c->effect_mod[i].shape);
            cJSON_AddNumberToObject(mo, "depth", c->effect_mod[i].depth);
            cJSON_AddNumberToObject(mo, "rate", c->effect_mod[i].rate);
            cJSON_AddNumberToObject(mo, "phase", c->effect_mod[i].phase);
            cJSON_AddItemToArray(fmod, mo);
        }
        cJSON_AddItemToObject(o, "fxp", fp);
        cJSON_AddItemToObject(o, "fxpos", fpos);
        cJSON_AddItemToObject(o, "fxrot", frot);
        cJSON_AddItemToObject(o, "fxch", fch);
        cJSON_AddItemToObject(o, "fxmod", fmod);
    }
    cJSON_AddNumberToObject(o, "dmxmode", (int)c->dmx_mode);
    cJSON_AddNumberToObject(o, "fxmask", (double)pm_effect_param_mask(c->effect_id));
    cJSON_AddNumberToObject(o, "aun", c->artnet_universe);
    cJSON_AddNumberToObject(o, "sun", c->sacn_universe);
    cJSON_AddNumberToObject(o, "ucnt", c->universe_count);
    cJSON_AddBoolToObject(o, "aen", c->artnet_enable);
    cJSON_AddBoolToObject(o, "sen", c->sacn_enable);
    cJSON_AddNumberToObject(o, "mw", c->map_width);
    cJSON_AddNumberToObject(o, "mh", c->map_height);
    cJSON_AddNumberToObject(o, "md", c->map_depth);
    cJSON_AddNumberToObject(o, "mdim", c->map_dim);
    cJSON_AddNumberToObject(o, "mlay", c->map_layout);
    cJSON_AddNumberToObject(o, "mfill", c->map_fill);
    cJSON_AddBoolToObject(o, "mopentb", c->map_open_tb);
    cJSON_AddNumberToObject(o, "mspc", c->map_spacing);
    cJSON_AddBoolToObject(o, "pove", c->pov_enable);
    cJSON_AddNumberToObject(o, "povm", c->pov_mode);
    cJSON_AddNumberToObject(o, "poyl", c->pov_layout);
    cJSON_AddNumberToObject(o, "povbl", c->pov_blade_count);
    cJSON_AddNumberToObject(o, "povrpm", c->pov_rpm);
    cJSON_AddNumberToObject(o, "povspd", c->pov_linear_speed_mps);
    cJSON_AddNumberToObject(o, "povrad", c->pov_radius_m);
    cJSON_AddNumberToObject(o, "povpath", c->pov_path_length_m);
    pm_config_unlock();
    return send_json(req, o);
}

static esp_err_t h_post_config(httpd_req_t *req)
{
    if (!pin_ok(req)) return reject_pin(req);
    cJSON *j = read_body_json(req);
    if (!j) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
        return ESP_FAIL;
    }
    pm_app_config_t *c = s_hooks.cfg;
    pm_config_lock();
    cJSON *v;
    if ((v = cJSON_GetObjectItem(j, "ssid")) && cJSON_IsString(v))
        strncpy(c->sta_ssid, v->valuestring, sizeof(c->sta_ssid) - 1);
    /* Empty / omitted pass keeps the previously stored password. */
    if ((v = cJSON_GetObjectItem(j, "pass")) && cJSON_IsString(v) && v->valuestring[0] != '\0') {
        strncpy(c->sta_pass, v->valuestring, sizeof(c->sta_pass) - 1);
        c->sta_pass[sizeof(c->sta_pass) - 1] = '\0';
    }
    if ((v = cJSON_GetObjectItem(j, "host")) && cJSON_IsString(v))
        strncpy(c->hostname, v->valuestring, sizeof(c->hostname) - 1);
    if ((v = cJSON_GetObjectItem(j, "apen"))) c->ap_enable = cJSON_IsTrue(v);
    if ((v = cJSON_GetObjectItem(j, "apfb"))) c->ap_fallback = cJSON_IsTrue(v);
    if ((v = cJSON_GetObjectItem(j, "apssid")) && cJSON_IsString(v)) {
        strncpy(c->ap_ssid, v->valuestring, sizeof(c->ap_ssid) - 1);
        c->ap_ssid[sizeof(c->ap_ssid) - 1] = '\0';
    }
    if ((v = cJSON_GetObjectItem(j, "appass")) && cJSON_IsString(v) && v->valuestring[0] != '\0') {
        if (!pm_security_password_ok(v->valuestring) || pm_security_is_insecure_ap_pass(v->valuestring)) {
            pm_config_unlock();
            cJSON_Delete(j);
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_set_type(req, "application/json");
            return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"AP password must be at least 12 characters\"}");
        }
        strncpy(c->ap_pass, v->valuestring, sizeof(c->ap_pass) - 1);
        c->ap_pass[sizeof(c->ap_pass) - 1] = '\0';
    }
    bool auth_changed = false;
    if ((v = cJSON_GetObjectItem(j, "webAuth"))) {
        bool want = cJSON_IsTrue(v);
        if (want != c->web_auth) auth_changed = true;
        c->web_auth = want;
    }
    if ((v = cJSON_GetObjectItem(j, "webpass")) && cJSON_IsString(v)) {
        if (v->valuestring[0] == '\0') {
            /* Blank password clears auth (open UI). */
            memset(c->web_pass, 0, sizeof(c->web_pass));
            c->ui_pin[0] = '\0';
            c->web_auth = false;
            c->web_pass_rotate = false;
            s_session[0] = '\0';
            auth_changed = true;
        } else {
            if (!pm_security_password_ok(v->valuestring)) {
                pm_config_unlock();
                cJSON_Delete(j);
                httpd_resp_set_status(req, "400 Bad Request");
                httpd_resp_set_type(req, "application/json");
                return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"web password must be at least 12 characters\"}");
            }
            char hash[PM_SECURITY_HASH_MAX];
            if (pm_security_hash_password(v->valuestring, hash, sizeof(hash)) != ESP_OK) {
                pm_config_unlock();
                cJSON_Delete(j);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "hash failed");
                return ESP_FAIL;
            }
            strncpy(c->web_pass, hash, sizeof(c->web_pass) - 1);
            c->web_pass[sizeof(c->web_pass) - 1] = '\0';
            c->ui_pin[0] = '\0';
            c->web_auth = true;
            c->web_pass_rotate = false;
            auth_changed = true;
        }
    }
    if (c->web_auth && !c->web_pass[0]) {
        pm_config_unlock();
        cJSON_Delete(j);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"set a web password or disable web auth\"}");
    }
    if ((v = cJSON_GetObjectItem(j, "gpio")) && cJSON_IsNumber(v)) {
        c->gpio_data = (int)v->valuedouble;
        c->strip_gpio[0] = c->gpio_data;
    }
    if ((v = cJSON_GetObjectItem(j, "clk")) && cJSON_IsNumber(v)) c->gpio_clock = (int)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "sled")) && cJSON_IsNumber(v)) c->gpio_status_led = (int)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "sledh"))) c->status_led_active_high = cJSON_IsTrue(v);
    if ((v = cJSON_GetObjectItem(j, "count")) && cJSON_IsNumber(v)) c->pixel_count = (uint16_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "scnt")) && cJSON_IsNumber(v)) c->strip_count = (uint8_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "slens")) && cJSON_IsArray(v)) {
        int n = cJSON_GetArraySize(v);
        if (n > PM_STRIP_MAX) n = PM_STRIP_MAX;
        if (n > 0) c->strip_count = (uint8_t)n;
        for (int i = 0; i < PM_STRIP_MAX; ++i) {
            if (i < n) {
                cJSON *el = cJSON_GetArrayItem(v, i);
                c->strip_len[i] = (el && cJSON_IsNumber(el)) ? (uint16_t)el->valuedouble : 1;
            } else {
                c->strip_len[i] = 0;
            }
        }
    }
    if ((v = cJSON_GetObjectItem(j, "sgpios")) && cJSON_IsArray(v)) {
        int n = cJSON_GetArraySize(v);
        if (n > PM_STRIP_MAX) n = PM_STRIP_MAX;
        for (int i = 0; i < n; ++i) {
            cJSON *el = cJSON_GetArrayItem(v, i);
            if (el && cJSON_IsNumber(el)) c->strip_gpio[i] = (int)el->valuedouble;
        }
    }
    pm_config_sync_strips(c);
    if ((v = cJSON_GetObjectItem(j, "bri")) && cJSON_IsNumber(v)) c->brightness = (uint8_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "gamma")) && cJSON_IsNumber(v)) c->gamma = (uint8_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "aw"))) c->auto_white = cJSON_IsTrue(v);
    if ((v = cJSON_GetObjectItem(j, "chip")) && cJSON_IsString(v)) {
        c->chipset = pm_chipset_from_name(v->valuestring);
        if (c->chipset == PM_CHIPSET_CUSTOM) {
            c->chipset = PM_CHIPSET_WS2812B;
        }
    }
    if ((v = cJSON_GetObjectItem(j, "order")) && cJSON_IsString(v))
        c->color_order = pm_color_order_from_name(v->valuestring);
    if ((v = cJSON_GetObjectItem(j, "fx")) && cJSON_IsNumber(v)) {
        int fx = (int)v->valuedouble;
        c->effect_id = (fx >= 0 && fx < (int)PM_EFFECT_COUNT) ? (pm_effect_id_t)fx : PM_EFFECT_RAINBOW_SPATIAL;
    }
    if ((v = cJSON_GetObjectItem(j, "speed")) && cJSON_IsNumber(v)) c->effect_speed = (float)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "scale")) && cJSON_IsNumber(v)) {
        c->effect_scale = (float)v->valuedouble;
        if (c->effect_scale < 1e-4f) c->effect_scale = 1.0f;
    }
    if ((v = cJSON_GetObjectItem(j, "fxint")) && cJSON_IsNumber(v)) {
        int iv = (int)v->valuedouble;
        if (iv < 0) iv = 0;
        if (iv > 255) iv = 255;
        c->effect_intensity = (uint8_t)iv;
    }
    if ((v = cJSON_GetObjectItem(j, "ph")) && cJSON_IsNumber(v)) c->effect_primary_h = (uint8_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "ps")) && cJSON_IsNumber(v)) c->effect_primary_s = (uint8_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "pv")) && cJSON_IsNumber(v)) c->effect_primary_v = (uint8_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "sh")) && cJSON_IsNumber(v)) c->effect_secondary_h = (uint8_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "ss")) && cJSON_IsNumber(v)) c->effect_secondary_s = (uint8_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "sv")) && cJSON_IsNumber(v)) c->effect_secondary_v = (uint8_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "fxp")) && cJSON_IsArray(v)) {
        int n = cJSON_GetArraySize(v);
        if (n > PM_FX_P_COUNT) n = PM_FX_P_COUNT;
        for (int i = 0; i < n; ++i) {
            cJSON *el = cJSON_GetArrayItem(v, i);
            if (el && cJSON_IsNumber(el)) {
                int iv = (int)el->valuedouble;
                if (iv < 0) iv = 0;
                if (iv > 255) iv = 255;
                c->effect_p[i] = (uint8_t)iv;
            }
        }
    }
    if ((v = cJSON_GetObjectItem(j, "fxpos")) && cJSON_IsArray(v)) {
        int n = cJSON_GetArraySize(v);
        if (n > 3) n = 3;
        for (int i = 0; i < n; ++i) {
            cJSON *el = cJSON_GetArrayItem(v, i);
            if (el && cJSON_IsNumber(el)) {
                int iv = (int)el->valuedouble;
                if (iv < 0) iv = 0;
                if (iv > 255) iv = 255;
                c->effect_pos[i] = (uint8_t)iv;
            }
        }
    }
    if ((v = cJSON_GetObjectItem(j, "fxrot")) && cJSON_IsArray(v)) {
        int n = cJSON_GetArraySize(v);
        if (n > 3) n = 3;
        for (int i = 0; i < n; ++i) {
            cJSON *el = cJSON_GetArrayItem(v, i);
            if (el && cJSON_IsNumber(el)) {
                int iv = (int)el->valuedouble;
                if (iv < 0) iv = 0;
                if (iv > 255) iv = 255;
                c->effect_rot[i] = (uint8_t)iv;
            }
        }
    }
    /* Effect param DMX channels are fixed (1..23); ignore client overrides */
    for (int i = 0; i < PM_FXPARAM_COUNT; ++i) {
        c->effect_param_ch[i] = (uint16_t)(i + 1);
    }
    if ((v = cJSON_GetObjectItem(j, "fxmod")) && cJSON_IsArray(v)) {
        int n = cJSON_GetArraySize(v);
        if (n > PM_FXPARAM_COUNT) n = PM_FXPARAM_COUNT;
        for (int i = 0; i < n; ++i) {
            cJSON *el = cJSON_GetArrayItem(v, i);
            if (!el || !cJSON_IsObject(el)) continue;
            cJSON *s = cJSON_GetObjectItem(el, "shape");
            cJSON *d = cJSON_GetObjectItem(el, "depth");
            cJSON *r = cJSON_GetObjectItem(el, "rate");
            cJSON *p = cJSON_GetObjectItem(el, "phase");
            int shape = (s && cJSON_IsNumber(s)) ? (int)s->valuedouble : 0;
            if (shape < 0) shape = 0;
            if (shape > (int)PM_FXMOD_NOISE) shape = (int)PM_FXMOD_OFF;
            c->effect_mod[i].shape = (uint8_t)shape;
            if (d && cJSON_IsNumber(d)) {
                int iv = (int)d->valuedouble;
                if (iv < 0) iv = 0;
                if (iv > 255) iv = 255;
                c->effect_mod[i].depth = (uint8_t)iv;
            }
            if (r && cJSON_IsNumber(r)) {
                int iv = (int)r->valuedouble;
                if (iv < 0) iv = 0;
                if (iv > 255) iv = 255;
                c->effect_mod[i].rate = (uint8_t)iv;
            }
            if (p && cJSON_IsNumber(p)) {
                int iv = (int)p->valuedouble;
                if (iv < 0) iv = 0;
                if (iv > 255) iv = 255;
                c->effect_mod[i].phase = (uint8_t)iv;
            }
        }
    }
    if ((v = cJSON_GetObjectItem(j, "dmxmode")) && cJSON_IsNumber(v)) {
        c->dmx_mode = ((int)v->valuedouble == (int)PM_DMX_MODE_PIXELS)
                          ? PM_DMX_MODE_PIXELS
                          : PM_DMX_MODE_PARAMS;
    }
    {
        cJSON *ja = cJSON_GetObjectItem(j, "aun");
        cJSON *js = cJSON_GetObjectItem(j, "sun");
        if (ja && cJSON_IsNumber(ja)) {
            c->artnet_universe = (uint16_t)ja->valuedouble;
            c->sacn_universe = c->artnet_universe;
        } else if (js && cJSON_IsNumber(js)) {
            c->sacn_universe = (uint16_t)js->valuedouble;
            c->artnet_universe = c->sacn_universe;
        }
    }
    if ((v = cJSON_GetObjectItem(j, "aen"))) c->artnet_enable = cJSON_IsTrue(v);
    if ((v = cJSON_GetObjectItem(j, "sen"))) c->sacn_enable = cJSON_IsTrue(v);
    if ((v = cJSON_GetObjectItem(j, "ucnt")) && cJSON_IsNumber(v)) {
        int uv = (int)v->valuedouble;
        if (uv < 1) uv = 1;
        if (uv > 16) uv = 16;
        c->universe_count = (uint16_t)uv;
    }
    if ((v = cJSON_GetObjectItem(j, "sminp")) && cJSON_IsNumber(v)) {
        int p = (int)v->valuedouble;
        if (p < 0) p = 0;
        if (p > 200) p = 200;
        c->sacn_min_priority = (uint8_t)p;
    }
    if ((v = cJSON_GetObjectItem(j, "maled")) && cJSON_IsNumber(v)) {
        int m = (int)v->valuedouble;
        if (m < 1) m = 1;
        if (m > 1000) m = 1000;
        c->ma_per_led = (uint16_t)m;
    }
    /* Empty string clears PIN; omit keeps current. */
    if ((v = cJSON_GetObjectItem(j, "uipin")) && cJSON_IsString(v)) {
        strncpy(c->ui_pin, v->valuestring, sizeof(c->ui_pin) - 1);
        c->ui_pin[sizeof(c->ui_pin) - 1] = '\0';
    }
    if ((v = cJSON_GetObjectItem(j, "auden"))) c->audio_enable = cJSON_IsTrue(v);
    if ((v = cJSON_GetObjectItem(j, "audws")) && cJSON_IsNumber(v)) c->audio_gpio_ws = (int)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "audck")) && cJSON_IsNumber(v)) c->audio_gpio_sck = (int)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "audsd")) && cJSON_IsNumber(v)) c->audio_gpio_sd = (int)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "audgn")) && cJSON_IsNumber(v)) {
        int g = (int)v->valuedouble;
        if (g < 1) g = 1;
        if (g > 255) g = 255;
        c->audio_gain = (uint8_t)g;
    }
    if ((v = cJSON_GetObjectItem(j, "audsq")) && cJSON_IsNumber(v)) {
        int q = (int)v->valuedouble;
        if (q < 0) q = 0;
        if (q > 255) q = 255;
        c->audio_squelch = (uint8_t)q;
    }
    if ((v = cJSON_GetObjectItem(j, "audmod"))) c->audio_modulate = cJSON_IsTrue(v);
    /* Only one protocol at a time */
    if (c->artnet_enable && c->sacn_enable) c->sacn_enable = false;
    if ((v = cJSON_GetObjectItem(j, "mw")) && cJSON_IsNumber(v)) c->map_width = (uint16_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "mh")) && cJSON_IsNumber(v)) c->map_height = (uint16_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "md")) && cJSON_IsNumber(v)) c->map_depth = (uint16_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "mdim")) && cJSON_IsNumber(v)) c->map_dim = (pm_map_dim_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "mlay")) && cJSON_IsNumber(v)) c->map_layout = (pm_map_layout_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "mfill")) && cJSON_IsNumber(v)) c->map_fill = (pm_map_fill_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "mopentb"))) c->map_open_tb = cJSON_IsTrue(v);
    if ((v = cJSON_GetObjectItem(j, "mspc")) && cJSON_IsNumber(v)) {
        c->map_spacing = (float)v->valuedouble;
        if (c->map_spacing < 1e-4f) c->map_spacing = 1.0f;
    }
    if ((v = cJSON_GetObjectItem(j, "pove"))) c->pov_enable = cJSON_IsTrue(v);
    if ((v = cJSON_GetObjectItem(j, "povm")) && cJSON_IsNumber(v)) c->pov_mode = (pm_pov_mode_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "poyl")) && cJSON_IsNumber(v)) c->pov_layout = (pm_pov_layout_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "povbl")) && cJSON_IsNumber(v)) c->pov_blade_count = pm_pov_clamp_blades((uint8_t)v->valuedouble);
    if ((v = cJSON_GetObjectItem(j, "povrpm")) && cJSON_IsNumber(v)) c->pov_rpm = (float)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "povspd")) && cJSON_IsNumber(v)) c->pov_linear_speed_mps = (float)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "povrad")) && cJSON_IsNumber(v)) c->pov_radius_m = (float)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "povpath")) && cJSON_IsNumber(v)) c->pov_path_length_m = (float)v->valuedouble;
    cJSON_Delete(j);

    pm_config_save(c);
    pm_config_unlock();
    if (s_hooks.on_config_changed) s_hooks.on_config_changed();

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    if (auth_changed && web_auth_enabled()) {
        mint_session(s_session, sizeof(s_session));
        set_session_cookie(req, s_session);
        cJSON_AddStringToObject(resp, "token", s_session);
    }
    return send_json(req, resp);
}

static esp_err_t h_get_map(httpd_req_t *req)
{
    if (!auth_ok(req, false)) return reject_pin(req);
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
    if (!pin_ok(req)) return reject_pin(req);
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
    if (!pin_ok(req)) return reject_pin(req);
    cJSON *j = read_body_json(req);
    if (!j) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
        return ESP_FAIL;
    }
    int w = cJSON_GetObjectItem(j, "w") ? (int)cJSON_GetObjectItem(j, "w")->valuedouble : 10;
    int h = cJSON_GetObjectItem(j, "h") ? (int)cJSON_GetObjectItem(j, "h")->valuedouble : 6;
    int d = cJSON_GetObjectItem(j, "d") ? (int)cJSON_GetObjectItem(j, "d")->valuedouble : 1;
    int dim = cJSON_GetObjectItem(j, "dim") ? (int)cJSON_GetObjectItem(j, "dim")->valuedouble : 0;
    int lay = cJSON_GetObjectItem(j, "lay") ? (int)cJSON_GetObjectItem(j, "lay")->valuedouble
                                            : (int)s_hooks.cfg->map_layout;
    float sp = cJSON_GetObjectItem(j, "spc") ? (float)cJSON_GetObjectItem(j, "spc")->valuedouble
                                             : s_hooks.cfg->map_spacing;
    int fill = cJSON_GetObjectItem(j, "fill") ? (int)cJSON_GetObjectItem(j, "fill")->valuedouble
                                              : (int)s_hooks.cfg->map_fill;
    cJSON *otb = cJSON_GetObjectItem(j, "opentb");
    bool open_tb = otb ? cJSON_IsTrue(otb) : s_hooks.cfg->map_open_tb;
    cJSON_Delete(j);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (d < 1) d = 1;
    if (sp < 1e-4f) sp = 1.0f;
    if (dim != 1) d = 1;
    if (fill != 1) fill = 0;

    pm_map_layout_t layout = (pm_map_layout_t)lay;
    if (dim != 1 && layout == PM_MAP_LAYOUT_SPHERE) layout = PM_MAP_LAYOUT_CIRCLE;
    if (dim == 1 && layout == PM_MAP_LAYOUT_CIRCLE) layout = PM_MAP_LAYOUT_SPHERE;
    /* 3D-only shapes → Box if forced to 2D; Grid in 3D → solid Box */
    if (dim != 1 && (layout == PM_MAP_LAYOUT_BOX || layout == PM_MAP_LAYOUT_CYLINDER
                     || layout == PM_MAP_LAYOUT_DOME || layout == PM_MAP_LAYOUT_PYRAMID)) {
        layout = PM_MAP_LAYOUT_GRID;
    }
    if (dim == 1 && layout == PM_MAP_LAYOUT_GRID) {
        layout = PM_MAP_LAYOUT_BOX;
        fill = 1;
    }
    if (fill != 0) open_tb = false; /* solid has no open top/bottom */

    pm_config_lock();
    uint16_t max_n = s_hooks.cfg->pixel_count > 0 ? s_hooks.cfg->pixel_count : 1;
    esp_err_t err = ESP_OK;

    if (layout == PM_MAP_LAYOUT_CUSTOM) {
        err = ESP_OK; /* points already on device via /api/map */
    } else if (layout == PM_MAP_LAYOUT_CIRCLE) {
        err = pm_pixel_map_build_circle(s_hooks.map, (uint16_t)w, sp, 0, max_n, (uint8_t)fill);
    } else if (layout == PM_MAP_LAYOUT_SPHERE) {
        err = pm_pixel_map_build_sphere(s_hooks.map, (uint16_t)w, sp, 0, max_n, (uint8_t)fill);
    } else if (layout == PM_MAP_LAYOUT_BOX) {
        err = pm_pixel_map_build_box(s_hooks.map, (uint16_t)w, (uint16_t)h, (uint16_t)d,
                                     sp, 0, max_n, (uint8_t)fill, open_tb);
    } else if (layout == PM_MAP_LAYOUT_CYLINDER) {
        err = pm_pixel_map_build_cylinder(s_hooks.map, (uint16_t)w, (uint16_t)h, sp, 0, max_n, open_tb);
    } else if (layout == PM_MAP_LAYOUT_DOME) {
        err = pm_pixel_map_build_dome(s_hooks.map, (uint16_t)w, sp, 0, max_n);
    } else if (layout == PM_MAP_LAYOUT_PYRAMID) {
        err = pm_pixel_map_build_pyramid(s_hooks.map, (uint16_t)w, (uint16_t)h, sp, 0, max_n);
    } else {
        err = pm_pixel_map_build_grid(s_hooks.map, (uint16_t)w, (uint16_t)h, sp, 0, max_n);
    }

    uint16_t used = 0;
    if (err == ESP_OK && layout != PM_MAP_LAYOUT_CUSTOM) {
        pm_pixel_map_normalize_uniform(s_hooks.map);
        used = pm_pixel_map_count(s_hooks.map);
        s_hooks.cfg->map_width = (uint16_t)w;
        s_hooks.cfg->map_height = (uint16_t)h;
        s_hooks.cfg->map_depth = (uint16_t)d;
        s_hooks.cfg->map_dim = dim == 1 ? PM_MAP_DIM_3D : PM_MAP_DIM_2D;
        s_hooks.cfg->map_layout = layout;
        s_hooks.cfg->map_fill = (pm_map_fill_t)fill;
        s_hooks.cfg->map_open_tb = open_tb;
        s_hooks.cfg->map_spacing = sp;
        pm_config_save(s_hooks.cfg);
    } else {
        used = pm_pixel_map_count(s_hooks.map);
    }
    pm_config_unlock();
    if (err == ESP_OK && layout != PM_MAP_LAYOUT_CUSTOM) {
        if (s_hooks.on_map_changed) s_hooks.on_map_changed();
        if (s_hooks.on_config_changed) s_hooks.on_config_changed();
    }

    unsigned unused = max_n > used ? (unsigned)(max_n - used) : 0;
    char resp[160];
    snprintf(resp, sizeof(resp),
             "{\"ok\":%s,\"count\":%u,\"max\":%u,\"unused\":%u}",
             err == ESP_OK ? "true" : "false",
             (unsigned)used, (unsigned)max_n, unused);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, resp);
}

static esp_err_t h_get_fx_lua(httpd_req_t *req)
{
    if (!auth_ok(req, false)) return reject_pin(req);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "script", pm_effect_lua_get_script());
    cJSON_AddBoolToObject(o, "ready", pm_effect_lua_ready());
    cJSON_AddStringToObject(o, "error", pm_effect_lua_last_error());
    cJSON_AddNumberToObject(o, "max", PM_LUA_SCRIPT_MAX - 1);
    return send_json(req, o);
}

static esp_err_t h_post_fx_lua(httpd_req_t *req)
{
    if (!pin_ok(req)) return reject_pin(req);
    cJSON *j = read_body_json(req);
    if (!j) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
        return ESP_FAIL;
    }
    cJSON *s = cJSON_GetObjectItem(j, "script");
    if (!s || !cJSON_IsString(s) || !s->valuestring) {
        cJSON_Delete(j);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "script required");
        return ESP_FAIL;
    }
    esp_err_t err = pm_effect_lua_set_script(s->valuestring);
    cJSON_Delete(j);

    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", err == ESP_OK);
    cJSON_AddBoolToObject(o, "ready", pm_effect_lua_ready());
    cJSON_AddStringToObject(o, "error", pm_effect_lua_last_error());
    if (err == ESP_ERR_INVALID_SIZE) {
        cJSON_AddStringToObject(o, "error", "script too large");
    }
    return send_json(req, o);
}

static esp_err_t h_get_ota(httpd_req_t *req)
{
    if (!auth_ok(req, false)) return reject_pin(req);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "part", pm_ota_running_label());
    cJSON_AddBoolToObject(o, "busy", pm_ota_in_progress());
    return send_json(req, o);
}

static esp_err_t h_post_ota(httpd_req_t *req)
{
    if (!pin_ok(req)) return reject_pin(req);
    if (req->content_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
        return ESP_FAIL;
    }
    esp_err_t err = pm_ota_begin((size_t)req->content_len);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        return ESP_FAIL;
    }

    uint8_t buf[2048];
    int remaining = req->content_len;
    while (remaining > 0) {
        int to_read = remaining > (int)sizeof(buf) ? (int)sizeof(buf) : remaining;
        int r = httpd_req_recv(req, (char *)buf, to_read);
        if (r <= 0) {
            pm_ota_abort();
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv");
            return ESP_FAIL;
        }
        err = pm_ota_write(buf, (size_t)r);
        if (err != ESP_OK) {
            pm_ota_abort();
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
            return ESP_FAIL;
        }
        remaining -= r;
    }

    err = pm_ota_finish();
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"reboot\":true}");
    schedule_reboot();
    return ESP_OK;
}

static esp_err_t h_get_presets(httpd_req_t *req)
{
    if (!auth_ok(req, false)) return reject_pin(req);
    cJSON *o = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < PM_PRESET_SLOTS; ++i) {
        const pm_preset_t *p = pm_presets_get((uint8_t)i);
        cJSON *slot = cJSON_CreateObject();
        cJSON_AddNumberToObject(slot, "slot", i);
        cJSON_AddBoolToObject(slot, "used", p && p->used);
        cJSON_AddStringToObject(slot, "name", (p && p->used) ? p->name : "");
        cJSON_AddItemToArray(arr, slot);
    }
    cJSON_AddItemToObject(o, "slots", arr);
    return send_json(req, o);
}

static esp_err_t h_post_presets(httpd_req_t *req)
{
    if (!pin_ok(req)) return reject_pin(req);
    cJSON *j = read_body_json(req);
    if (!j) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
        return ESP_FAIL;
    }
    cJSON *act = cJSON_GetObjectItem(j, "action");
    cJSON *slotj = cJSON_GetObjectItem(j, "slot");
    if (!act || !cJSON_IsString(act) || !slotj || !cJSON_IsNumber(slotj)) {
        cJSON_Delete(j);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "action/slot");
        return ESP_FAIL;
    }
    int slot = (int)slotj->valuedouble;
    if (slot < 0 || slot >= PM_PRESET_SLOTS) {
        cJSON_Delete(j);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "slot");
        return ESP_FAIL;
    }
    char action[16] = {0};
    strncpy(action, act->valuestring, sizeof(action) - 1);
    cJSON *name = cJSON_GetObjectItem(j, "name");
    char namebuf[PM_PRESET_NAME_MAX] = {0};
    if (name && cJSON_IsString(name) && name->valuestring) {
        strncpy(namebuf, name->valuestring, sizeof(namebuf) - 1);
    }
    cJSON_Delete(j);

    esp_err_t err = ESP_FAIL;
    bool applied = false;
    pm_config_lock();
    if (strcmp(action, "save") == 0) {
        err = pm_presets_save_slot((uint8_t)slot, namebuf[0] ? namebuf : NULL, s_hooks.cfg);
    } else if (strcmp(action, "apply") == 0) {
        err = pm_presets_apply_slot((uint8_t)slot, s_hooks.cfg);
        if (err == ESP_OK) {
            pm_config_save(s_hooks.cfg);
            applied = true;
        }
    } else if (strcmp(action, "clear") == 0) {
        err = pm_presets_clear_slot((uint8_t)slot);
    } else {
        err = ESP_ERR_INVALID_ARG;
    }
    pm_config_unlock();
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, esp_err_to_name(err));
        return ESP_FAIL;
    }
    if (applied && s_hooks.on_config_changed) {
        s_hooks.on_config_changed();
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t h_post_factory_reset(httpd_req_t *req)
{
    if (!pin_ok(req)) return reject_pin(req);
    cJSON *j = read_body_json(req);
    if (!j) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
        return ESP_FAIL;
    }
    cJSON *p = cJSON_GetObjectItem(j, "confirmPass");
    const char *pass = (p && cJSON_IsString(p) && p->valuestring) ? p->valuestring : "";
    bool ok = pm_security_verify_password(pass, stored_web_secret());
    cJSON_Delete(j);
    if (!ok) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"confirmPass required\"}");
    }
    s_session[0] = '\0';
    pm_config_factory_reset_nvs();
    pm_map_store_erase();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"reboot\":true}");
    schedule_reboot();
    return ESP_OK;
}

static esp_err_t h_post_auth(httpd_req_t *req)
{
    if (now_ms() < s_auth_lock_until_ms) {
        httpd_resp_set_status(req, "429 Too Many Requests");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"try again later\"}");
    }
    cJSON *j = read_body_json(req);
    if (!j) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
        return ESP_FAIL;
    }
    cJSON *p = cJSON_GetObjectItem(j, "pass");
    const char *pass = (p && cJSON_IsString(p) && p->valuestring) ? p->valuestring : "";
    const char *expect = stored_web_secret();
    bool ok = expect[0] && pm_security_verify_password(pass, expect);
    cJSON_Delete(j);
    if (!ok) {
        s_auth_fails++;
        if (s_auth_fails >= PM_AUTH_MAX_FAILS) {
            s_auth_lock_until_ms = now_ms() + PM_AUTH_LOCK_MS;
            s_auth_fails = 0;
            ESP_LOGW(TAG, "auth lockout for %lld ms", (long long)PM_AUTH_LOCK_MS);
        }
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"bad password\"}");
    }
    s_auth_fails = 0;
    mint_session(s_session, sizeof(s_session));
    set_session_cookie(req, s_session);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddStringToObject(o, "token", s_session);
    return send_json(req, o);
}

static esp_err_t h_get_auth(httpd_req_t *req)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "setupComplete", setup_done());
    cJSON_AddBoolToObject(o, "required", setup_done() && web_auth_enabled());
    cJSON_AddBoolToObject(o, "ok", setup_done() && session_ok(req));
    return send_json(req, o);
}

static esp_err_t h_get_setup_status(httpd_req_t *req)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "setupComplete", setup_done());
    cJSON_AddStringToObject(o, "setupApPass", PM_SETUP_AP_PASS);
    cJSON_AddStringToObject(o, "apIp", "192.168.4.1");
    return send_json(req, o);
}

static esp_err_t wifi_scan_response(httpd_req_t *req)
{
    pm_wifi_scan_ap_t aps[24];
    size_t n = 0;
    esp_err_t err = pm_wifi_scan(aps, sizeof(aps) / sizeof(aps[0]), &n);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", err == ESP_OK);
    if (err != ESP_OK) {
        cJSON_AddStringToObject(o, "error", esp_err_to_name(err));
        return send_json(req, o);
    }
    cJSON *arr = cJSON_CreateArray();
    for (size_t i = 0; i < n; ++i) {
        cJSON *a = cJSON_CreateObject();
        cJSON_AddStringToObject(a, "ssid", aps[i].ssid);
        cJSON_AddNumberToObject(a, "rssi", aps[i].rssi);
        cJSON_AddBoolToObject(a, "open", aps[i].open);
        cJSON_AddItemToArray(arr, a);
    }
    cJSON_AddItemToObject(o, "aps", arr);
    return send_json(req, o);
}

static esp_err_t h_get_setup_scan(httpd_req_t *req)
{
    if (setup_done()) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"setup already complete\"}");
    }
    return wifi_scan_response(req);
}

static esp_err_t h_post_setup_complete(httpd_req_t *req)
{
    if (setup_done()) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"setup already complete\"}");
    }
    cJSON *j = read_body_json(req);
    if (!j) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
        return ESP_FAIL;
    }

    pm_app_config_t *c = s_hooks.cfg;
    pm_config_lock();
    cJSON *v;
    if ((v = cJSON_GetObjectItem(j, "ssid")) && cJSON_IsString(v)) {
        strncpy(c->sta_ssid, v->valuestring, sizeof(c->sta_ssid) - 1);
        c->sta_ssid[sizeof(c->sta_ssid) - 1] = '\0';
    }
    if ((v = cJSON_GetObjectItem(j, "pass")) && cJSON_IsString(v) && v->valuestring[0]) {
        strncpy(c->sta_pass, v->valuestring, sizeof(c->sta_pass) - 1);
        c->sta_pass[sizeof(c->sta_pass) - 1] = '\0';
    }
    if ((v = cJSON_GetObjectItem(j, "host")) && cJSON_IsString(v) && v->valuestring[0]) {
        strncpy(c->hostname, v->valuestring, sizeof(c->hostname) - 1);
        c->hostname[sizeof(c->hostname) - 1] = '\0';
    }
    if ((v = cJSON_GetObjectItem(j, "apen"))) c->ap_enable = cJSON_IsTrue(v);
    if ((v = cJSON_GetObjectItem(j, "apfb"))) c->ap_fallback = cJSON_IsTrue(v);
    if ((v = cJSON_GetObjectItem(j, "apssid")) && cJSON_IsString(v)) {
        strncpy(c->ap_ssid, v->valuestring, sizeof(c->ap_ssid) - 1);
        c->ap_ssid[sizeof(c->ap_ssid) - 1] = '\0';
    }
    if ((v = cJSON_GetObjectItem(j, "appass")) && cJSON_IsString(v) && v->valuestring[0]) {
        if (!pm_security_password_ok(v->valuestring)) {
            pm_config_unlock();
            cJSON_Delete(j);
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_set_type(req, "application/json");
            return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"AP password must be at least 12 characters\"}");
        }
        strncpy(c->ap_pass, v->valuestring, sizeof(c->ap_pass) - 1);
        c->ap_pass[sizeof(c->ap_pass) - 1] = '\0';
    } else {
        /* Keep setup default; require acknowledgement in wizard. */
        bool ack = cJSON_IsTrue(cJSON_GetObjectItem(j, "ackDefaultAp"));
        if (!ack && strcmp(c->ap_pass, PM_SETUP_AP_PASS) == 0) {
            /* allow keep with ackDefaultAp from wizard */
        }
        strncpy(c->ap_pass, PM_SETUP_AP_PASS, sizeof(c->ap_pass) - 1);
    }

    bool open_ui = cJSON_IsTrue(cJSON_GetObjectItem(j, "openUi"));
    const char *webpass = NULL;
    if ((v = cJSON_GetObjectItem(j, "webpass")) && cJSON_IsString(v)) webpass = v->valuestring;
    if (open_ui || !webpass || !webpass[0]) {
        if (!cJSON_IsTrue(cJSON_GetObjectItem(j, "ackOpen"))) {
            pm_config_unlock();
            cJSON_Delete(j);
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_set_type(req, "application/json");
            return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"acknowledge open UI or set a password\"}");
        }
        memset(c->web_pass, 0, sizeof(c->web_pass));
        c->web_auth = false;
        c->web_pass_rotate = false;
    } else {
        if (!pm_security_password_ok(webpass)) {
            pm_config_unlock();
            cJSON_Delete(j);
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_set_type(req, "application/json");
            return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"web password must be at least 12 characters\"}");
        }
        char hash[PM_SECURITY_HASH_MAX];
        if (pm_security_hash_password(webpass, hash, sizeof(hash)) != ESP_OK) {
            pm_config_unlock();
            cJSON_Delete(j);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "hash failed");
            return ESP_FAIL;
        }
        strncpy(c->web_pass, hash, sizeof(c->web_pass) - 1);
        c->web_pass[sizeof(c->web_pass) - 1] = '\0';
        c->web_auth = true;
        c->web_pass_rotate = false;
    }

    c->setup_complete = true;
    cJSON_Delete(j);
    pm_config_save(c);
    pm_config_unlock();
    if (s_hooks.on_config_changed) s_hooks.on_config_changed();

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    if (web_auth_enabled()) {
        mint_session(s_session, sizeof(s_session));
        set_session_cookie(req, s_session);
        cJSON_AddStringToObject(resp, "token", s_session);
    }
    return send_json(req, resp);
}

static esp_err_t h_post_auth_logout(httpd_req_t *req)
{
    if (!session_ok(req)) return reject_pin(req);
    s_session[0] = '\0';
    set_session_cookie(req, NULL);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t h_get_wifi_status(httpd_req_t *req)
{
    if (!auth_ok(req, false)) return reject_pin(req);
    pm_wifi_status_t st;
    pm_wifi_get_status(&st);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "mode", st.mode ? st.mode : "?");
    cJSON_AddBoolToObject(o, "sta", st.sta_connected);
    cJSON_AddBoolToObject(o, "staConnecting", st.sta_connecting);
    cJSON_AddBoolToObject(o, "ap", st.ap_active);
    cJSON_AddBoolToObject(o, "apen", st.ap_enable);
    cJSON_AddBoolToObject(o, "apfb", st.ap_fallback);
    cJSON_AddStringToObject(o, "staIp", st.sta_ip);
    cJSON_AddStringToObject(o, "apIp", st.ap_ip);
    cJSON_AddStringToObject(o, "staSsid", st.sta_ssid);
    cJSON_AddStringToObject(o, "apSsid", st.ap_ssid);
    cJSON_AddStringToObject(o, "host", st.hostname);
    cJSON_AddStringToObject(o, "otaPart", pm_ota_running_label());
    return send_json(req, o);
}

static esp_err_t h_get_wifi_scan(httpd_req_t *req)
{
    if (!auth_ok(req, false)) return reject_pin(req);
    return wifi_scan_response(req);
}

static esp_err_t h_get_audio(httpd_req_t *req)
{
    if (!auth_ok(req, false)) return reject_pin(req);
    pm_audio_levels_t lv;
    pm_audio_get_levels(&lv);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "on", pm_audio_running());
    cJSON_AddBoolToObject(o, "active", lv.active);
    cJSON_AddNumberToObject(o, "vol", lv.volume);
    cJSON_AddNumberToObject(o, "bass", lv.bass);
    cJSON_AddNumberToObject(o, "mid", lv.mid);
    cJSON_AddNumberToObject(o, "tre", lv.treble);
    cJSON_AddBoolToObject(o, "beat", lv.beat);
    cJSON *bins = cJSON_CreateArray();
    for (int i = 0; i < PM_AUDIO_BINS; ++i) {
        cJSON_AddItemToArray(bins, cJSON_CreateNumber(lv.bins[i]));
    }
    cJSON_AddItemToObject(o, "bins", bins);
    return send_json(req, o);
}

esp_err_t pm_web_ui_start(const pm_web_ui_hooks_t *hooks)
{
    if (!hooks || !hooks->cfg || !hooks->map) return ESP_ERR_INVALID_ARG;
    s_hooks = *hooks;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 40;
    config.stack_size = 10240;
    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), TAG, "httpd");

    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = h_index},
        {.uri = "/logo.svg", .method = HTTP_GET, .handler = h_logo_svg},
        {.uri = "/vendor/bootstrap.min.css", .method = HTTP_GET, .handler = h_bootstrap_css},
        {.uri = "/vendor/bootstrap.bundle.min.js", .method = HTTP_GET, .handler = h_bootstrap_js},
        {.uri = "/api/auth", .method = HTTP_GET, .handler = h_get_auth},
        {.uri = "/api/auth", .method = HTTP_POST, .handler = h_post_auth},
        {.uri = "/api/auth/logout", .method = HTTP_POST, .handler = h_post_auth_logout},
        {.uri = "/api/setup/status", .method = HTTP_GET, .handler = h_get_setup_status},
        {.uri = "/api/setup/scan", .method = HTTP_GET, .handler = h_get_setup_scan},
        {.uri = "/api/setup/complete", .method = HTTP_POST, .handler = h_post_setup_complete},
        {.uri = "/api/wifi/status", .method = HTTP_GET, .handler = h_get_wifi_status},
        {.uri = "/api/wifi/scan", .method = HTTP_GET, .handler = h_get_wifi_scan},
        {.uri = "/api/config", .method = HTTP_GET, .handler = h_get_config},
        {.uri = "/api/config", .method = HTTP_POST, .handler = h_post_config},
        {.uri = "/api/map", .method = HTTP_GET, .handler = h_get_map},
        {.uri = "/api/map", .method = HTTP_POST, .handler = h_post_map},
        {.uri = "/api/map/grid", .method = HTTP_POST, .handler = h_post_grid},
        {.uri = "/api/fx/lua", .method = HTTP_GET, .handler = h_get_fx_lua},
        {.uri = "/api/fx/lua", .method = HTTP_POST, .handler = h_post_fx_lua},
        {.uri = "/api/ota", .method = HTTP_GET, .handler = h_get_ota},
        {.uri = "/api/ota", .method = HTTP_POST, .handler = h_post_ota},
        {.uri = "/api/presets", .method = HTTP_GET, .handler = h_get_presets},
        {.uri = "/api/presets", .method = HTTP_POST, .handler = h_post_presets},
        {.uri = "/api/factory_reset", .method = HTTP_POST, .handler = h_post_factory_reset},
        {.uri = "/api/audio", .method = HTTP_GET, .handler = h_get_audio},
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
