#include "audio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

static const char *TAG = "audio";

#define SAMPLE_RATE 16000
#define FFT_N 256
#define FFT_HALF (FFT_N / 2)

static i2s_chan_handle_t s_rx;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_mtx;
static pm_audio_levels_t s_levels;
static pm_audio_config_t s_cfg;
static bool s_running;
static float s_agc = 1.0f;
static float s_env;
static float s_beat_hold;
static float s_prev_flux;

/* In-place radix-2 complex FFT (real input packed as re/im interleaved). */
static void fft_radix2(float *re, float *im, int n)
{
    int j = 0;
    for (int i = 0; i < n; ++i) {
        if (i < j) {
            float tr = re[i]; re[i] = re[j]; re[j] = tr;
            float ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
        int m = n >> 1;
        while (m >= 1 && j >= m) {
            j -= m;
            m >>= 1;
        }
        j += m;
    }
    for (int len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * (float)M_PI / (float)len;
        float wlen_r = cosf(ang);
        float wlen_i = sinf(ang);
        for (int i = 0; i < n; i += len) {
            float wr = 1.0f, wi = 0.0f;
            for (int k = 0; k < len / 2; ++k) {
                int i0 = i + k;
                int i1 = i0 + len / 2;
                float tr = wr * re[i1] - wi * im[i1];
                float ti = wr * im[i1] + wi * re[i1];
                re[i1] = re[i0] - tr;
                im[i1] = im[i0] - ti;
                re[i0] += tr;
                im[i0] += ti;
                float nwr = wr * wlen_r - wi * wlen_i;
                wi = wr * wlen_i + wi * wlen_r;
                wr = nwr;
            }
        }
    }
}

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void process_block(const int32_t *samples, int n)
{
    static float re[FFT_N];
    static float im[FFT_N];
    if (n > FFT_N) n = FFT_N;

    float gain = (s_cfg.gain < 1 ? 1 : s_cfg.gain) / 64.0f;
    float squelch = s_cfg.squelch / 255.0f * 0.05f;

    for (int i = 0; i < FFT_N; ++i) {
        float s = 0.0f;
        if (i < n) {
            /* INMP441: 24-bit left-justified in 32-bit slot */
            s = (float)(samples[i] >> 8) * (1.0f / 8388608.0f);
        }
        float w = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * (float)i / (float)(FFT_N - 1)));
        re[i] = s * w * gain * s_agc;
        im[i] = 0.0f;
    }

    fft_radix2(re, im, FFT_N);

    float mags[FFT_HALF];
    float peak = 1e-6f;
    for (int i = 0; i < FFT_HALF; ++i) {
        float m = sqrtf(re[i] * re[i] + im[i] * im[i]);
        mags[i] = m;
        if (m > peak) peak = m;
    }

    /* Slow AGC toward ~0.35 peak */
    float target = 0.35f / peak;
    s_agc = s_agc * 0.98f + target * 0.02f;
    s_agc = clampf(s_agc, 0.25f, 32.0f);

    float bins[PM_AUDIO_BINS];
    for (int b = 0; b < PM_AUDIO_BINS; ++b) {
        int i0 = 1 + (b * (FFT_HALF - 1)) / PM_AUDIO_BINS;
        int i1 = 1 + ((b + 1) * (FFT_HALF - 1)) / PM_AUDIO_BINS;
        if (i1 <= i0) i1 = i0 + 1;
        float sum = 0.0f;
        for (int i = i0; i < i1 && i < FFT_HALF; ++i) sum += mags[i];
        bins[b] = sum / (float)(i1 - i0);
    }

    float bass = 0, mid = 0, tre = 0;
    for (int b = 0; b < 4; ++b) bass += bins[b];
    for (int b = 4; b < 10; ++b) mid += bins[b];
    for (int b = 10; b < PM_AUDIO_BINS; ++b) tre += bins[b];
    bass *= 0.25f;
    mid *= (1.0f / 6.0f);
    tre *= (1.0f / 6.0f);

    float vol = 0.0f;
    for (int b = 0; b < PM_AUDIO_BINS; ++b) vol += bins[b];
    vol /= (float)PM_AUDIO_BINS;

    if (vol < squelch) {
        vol = bass = mid = tre = 0.0f;
        memset(bins, 0, sizeof(bins));
    }

    s_env = s_env * 0.85f + vol * 0.15f;
    float flux = vol - s_prev_flux;
    s_prev_flux = vol;
    if (flux > 0.08f + s_env * 0.15f && s_beat_hold <= 0.0f) {
        s_beat_hold = 0.12f; /* ~120 ms */
    }
    if (s_beat_hold > 0.0f) s_beat_hold -= (float)FFT_N / (float)SAMPLE_RATE;

    float norm = 1.0f / fmaxf(s_env * 2.5f, 1e-3f);
    pm_audio_levels_t out = {0};
    out.active = true;
    out.volume = clampf(vol * norm, 0.0f, 1.0f);
    out.bass = clampf(bass * norm, 0.0f, 1.0f);
    out.mid = clampf(mid * norm, 0.0f, 1.0f);
    out.treble = clampf(tre * norm, 0.0f, 1.0f);
    out.beat = s_beat_hold > 0.0f;
    for (int b = 0; b < PM_AUDIO_BINS; ++b) {
        out.bins[b] = clampf(bins[b] * norm, 0.0f, 1.0f);
    }

    if (s_mtx && xSemaphoreTake(s_mtx, pdMS_TO_TICKS(5)) == pdTRUE) {
        s_levels = out;
        xSemaphoreGive(s_mtx);
    }
}

static void audio_task(void *arg)
{
    (void)arg;
    int32_t buf[FFT_N];
    size_t bytes = 0;
    while (s_running) {
        esp_err_t err = i2s_channel_read(s_rx, buf, sizeof(buf), &bytes, pdMS_TO_TICKS(100));
        if (err != ESP_OK || bytes < sizeof(int32_t) * 16) continue;
        int n = (int)(bytes / sizeof(int32_t));
        process_block(buf, n);
    }
    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t pm_audio_start(const pm_audio_config_t *cfg)
{
    if (!cfg || !cfg->enable) return ESP_ERR_INVALID_ARG;
    if (cfg->gpio_ws < 0 || cfg->gpio_sck < 0 || cfg->gpio_sd < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_running) pm_audio_stop();

    if (!s_mtx) s_mtx = xSemaphoreCreateMutex();
    s_cfg = *cfg;
    s_agc = 4.0f;
    s_env = 0.0f;
    s_beat_hold = 0.0f;
    s_prev_flux = 0.0f;
    memset(&s_levels, 0, sizeof(s_levels));

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &s_rx);
    if (err != ESP_OK) return err;

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = cfg->gpio_sck,
            .ws = cfg->gpio_ws,
            .dout = I2S_GPIO_UNUSED,
            .din = cfg->gpio_sd,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    err = i2s_channel_init_std_mode(s_rx, &std_cfg);
    if (err != ESP_OK) {
        i2s_del_channel(s_rx);
        s_rx = NULL;
        return err;
    }
    err = i2s_channel_enable(s_rx);
    if (err != ESP_OK) {
        i2s_del_channel(s_rx);
        s_rx = NULL;
        return err;
    }

    s_running = true;
    if (xTaskCreate(audio_task, "audio", 4096, NULL, 5, &s_task) != pdPASS) {
        s_running = false;
        i2s_channel_disable(s_rx);
        i2s_del_channel(s_rx);
        s_rx = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "I2S mic WS=%d SCK=%d SD=%d @ %d Hz",
             cfg->gpio_ws, cfg->gpio_sck, cfg->gpio_sd, SAMPLE_RATE);
    return ESP_OK;
}

void pm_audio_stop(void)
{
    if (!s_running && !s_rx) return;
    s_running = false;
    for (int i = 0; i < 50 && s_task; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_rx) {
        i2s_channel_disable(s_rx);
        i2s_del_channel(s_rx);
        s_rx = NULL;
    }
    if (s_mtx && xSemaphoreTake(s_mtx, pdMS_TO_TICKS(50)) == pdTRUE) {
        memset(&s_levels, 0, sizeof(s_levels));
        xSemaphoreGive(s_mtx);
    }
    ESP_LOGI(TAG, "audio stopped");
}

bool pm_audio_running(void) { return s_running; }

void pm_audio_get_levels(pm_audio_levels_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!s_mtx) return;
    if (xSemaphoreTake(s_mtx, pdMS_TO_TICKS(5)) == pdTRUE) {
        *out = s_levels;
        xSemaphoreGive(s_mtx);
    }
}
