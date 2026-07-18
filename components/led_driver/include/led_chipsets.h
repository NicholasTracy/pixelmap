#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "color_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PM_CHIPSET_WS2812 = 0,
    PM_CHIPSET_WS2812B,
    PM_CHIPSET_WS2811,
    PM_CHIPSET_WS2813,
    PM_CHIPSET_SK6812_RGBW,
    PM_CHIPSET_TM1814,
    PM_CHIPSET_APA102,
    PM_CHIPSET_SK9822,
    PM_CHIPSET_CUSTOM,
} pm_chipset_t;

typedef enum {
    PM_BUS_PROTOCOL_NRZ_800KHZ = 0, /* WS281x family via RMT */
    PM_BUS_PROTOCOL_NRZ_400KHZ,     /* WS2811 slow */
    PM_BUS_PROTOCOL_SPI_CLOCKED,    /* APA102 / SK9822 */
} pm_bus_protocol_t;

/* Nanosecond timing for one NRZ bit (T0H/T0L/T1H/T1L) + reset */
typedef struct {
    uint16_t t0h_ns;
    uint16_t t0l_ns;
    uint16_t t1h_ns;
    uint16_t t1l_ns;
    uint16_t reset_us;
    uint8_t channels;          /* 3 = RGB, 4 = RGBW, 5 = RGBWW */
    pm_bus_protocol_t protocol;
    pm_color_order_t default_order;
    bool needs_clock;
} pm_chipset_timing_t;

const pm_chipset_timing_t *pm_chipset_timing(pm_chipset_t chipset);
const char *pm_chipset_name(pm_chipset_t chipset);
pm_chipset_t pm_chipset_from_name(const char *name);

#ifdef __cplusplus
}
#endif
