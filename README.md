# PixelMap

End-to-end ESP32 pixel LED driver focused on **spatial pixel mapping** and **procedurally generated patterns**. Compatible with common WLED-style board pinouts, with a WiFi settings/editor UI plus **Art-Net** and **sACN (E1.31)** control.

## Highlights

- **Bare-metal timing** via the ESP32 RMT peripheral for accurate, high-speed chipset signaling
- **Chipsets**: WS2812 / WS2812B, WS2811, WS2813, SK6812 (RGBW), APA102 / SK9822 (4-pin clocked), TM1814, and extensible timing tables
- **Color models**: RGB, RGBW, RGBWW / RGBCW, HSV mixing, white-channel extraction, and FastLED-style color correction / temperature / gamma
- **Spatial engine**: 1D / 2D / 3D pixel maps; effects evaluate color from pixel world coordinates
- **Control**: WiFi AP+STA, browser UI for settings + map editing, Art-Net, sACN
- **Boards**: default pin maps aligned with popular WLED ESP32 layouts

## Architecture

```
components/
  led_driver/     RMT strip driver + chipset timing
  color/          CRGB / CRGBW / CHSV, correction, blending
  mapping/        spatial pixel maps & fixtures
  effects/        procedural spatial shaders
  wifi_mgr/       STA/AP lifecycle
  web_ui/         HTTP + WebSocket settings/editor UI
  artnet/         Art-Net receiver
  sacn/           sACN / E1.31 receiver
  config_store/   NVS persistence
main/             app glue, render loop, output mux
```

## CI

GitHub Actions runs on every push/PR:

- **Lint & structure** — required files, component layout, secret hygiene
- **Firmware build** — ESP-IDF `v5.2.3` for `esp32` and `esp32s3` (artifacts uploaded)
- **CodeQL** — C/C++ security scanning

## Build (ESP-IDF)

Requires [ESP-IDF v5.1+](https://docs.espressif.com/projects/esp-idf/) (ESP32 / ESP32-S3 recommended).

```bash
idf.py set-target esp32
idf.py menuconfig   # optional: PixelMap options
idf.py build
idf.py -p COMx flash monitor
```

First boot opens AP `PixelMap-XXXX` (password `pixelmap1`). Open `http://192.168.4.1` to configure WiFi, strips, maps, and effects.

## Default pinouts (WLED-style)

| Function        | ESP32 GPIO | Notes                          |
|-----------------|------------|--------------------------------|
| LED data (out1) | 16         | Primary data line              |
| LED data (out2) | 2          | Optional second strip          |
| LED clock       | 14         | APA102 / SK9822 clock          |
| Button          | 0          | Boot / UI factory reset hold   |
| Status LED      | 2          | Shared carefully when unused   |

Board presets live under `boards/`. Override in the web UI or NVS.

## Control priorities

1. Art-Net / sACN (when receiving within timeout)
2. Live UI preview / effect engine
3. Solid / standby color

## License

MIT
