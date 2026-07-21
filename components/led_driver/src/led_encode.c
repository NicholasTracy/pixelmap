#include "led_strip.h"
#include <string.h>

size_t pm_led_encode_frame(const uint8_t *pixels, uint16_t count, uint8_t channels,
                           pm_color_order_t order, const pm_color_correction_t *cc,
                           uint8_t *out, size_t out_cap)
{
    if (!pixels || !out || !cc || channels == 0 || count == 0) return 0;
    size_t out_i = 0;
    for (uint16_t i = 0; i < count; ++i) {
        const uint8_t *src = &pixels[(size_t)i * channels];
        pm_rgb_t rgb = {src[0], src[1], src[2]};
        rgb = pm_apply_correction(rgb, cc);

        uint8_t ch[5] = {rgb.r, rgb.g, rgb.b, 0, 0};
        if (channels >= 5) {
            pm_rgbww_t ww = pm_rgb_to_rgbww(rgb, cc->auto_white, cc->warm_mix);
            ch[0] = ww.r; ch[1] = ww.g; ch[2] = ww.b; ch[3] = ww.w1; ch[4] = ww.w2;
        } else if (channels >= 4) {
            pm_rgbw_t w = pm_rgb_to_rgbw(rgb, cc->auto_white);
            ch[0] = w.r; ch[1] = w.g; ch[2] = w.b; ch[3] = w.w;
        }

        uint8_t packed[5];
        int plen = 0;
        pm_pack_pixel(ch, channels, order, packed, &plen);
        if (out_i + (size_t)plen > out_cap) return 0;
        memcpy(&out[out_i], packed, (size_t)plen);
        out_i += (size_t)plen;
    }
    return out_i;
}
