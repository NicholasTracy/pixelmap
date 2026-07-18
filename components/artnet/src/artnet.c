#include "artnet.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <unistd.h>

static const char *TAG = "artnet";

static TaskHandle_t s_task;
static int s_sock = -1;
static pm_artnet_config_t s_cfg;
static int64_t s_last_us;

#pragma pack(push, 1)
typedef struct {
    char id[8];
    uint16_t opcode;
    uint16_t protver_hi_lo; /* big-endian style in Art-Net: hi then lo as bytes */
    uint8_t sequence;
    uint8_t physical;
    uint16_t universe; /* little-endian */
    uint16_t length;   /* big-endian */
} art_dmx_hdr_t;
#pragma pack(pop)

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static void artnet_task(void *arg)
{
    (void)arg;
    uint8_t buf[530];
    while (1) {
        struct sockaddr_in src;
        socklen_t slen = sizeof(src);
        int n = recvfrom(s_sock, buf, sizeof(buf), 0, (struct sockaddr *)&src, &slen);
        if (n < (int)sizeof(art_dmx_hdr_t)) {
            continue;
        }
        if (memcmp(buf, "Art-Net", 7) != 0) {
            continue;
        }
        uint16_t opcode = buf[8] | (buf[9] << 8);
        if (opcode != 0x5000) { /* OpOutput / ArtDmx */
            continue;
        }
        uint16_t universe = buf[14] | (buf[15] << 8);
        uint16_t length = be16(&buf[16]);
        if (length > 512) length = 512;
        if (n < 18 + length) continue;

        if (universe < s_cfg.universe_start ||
            universe >= s_cfg.universe_start + s_cfg.universe_count) {
            continue;
        }
        s_last_us = esp_timer_get_time();
        if (s_cfg.on_dmx) {
            s_cfg.on_dmx(universe, &buf[18], length, s_cfg.user);
        }
    }
}

esp_err_t pm_artnet_start(const pm_artnet_config_t *cfg)
{
    if (!cfg || !cfg->on_dmx) return ESP_ERR_INVALID_ARG;
    if (s_task) return ESP_ERR_INVALID_STATE;
    s_cfg = *cfg;
    if (s_cfg.listen_port == 0) s_cfg.listen_port = 6454;
    if (s_cfg.universe_count == 0) s_cfg.universe_count = 4;

    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) return ESP_FAIL;

    int yes = 1;
    setsockopt(s_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(s_cfg.listen_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }

    xTaskCreate(artnet_task, "artnet", 4096, NULL, 5, &s_task);
    ESP_LOGI(TAG, "listening UDP %u, universes %u..%u",
             s_cfg.listen_port, s_cfg.universe_start,
             s_cfg.universe_start + s_cfg.universe_count - 1);
    return ESP_OK;
}

void pm_artnet_stop(void)
{
    if (s_task) {
        vTaskDelete(s_task);
        s_task = NULL;
    }
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }
}

uint32_t pm_artnet_last_packet_ms(void)
{
    return (uint32_t)(s_last_us / 1000);
}

bool pm_artnet_active(uint32_t timeout_ms)
{
    if (s_last_us == 0) return false;
    int64_t age = (esp_timer_get_time() - s_last_us) / 1000;
    return age >= 0 && age < (int64_t)timeout_ms;
}
