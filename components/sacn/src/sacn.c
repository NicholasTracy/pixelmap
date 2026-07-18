#include "sacn.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/igmp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *TAG = "sacn";

#define SACN_PORT 5568
#define ACN_PACKET_ID "ASC-E1.17\0\0\0"

static TaskHandle_t s_task;
static int s_sock = -1;
static pm_sacn_config_t s_cfg;
static int64_t s_last_us;

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

static void join_universe_mcast(uint16_t universe)
{
    /* 239.255.(universe>>8).(universe&0xFF) */
    struct ip_mreq mreq = {0};
    char addr[16];
    snprintf(addr, sizeof(addr), "239.255.%u.%u", (universe >> 8) & 0xFF, universe & 0xFF);
    mreq.imr_multiaddr.s_addr = inet_addr(addr);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(s_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
}

static void sacn_task(void *arg)
{
    (void)arg;
    uint8_t buf[638];
    while (1) {
        int n = recv(s_sock, buf, sizeof(buf), 0);
        if (n < 126) continue;

        /* Root vector + framing: validate ACN packet identifier at offset 4 */
        if (memcmp(&buf[4], ACN_PACKET_ID, 12) != 0) continue;

        /* DMP layer starts after root(38) + framing(77) = typically universe at 113 */
        /* E1.31: universe is bytes 113-114 (1-based index in many docs: offset 113) */
        if (n < 126) continue;
        uint16_t universe = be16(&buf[113]);
        uint16_t prop_count = be16(&buf[123]); /* includes start code */
        if (prop_count < 1 || prop_count > 513) continue;
        uint16_t dmx_len = (uint16_t)(prop_count - 1);
        const uint8_t *dmx = &buf[126]; /* skip start code at 125 */
        if (n < 126 + dmx_len) continue;

        if (universe < s_cfg.universe_start ||
            universe >= s_cfg.universe_start + s_cfg.universe_count) {
            continue;
        }

        s_last_us = esp_timer_get_time();
        if (s_cfg.on_dmx) {
            s_cfg.on_dmx(universe, dmx, dmx_len, s_cfg.user);
        }
    }
}

esp_err_t pm_sacn_start(const pm_sacn_config_t *cfg)
{
    if (!cfg || !cfg->on_dmx) return ESP_ERR_INVALID_ARG;
    if (s_task) return ESP_ERR_INVALID_STATE;
    s_cfg = *cfg;
    if (s_cfg.universe_count == 0) s_cfg.universe_count = 4;

    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) return ESP_FAIL;
    int yes = 1;
    setsockopt(s_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SACN_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }

    if (s_cfg.join_multicast) {
        for (uint16_t u = 0; u < s_cfg.universe_count; ++u) {
            join_universe_mcast((uint16_t)(s_cfg.universe_start + u));
        }
    }

    xTaskCreate(sacn_task, "sacn", 4096, NULL, 5, &s_task);
    ESP_LOGI(TAG, "E1.31 listening, universes %u..%u",
             s_cfg.universe_start, s_cfg.universe_start + s_cfg.universe_count - 1);
    return ESP_OK;
}

void pm_sacn_stop(void)
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

bool pm_sacn_active(uint32_t timeout_ms)
{
    if (s_last_us == 0) return false;
    int64_t age = (esp_timer_get_time() - s_last_us) / 1000;
    return age >= 0 && age < (int64_t)timeout_ms;
}
