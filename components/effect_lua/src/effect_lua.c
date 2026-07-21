#include "effect_lua.h"
#include "color_engine.h"
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "effect_lua";
static const char *NVS_NS = "pm_lua";
static const char *NVS_KEY = "script";

static const char *DEFAULT_SCRIPT =
    "-- Per-pixel custom effect (Lua)\n"
    "local d = length(x - 0.5, y - 0.5, z - 0.5)\n"
    "local wave = 0.5 + 0.5 * sin(d * 16 - t * 4)\n"
    "return hsv((t * 40 + d * 80) % 256, 230, wave * 255)\n";

static lua_State *s_L;
static int s_fn_ref = LUA_NOREF;
static char s_script[PM_LUA_SCRIPT_MAX];
static char s_err[160];
static bool s_ready;
static SemaphoreHandle_t s_mu;
static volatile int s_hook_budget;

static void set_err(const char *msg)
{
    if (!msg) msg = "error";
    strncpy(s_err, msg, sizeof(s_err) - 1);
    s_err[sizeof(s_err) - 1] = 0;
}

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float hash2(float x, float y)
{
    float n = sinf(x * 127.1f + y * 311.7f) * 43758.5453f;
    return n - floorf(n);
}

static float noise2f(float x, float y)
{
    float xi = floorf(x), yi = floorf(y);
    float xf = x - xi, yf = y - yi;
    float u = xf * xf * (3.0f - 2.0f * xf);
    float v = yf * yf * (3.0f - 2.0f * yf);
    float a = hash2(xi, yi);
    float b = hash2(xi + 1, yi);
    float c = hash2(xi, yi + 1);
    float d = hash2(xi + 1, yi + 1);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}

static float noise3f(float x, float y, float z)
{
    return noise2f(x + z * 0.31f, y - z * 0.17f);
}

static void hook_count(lua_State *L, lua_Debug *ar)
{
    (void)ar;
    if (--s_hook_budget <= 0) {
        luaL_error(L, "script instruction budget exceeded");
    }
}

/* —— helpers exposed to Lua —— */

static int l_hsv(lua_State *L)
{
    lua_createtable(L, 0, 4);
    lua_pushstring(L, "hsv");
    lua_setfield(L, -2, "t");
    lua_pushnumber(L, luaL_optnumber(L, 1, 0));
    lua_setfield(L, -2, "h");
    lua_pushnumber(L, luaL_optnumber(L, 2, 255));
    lua_setfield(L, -2, "s");
    lua_pushnumber(L, luaL_optnumber(L, 3, 255));
    lua_setfield(L, -2, "v");
    return 1;
}

static int l_rgb(lua_State *L)
{
    lua_createtable(L, 0, 4);
    lua_pushstring(L, "rgb");
    lua_setfield(L, -2, "t");
    lua_pushnumber(L, luaL_optnumber(L, 1, 0));
    lua_setfield(L, -2, "r");
    lua_pushnumber(L, luaL_optnumber(L, 2, 0));
    lua_setfield(L, -2, "g");
    lua_pushnumber(L, luaL_optnumber(L, 3, 0));
    lua_setfield(L, -2, "b");
    return 1;
}

static int l_lerp(lua_State *L)
{
    float a = (float)luaL_checknumber(L, 1);
    float b = (float)luaL_checknumber(L, 2);
    float t = (float)luaL_checknumber(L, 3);
    lua_pushnumber(L, a + (b - a) * t);
    return 1;
}

static int l_clamp(lua_State *L)
{
    float v = (float)luaL_checknumber(L, 1);
    float lo = (float)luaL_optnumber(L, 2, 0);
    float hi = (float)luaL_optnumber(L, 3, 1);
    lua_pushnumber(L, clampf(v, lo, hi));
    return 1;
}

static int l_noise2(lua_State *L)
{
    lua_pushnumber(L, noise2f((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2)));
    return 1;
}

static int l_noise3(lua_State *L)
{
    lua_pushnumber(L, noise3f((float)luaL_checknumber(L, 1),
                              (float)luaL_checknumber(L, 2),
                              (float)luaL_checknumber(L, 3)));
    return 1;
}

static int l_fract(lua_State *L)
{
    float v = (float)luaL_checknumber(L, 1);
    lua_pushnumber(L, v - floorf(v));
    return 1;
}

static int l_length(lua_State *L)
{
    int n = lua_gettop(L);
    float s = 0;
    for (int i = 1; i <= n; ++i) {
        float v = (float)luaL_checknumber(L, i);
        s += v * v;
    }
    lua_pushnumber(L, sqrtf(s));
    return 1;
}

static int l_dist(lua_State *L)
{
    float ax = (float)luaL_checknumber(L, 1);
    float ay = (float)luaL_checknumber(L, 2);
    float az = (float)luaL_optnumber(L, 3, 0);
    float bx = (float)luaL_optnumber(L, 4, 0);
    float by = (float)luaL_optnumber(L, 5, 0);
    float bz = (float)luaL_optnumber(L, 6, 0);
    /* dist(x,y,z) from origin OR dist(x1,y1,z1,x2,y2,z2) */
    if (lua_gettop(L) <= 3) {
        lua_pushnumber(L, sqrtf(ax * ax + ay * ay + az * az));
    } else {
        float dx = ax - bx, dy = ay - by, dz = az - bz;
        lua_pushnumber(L, sqrtf(dx * dx + dy * dy + dz * dz));
    }
    return 1;
}

static int l_wrap_math1(lua_State *L, float (*fn)(float))
{
    lua_pushnumber(L, fn((float)luaL_checknumber(L, 1)));
    return 1;
}

static int l_sin(lua_State *L) { return l_wrap_math1(L, sinf); }
static int l_cos(lua_State *L) { return l_wrap_math1(L, cosf); }
static int l_abs(lua_State *L) { return l_wrap_math1(L, fabsf); }
static int l_floor(lua_State *L) { return l_wrap_math1(L, floorf); }

static int l_atan2(lua_State *L)
{
    lua_pushnumber(L, atan2f((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2)));
    return 1;
}

static void register_helpers(lua_State *L)
{
    lua_register(L, "hsv", l_hsv);
    lua_register(L, "rgb", l_rgb);
    lua_register(L, "lerp", l_lerp);
    lua_register(L, "clamp", l_clamp);
    lua_register(L, "noise2", l_noise2);
    lua_register(L, "noise3", l_noise3);
    lua_register(L, "fract", l_fract);
    lua_register(L, "length", l_length);
    lua_register(L, "dist", l_dist);
    lua_register(L, "sin", l_sin);
    lua_register(L, "cos", l_cos);
    lua_register(L, "abs", l_abs);
    lua_register(L, "floor", l_floor);
    lua_register(L, "atan2", l_atan2);
}

static void open_safe_libs(lua_State *L)
{
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    lua_pop(L, 1);

    /* Remove dangerous base functions */
    lua_pushnil(L); lua_setglobal(L, "dofile");
    lua_pushnil(L); lua_setglobal(L, "loadfile");
    lua_pushnil(L); lua_setglobal(L, "load");
    lua_pushnil(L); lua_setglobal(L, "require");
    lua_pushnil(L); lua_setglobal(L, "print");
    lua_pushnil(L); lua_setglobal(L, "collectgarbage");
}

static esp_err_t persist_script(const char *src)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, NVS_KEY, src);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static void load_script_from_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        strncpy(s_script, DEFAULT_SCRIPT, sizeof(s_script) - 1);
        return;
    }
    size_t len = sizeof(s_script);
    esp_err_t err = nvs_get_str(h, NVS_KEY, s_script, &len);
    nvs_close(h);
    if (err != ESP_OK || s_script[0] == 0) {
        strncpy(s_script, DEFAULT_SCRIPT, sizeof(s_script) - 1);
        s_script[sizeof(s_script) - 1] = 0;
    }
}

static esp_err_t compile_locked(const char *src)
{
    if (!s_L) return ESP_ERR_INVALID_STATE;

    if (s_fn_ref != LUA_NOREF) {
        luaL_unref(s_L, LUA_REGISTRYINDEX, s_fn_ref);
        s_fn_ref = LUA_NOREF;
    }
    s_ready = false;

    /* Wrap user body as a callable: return function() <body> end */
    static char wrapped[PM_LUA_SCRIPT_MAX + 64];
    int n = snprintf(wrapped, sizeof(wrapped), "return function()\n%s\nend", src);
    if (n < 0 || n >= (int)sizeof(wrapped)) {
        set_err("script too large");
        return ESP_ERR_INVALID_SIZE;
    }

    if (luaL_loadbuffer(s_L, wrapped, strlen(wrapped), "effect") != LUA_OK) {
        set_err(lua_tostring(s_L, -1));
        lua_pop(s_L, 1);
        return ESP_FAIL;
    }
    if (lua_pcall(s_L, 0, 1, 0) != LUA_OK) {
        set_err(lua_tostring(s_L, -1));
        lua_pop(s_L, 1);
        return ESP_FAIL;
    }
    if (!lua_isfunction(s_L, -1)) {
        set_err("compile did not produce a function");
        lua_pop(s_L, 1);
        return ESP_FAIL;
    }
    s_fn_ref = luaL_ref(s_L, LUA_REGISTRYINDEX);
    s_ready = true;
    set_err("ok");
    return ESP_OK;
}

static pm_rgb_t color_from_lua(lua_State *L, int idx, float intensity)
{
    pm_rgb_t rgb = {0, 0, 0};
    intensity = clampf(intensity, 0.0f, 1.0f);

    if (lua_istable(L, idx)) {
        lua_getfield(L, idx, "t");
        const char *tag = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
        lua_pop(L, 1);
        if (strcmp(tag, "rgb") == 0) {
            lua_getfield(L, idx, "r"); float r = (float)lua_tonumber(L, -1); lua_pop(L, 1);
            lua_getfield(L, idx, "g"); float g = (float)lua_tonumber(L, -1); lua_pop(L, 1);
            lua_getfield(L, idx, "b"); float b = (float)lua_tonumber(L, -1); lua_pop(L, 1);
            rgb.r = (uint8_t)clampf(r * intensity, 0, 255);
            rgb.g = (uint8_t)clampf(g * intensity, 0, 255);
            rgb.b = (uint8_t)clampf(b * intensity, 0, 255);
            return rgb;
        }
        /* hsv or bare {h,s,v} */
        lua_getfield(L, idx, "h"); float h = (float)lua_tonumber(L, -1); lua_pop(L, 1);
        lua_getfield(L, idx, "s"); float s = (float)lua_tonumber(L, -1); lua_pop(L, 1);
        lua_getfield(L, idx, "v"); float v = (float)lua_tonumber(L, -1); lua_pop(L, 1);
        pm_hsv_t hsv = {
            .h = (uint8_t)((int)h & 255),
            .s = (uint8_t)clampf(s, 0, 255),
            .v = (uint8_t)clampf(v * intensity, 0, 255),
        };
        return pm_hsv_to_rgb(hsv);
    }

    if (lua_isnumber(L, idx)) {
        /* Treat single number as V in primary red hue */
        float v = (float)lua_tonumber(L, idx);
        pm_hsv_t hsv = {.h = 0, .s = 255, .v = (uint8_t)clampf(v * intensity, 0, 255)};
        return pm_hsv_to_rgb(hsv);
    }
    return rgb;
}

esp_err_t pm_effect_lua_init(void)
{
    if (s_L) return ESP_OK;
    s_mu = xSemaphoreCreateMutex();
    if (!s_mu) return ESP_ERR_NO_MEM;

    memset(s_script, 0, sizeof(s_script));
    load_script_from_nvs();

    s_L = luaL_newstate();
    if (!s_L) return ESP_ERR_NO_MEM;
    open_safe_libs(s_L);
    register_helpers(s_L);

    xSemaphoreTake(s_mu, portMAX_DELAY);
    esp_err_t err = compile_locked(s_script);
    xSemaphoreGive(s_mu);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "default/script compile: %s", s_err);
    } else {
        ESP_LOGI(TAG, "Lua effect runtime ready");
    }
    return ESP_OK;
}

esp_err_t pm_effect_lua_set_script(const char *src)
{
    if (!src) return ESP_ERR_INVALID_ARG;
    size_t len = strlen(src);
    if (len >= PM_LUA_SCRIPT_MAX) return ESP_ERR_INVALID_SIZE;

    if (!s_L) {
        esp_err_t e = pm_effect_lua_init();
        if (e != ESP_OK) return e;
    }

    xSemaphoreTake(s_mu, portMAX_DELAY);
    strncpy(s_script, src, sizeof(s_script) - 1);
    s_script[sizeof(s_script) - 1] = 0;
    esp_err_t err = compile_locked(s_script);
    xSemaphoreGive(s_mu);
    if (err != ESP_OK) return err;

    err = persist_script(s_script);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS persist failed: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}

const char *pm_effect_lua_get_script(void)
{
    if (s_script[0] == 0) {
        strncpy(s_script, DEFAULT_SCRIPT, sizeof(s_script) - 1);
    }
    return s_script;
}

bool pm_effect_lua_ready(void)
{
    return s_ready && s_fn_ref != LUA_NOREF;
}

const char *pm_effect_lua_last_error(void)
{
    return s_err[0] ? s_err : "ok";
}

pm_rgb_t pm_effect_lua_eval(const pm_lua_effect_inputs_t *in,
                            uint16_t i, uint16_t count,
                            float x, float y, float z, float t)
{
    pm_rgb_t black = {0, 0, 0};
    if (!in || !s_L || !pm_effect_lua_ready()) return black;

    if (xSemaphoreTake(s_mu, pdMS_TO_TICKS(5)) != pdTRUE) return black;

    lua_rawgeti(s_L, LUA_REGISTRYINDEX, s_fn_ref);

    float inten = in->intensity;
    if (inten < 0.0f) inten = 0.0f;
    if (inten > 1.0f) inten = 1.0f;

    lua_pushnumber(s_L, t); lua_setglobal(s_L, "t");
    lua_pushnumber(s_L, in->speed); lua_setglobal(s_L, "speed");
    lua_pushnumber(s_L, in->scale); lua_setglobal(s_L, "scale");
    lua_pushnumber(s_L, inten); lua_setglobal(s_L, "intensity");
    lua_pushinteger(s_L, i); lua_setglobal(s_L, "i");
    lua_pushinteger(s_L, count); lua_setglobal(s_L, "count");
    lua_pushnumber(s_L, x); lua_setglobal(s_L, "x");
    lua_pushnumber(s_L, y); lua_setglobal(s_L, "y");
    lua_pushnumber(s_L, z); lua_setglobal(s_L, "z");
    lua_pushnumber(s_L, in->ph); lua_setglobal(s_L, "ph");
    lua_pushnumber(s_L, in->ps); lua_setglobal(s_L, "ps");
    lua_pushnumber(s_L, in->pv); lua_setglobal(s_L, "pv");
    lua_pushnumber(s_L, in->sh); lua_setglobal(s_L, "sh");
    lua_pushnumber(s_L, in->ss); lua_setglobal(s_L, "ss");
    lua_pushnumber(s_L, in->sv); lua_setglobal(s_L, "sv");

    static const char *pnames[PM_FX_P_COUNT] = {
        "p1", "p2", "p3", "p4", "p5", "p6", "p7", "p8"
    };
    lua_createtable(s_L, PM_FX_P_COUNT, 0);
    for (int k = 0; k < PM_FX_P_COUNT; ++k) {
        float pv = in->p[k];
        if (pv < 0.0f) pv = 0.0f;
        if (pv > 1.0f) pv = 1.0f;
        lua_pushnumber(s_L, pv);
        lua_setglobal(s_L, pnames[k]);
        lua_pushnumber(s_L, pv);
        lua_rawseti(s_L, -2, k + 1);
    }
    lua_setglobal(s_L, "p");

    s_hook_budget = 8000;
    lua_sethook(s_L, hook_count, LUA_MASKCOUNT, 200);

    pm_rgb_t rgb = black;
    if (lua_pcall(s_L, 0, 1, 0) != LUA_OK) {
        set_err(lua_tostring(s_L, -1));
        lua_pop(s_L, 1);
        s_ready = false; /* stop hammering a broken script */
    } else {
        rgb = color_from_lua(s_L, -1, inten);
        lua_pop(s_L, 1);
    }

    lua_sethook(s_L, NULL, 0, 0);
    xSemaphoreGive(s_mu);
    return rgb;
}
