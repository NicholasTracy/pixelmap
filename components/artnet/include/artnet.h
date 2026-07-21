#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pm_artnet_dmx_cb_t)(uint16_t universe, const uint8_t *data, uint16_t len, void *user);

typedef struct {
    uint16_t listen_port;     /* default 6454 */
    uint16_t universe_start;  /* first universe mapped to pixel 0 */
    uint16_t universe_count;
    const char *short_name;   /* ArtPollReply short name (≤17) */
    const char *long_name;    /* ArtPollReply long name (≤63) */
    pm_artnet_dmx_cb_t on_dmx;
    void *user;
} pm_artnet_config_t;

esp_err_t pm_artnet_start(const pm_artnet_config_t *cfg);
void pm_artnet_stop(void);
bool pm_artnet_active(uint32_t timeout_ms);
uint32_t pm_artnet_last_packet_ms(void);

#ifdef __cplusplus
}
#endif
