#include "led_chipsets.h"
#include <ctype.h>
#include <string.h>

static int ieq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        ++a; ++b;
    }
    return *a == 0 && *b == 0;
}

static const pm_chipset_timing_t k_timings[] = {
    [PM_CHIPSET_WS2812] = {
        .t0h_ns = 350, .t0l_ns = 800, .t1h_ns = 700, .t1l_ns = 600,
        .reset_us = 50, .channels = 3,
        .protocol = PM_BUS_PROTOCOL_NRZ_800KHZ,
        .default_order = PM_COLOR_ORDER_GRB, .needs_clock = false,
    },
    [PM_CHIPSET_WS2812B] = {
        .t0h_ns = 400, .t0l_ns = 850, .t1h_ns = 800, .t1l_ns = 450,
        .reset_us = 50, .channels = 3,
        .protocol = PM_BUS_PROTOCOL_NRZ_800KHZ,
        .default_order = PM_COLOR_ORDER_GRB, .needs_clock = false,
    },
    [PM_CHIPSET_WS2811] = {
        .t0h_ns = 500, .t0l_ns = 2000, .t1h_ns = 1200, .t1l_ns = 1300,
        .reset_us = 50, .channels = 3,
        .protocol = PM_BUS_PROTOCOL_NRZ_400KHZ,
        .default_order = PM_COLOR_ORDER_RGB, .needs_clock = false,
    },
    [PM_CHIPSET_WS2813] = {
        .t0h_ns = 350, .t0l_ns = 800, .t1h_ns = 700, .t1l_ns = 600,
        .reset_us = 300, .channels = 3,
        .protocol = PM_BUS_PROTOCOL_NRZ_800KHZ,
        .default_order = PM_COLOR_ORDER_GRB, .needs_clock = false,
    },
    [PM_CHIPSET_SK6812_RGBW] = {
        .t0h_ns = 300, .t0l_ns = 900, .t1h_ns = 600, .t1l_ns = 600,
        .reset_us = 80, .channels = 4,
        .protocol = PM_BUS_PROTOCOL_NRZ_800KHZ,
        .default_order = PM_COLOR_ORDER_GRBW, .needs_clock = false,
    },
    [PM_CHIPSET_TM1814] = {
        .t0h_ns = 400, .t0l_ns = 850, .t1h_ns = 800, .t1l_ns = 450,
        .reset_us = 200, .channels = 4,
        .protocol = PM_BUS_PROTOCOL_NRZ_800KHZ,
        .default_order = PM_COLOR_ORDER_RGBW, .needs_clock = false,
    },
    [PM_CHIPSET_APA102] = {
        .t0h_ns = 0, .t0l_ns = 0, .t1h_ns = 0, .t1l_ns = 0,
        .reset_us = 0, .channels = 3,
        .protocol = PM_BUS_PROTOCOL_SPI_CLOCKED,
        .default_order = PM_COLOR_ORDER_BGR, .needs_clock = true,
    },
    [PM_CHIPSET_SK9822] = {
        .t0h_ns = 0, .t0l_ns = 0, .t1h_ns = 0, .t1l_ns = 0,
        .reset_us = 0, .channels = 3,
        .protocol = PM_BUS_PROTOCOL_SPI_CLOCKED,
        .default_order = PM_COLOR_ORDER_BGR, .needs_clock = true,
    },
};

const pm_chipset_timing_t *pm_chipset_timing(pm_chipset_t chipset)
{
    if (chipset < 0 || chipset > PM_CHIPSET_SK9822) {
        return &k_timings[PM_CHIPSET_WS2812B];
    }
    return &k_timings[chipset];
}

const char *pm_chipset_name(pm_chipset_t chipset)
{
    switch (chipset) {
    case PM_CHIPSET_WS2812: return "WS2812";
    case PM_CHIPSET_WS2812B: return "WS2812B";
    case PM_CHIPSET_WS2811: return "WS2811";
    case PM_CHIPSET_WS2813: return "WS2813";
    case PM_CHIPSET_SK6812_RGBW: return "SK6812_RGBW";
    case PM_CHIPSET_TM1814: return "TM1814";
    case PM_CHIPSET_APA102: return "APA102";
    case PM_CHIPSET_SK9822: return "SK9822";
    default: return "CUSTOM";
    }
}

pm_chipset_t pm_chipset_from_name(const char *name)
{
    if (!name) return PM_CHIPSET_WS2812B;
    if (ieq(name, "WS2812")) return PM_CHIPSET_WS2812;
    if (ieq(name, "WS2812B")) return PM_CHIPSET_WS2812B;
    if (ieq(name, "WS2811")) return PM_CHIPSET_WS2811;
    if (ieq(name, "WS2813")) return PM_CHIPSET_WS2813;
    if (ieq(name, "SK6812") || ieq(name, "SK6812_RGBW")) return PM_CHIPSET_SK6812_RGBW;
    if (ieq(name, "TM1814")) return PM_CHIPSET_TM1814;
    if (ieq(name, "APA102")) return PM_CHIPSET_APA102;
    if (ieq(name, "SK9822")) return PM_CHIPSET_SK9822;
    return PM_CHIPSET_WS2812B;
}
