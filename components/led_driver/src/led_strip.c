#include "led_strip.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "led_strip";

struct pm_led_strip {
    pm_led_strip_config_t cfg;
    const pm_chipset_timing_t *timing;
    pm_color_correction_t correction;
    uint8_t *pixels;          /* logical RGB or RGBW per pixel */
    uint8_t channels;
    rmt_channel_handle_t rmt_chan;
    rmt_encoder_handle_t bytes_encoder;
    rmt_encoder_handle_t copy_encoder;
    rmt_symbol_word_t reset_symbol;
    uint16_t t0h_ticks;
    uint16_t t0l_ticks;
    uint16_t t1h_ticks;
    uint16_t t1l_ticks;
    uint8_t *tx_buf;
    size_t tx_buf_len;
};

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t *bytes_encoder;
    rmt_encoder_t *copy_encoder;
    int state;
    rmt_symbol_word_t reset;
} pm_led_encoder_t;

static size_t encode_led(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                         const void *primary_data, size_t data_size,
                         rmt_encode_state_t *ret_state)
{
    pm_led_encoder_t *led = __containerof(encoder, pm_led_encoder_t, base);
    rmt_encode_state_t session = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded = 0;

    switch (led->state) {
    case 0:
        encoded += led->bytes_encoder->encode(led->bytes_encoder, channel,
                                              primary_data, data_size, &session);
        if (session & RMT_ENCODING_COMPLETE) {
            led->state = 1;
        }
        if (session & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out;
        }
        /* fallthrough */
    case 1:
        encoded += led->copy_encoder->encode(led->copy_encoder, channel,
                                             &led->reset, sizeof(led->reset), &session);
        if (session & RMT_ENCODING_COMPLETE) {
            led->state = 0;
            state |= RMT_ENCODING_COMPLETE;
        }
        if (session & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out;
        }
        break;
    }
out:
    *ret_state = state;
    return encoded;
}

static esp_err_t reset_led_encoder(rmt_encoder_t *encoder)
{
    pm_led_encoder_t *led = __containerof(encoder, pm_led_encoder_t, base);
    rmt_encoder_reset(led->bytes_encoder);
    rmt_encoder_reset(led->copy_encoder);
    led->state = 0;
    return ESP_OK;
}

static esp_err_t del_led_encoder(rmt_encoder_t *encoder)
{
    pm_led_encoder_t *led = __containerof(encoder, pm_led_encoder_t, base);
    rmt_del_encoder(led->bytes_encoder);
    rmt_del_encoder(led->copy_encoder);
    free(led);
    return ESP_OK;
}

static esp_err_t make_led_encoder(pm_led_strip_t *strip, rmt_encoder_handle_t *ret)
{
    pm_led_encoder_t *led = calloc(1, sizeof(*led));
    ESP_RETURN_ON_FALSE(led, ESP_ERR_NO_MEM, TAG, "encoder alloc");

    led->base.encode = encode_led;
    led->base.reset = reset_led_encoder;
    led->base.del = del_led_encoder;
    led->reset = strip->reset_symbol;

    rmt_bytes_encoder_config_t bytes_cfg = {
        .bit0 = {
            .level0 = 1,
            .duration0 = strip->t0h_ticks,
            .level1 = 0,
            .duration1 = strip->t0l_ticks,
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = strip->t1h_ticks,
            .level1 = 0,
            .duration1 = strip->t1l_ticks,
        },
        .flags.msb_first = 1,
    };
    ESP_RETURN_ON_ERROR(rmt_new_bytes_encoder(&bytes_cfg, &led->bytes_encoder), TAG, "bytes enc");
    rmt_copy_encoder_config_t copy_cfg = {};
    ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&copy_cfg, &led->copy_encoder), TAG, "copy enc");

    *ret = &led->base;
    return ESP_OK;
}

static uint16_t ns_to_ticks(uint32_t ns, uint32_t res_hz)
{
    uint32_t ticks = (uint32_t)(((uint64_t)ns * res_hz) / 1000000000ULL);
    if (ticks < 1) ticks = 1;
    if (ticks > 32767) ticks = 32767;
    return (uint16_t)ticks;
}

esp_err_t pm_led_strip_create(const pm_led_strip_config_t *cfg, pm_led_strip_t **out)
{
    ESP_RETURN_ON_FALSE(cfg && out, ESP_ERR_INVALID_ARG, TAG, "bad args");
    ESP_RETURN_ON_FALSE(cfg->pixel_count > 0, ESP_ERR_INVALID_ARG, TAG, "count");

    pm_led_strip_t *s = calloc(1, sizeof(*s));
    ESP_RETURN_ON_FALSE(s, ESP_ERR_NO_MEM, TAG, "strip");

    s->cfg = *cfg;
    s->timing = pm_chipset_timing(cfg->chipset);
    s->channels = s->timing->channels;
    /* Prefer explicit order from config; fall back to chipset default when unset sentinel used */
    if (cfg->color_order == (pm_color_order_t)0xFF) {
        s->cfg.color_order = s->timing->default_order;
    }
    s->correction = (pm_color_correction_t){
        .correction = {255, 255, 255},
        .temperature = {255, 255, 255},
        .gamma = 220,
        .brightness = 128,
        .auto_white = true,
        .warm_mix = 128,
    };

    size_t px_bytes = (size_t)cfg->pixel_count * s->channels;
    s->pixels = heap_caps_calloc(1, px_bytes, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(s->pixels, ESP_ERR_NO_MEM, TAG, "pixels");
    s->tx_buf_len = px_bytes;
    s->tx_buf = heap_caps_malloc(s->tx_buf_len, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(s->tx_buf, ESP_ERR_NO_MEM, TAG, "tx");

    if (s->timing->protocol == PM_BUS_PROTOCOL_SPI_CLOCKED) {
        ESP_LOGW(TAG, "APA102/SK9822 path uses RMT bit-bang clocked mode placeholder; prefer SPI host binding in board config");
    }

    uint32_t res_hz = (cfg->rmt_resolution_hz_mhz ? cfg->rmt_resolution_hz_mhz : 10) * 1000000U;
    s->t0h_ticks = ns_to_ticks(s->timing->t0h_ns, res_hz);
    s->t0l_ticks = ns_to_ticks(s->timing->t0l_ns, res_hz);
    s->t1h_ticks = ns_to_ticks(s->timing->t1h_ns, res_hz);
    s->t1l_ticks = ns_to_ticks(s->timing->t1l_ns, res_hz);

    uint32_t reset_ticks = (s->timing->reset_us * (res_hz / 1000U)) / 1000U;
    if (reset_ticks < 1) reset_ticks = 1;
    s->reset_symbol = (rmt_symbol_word_t){
        .level0 = 0,
        .duration0 = reset_ticks > 32767 ? 32767 : reset_ticks,
        .level1 = 0,
        .duration1 = 0,
    };

    rmt_tx_channel_config_t tx_cfg = {
        .gpio_num = cfg->gpio_data,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = res_hz,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_cfg, &s->rmt_chan), TAG, "rmt chan");
    ESP_RETURN_ON_ERROR(make_led_encoder(s, &s->bytes_encoder), TAG, "encoder");
    ESP_RETURN_ON_ERROR(rmt_enable(s->rmt_chan), TAG, "rmt en");

    *out = s;
    ESP_LOGI(TAG, "strip ready: %s gpio=%d n=%u ch=%u",
             pm_chipset_name(cfg->chipset), cfg->gpio_data, cfg->pixel_count, s->channels);
    return ESP_OK;
}

void pm_led_strip_destroy(pm_led_strip_t *strip)
{
    if (!strip) return;
    if (strip->rmt_chan) {
        rmt_disable(strip->rmt_chan);
        rmt_del_channel(strip->rmt_chan);
    }
    if (strip->bytes_encoder) {
        rmt_del_encoder(strip->bytes_encoder);
    }
    free(strip->tx_buf);
    free(strip->pixels);
    free(strip);
}

uint16_t pm_led_strip_length(const pm_led_strip_t *strip)
{
    return strip ? strip->cfg.pixel_count : 0;
}

pm_color_mode_t pm_led_strip_color_mode(const pm_led_strip_t *strip)
{
    if (!strip) return PM_COLOR_MODE_RGB;
    if (strip->channels >= 5) return PM_COLOR_MODE_RGBWW;
    if (strip->channels == 4) return PM_COLOR_MODE_RGBW;
    return PM_COLOR_MODE_RGB;
}

void pm_led_strip_clear(pm_led_strip_t *strip)
{
    if (!strip) return;
    memset(strip->pixels, 0, (size_t)strip->cfg.pixel_count * strip->channels);
}

void pm_led_strip_set_correction(pm_led_strip_t *strip, const pm_color_correction_t *cc)
{
    if (strip && cc) strip->correction = *cc;
}

const pm_color_correction_t *pm_led_strip_get_correction(const pm_led_strip_t *strip)
{
    return strip ? &strip->correction : NULL;
}

void pm_led_strip_set_rgb(pm_led_strip_t *strip, uint16_t index, pm_rgb_t c)
{
    if (!strip || index >= strip->cfg.pixel_count) return;
    uint8_t *p = &strip->pixels[(size_t)index * strip->channels];
    if (strip->channels >= 5) {
        pm_rgbww_t ww = pm_rgb_to_rgbww(c, strip->correction.auto_white, strip->correction.warm_mix);
        p[0] = ww.r; p[1] = ww.g; p[2] = ww.b; p[3] = ww.w1; p[4] = ww.w2;
    } else if (strip->channels == 4) {
        pm_rgbw_t w = pm_rgb_to_rgbw(c, strip->correction.auto_white);
        p[0] = w.r; p[1] = w.g; p[2] = w.b; p[3] = w.w;
    } else {
        p[0] = c.r; p[1] = c.g; p[2] = c.b;
    }
}

void pm_led_strip_set_rgbw(pm_led_strip_t *strip, uint16_t index, pm_rgbw_t c)
{
    if (!strip || index >= strip->cfg.pixel_count) return;
    uint8_t *p = &strip->pixels[(size_t)index * strip->channels];
    p[0] = c.r; p[1] = c.g; p[2] = c.b;
    if (strip->channels >= 4) p[3] = c.w;
}

void pm_led_strip_fill_rgb(pm_led_strip_t *strip, pm_rgb_t c)
{
    if (!strip) return;
    for (uint16_t i = 0; i < strip->cfg.pixel_count; ++i) {
        pm_led_strip_set_rgb(strip, i, c);
    }
}

esp_err_t pm_led_strip_show(pm_led_strip_t *strip)
{
    ESP_RETURN_ON_FALSE(strip, ESP_ERR_INVALID_ARG, TAG, "null");

    size_t out_i = 0;
    for (uint16_t i = 0; i < strip->cfg.pixel_count; ++i) {
        uint8_t *src = &strip->pixels[(size_t)i * strip->channels];
        pm_rgb_t rgb = {src[0], src[1], src[2]};
        rgb = pm_apply_correction(rgb, &strip->correction);

        uint8_t ch[5] = {rgb.r, rgb.g, rgb.b, 0, 0};
        if (strip->channels >= 4) {
            /* re-derive whites after RGB correction */
            if (strip->channels >= 5) {
                pm_rgbww_t ww = pm_rgb_to_rgbww(rgb, strip->correction.auto_white, strip->correction.warm_mix);
                ch[0] = ww.r; ch[1] = ww.g; ch[2] = ww.b; ch[3] = ww.w1; ch[4] = ww.w2;
            } else {
                pm_rgbw_t w = pm_rgb_to_rgbw(rgb, strip->correction.auto_white);
                ch[0] = w.r; ch[1] = w.g; ch[2] = w.b; ch[3] = w.w;
            }
        }

        uint8_t packed[5];
        int plen = 0;
        pm_pack_pixel(ch, strip->channels, strip->cfg.color_order, packed, &plen);
        memcpy(&strip->tx_buf[out_i], packed, (size_t)plen);
        out_i += (size_t)plen;
    }

    rmt_transmit_config_t txcfg = {.loop_count = 0};
    ESP_RETURN_ON_ERROR(rmt_transmit(strip->rmt_chan, strip->bytes_encoder,
                                     strip->tx_buf, out_i, &txcfg), TAG, "tx");
    return rmt_tx_wait_all_done(strip->rmt_chan, 100);
}
