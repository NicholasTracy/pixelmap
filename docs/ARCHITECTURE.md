# PixelMap Architecture

For project health, defects, missing features, and the phased roadmap, see
[PROJECT_EVALUATION_AND_ROADMAP.md](./PROJECT_EVALUATION_AND_ROADMAP.md).

## Render path

```
map / Art-Net / sACN / UI
        │
        ▼
  effect engine  ──► per-pixel RGB (spatial eval)
        │
        ▼
  color engine   ──► correction, gamma, temp, RGBW mix
        │
        ▼
  led_driver     ──► pm_led_encode_frame (pack) ──► RMT NRZ
        │
        ▼
     GPIO data
```

## Control priority

1. Live Art-Net or sACN within timeout (default 1.5s)
2. Procedural spatial effect engine
3. Idle / last frame (no separate standby mode yet)

## Spatial effects

Effects do **not** walk strip index as a 1D rainbow by default. They sample `pm_mapped_pixel_t.pos` (`x,y,z`) so the same shader works on lines, grids, rings, and irregular fixtures.

## Persistence of vision (POV)

When POV is enabled, `effects` asks `pov` for each pixel’s **instantaneous world position** from time + RPM (rotation) or linear speed (wand). Spatial shaders then sample that plane. `PM_EFFECT_POV_IMAGE_PLANE` keeps the pattern fixed in world space so motion alone “paints” the image.

## Bare-metal signaling

WS281x / SK6812 / TM1814 use the ESP32 **RMT** TX peripheral with nanosecond timing tables (`led_chipsets.c`). Bit patterns are DMA-friendly via `rmt_bytes_encoder`. APA102 / SK9822 use **SPI2** (MOSI = data, SCLK = clock); multi-strip SPI is not supported yet.

## Audio reactive

Optional I2S MEMS mic (`components/audio`, INMP441-style: WS / BCLK / DOUT). A lightweight 256-point FFT produces volume, bass/mid/treble, 16 spectrum bins, and a beat pulse. Levels feed `pm_effect_context_t.audio` for Audio Pulse / Ripple / Spectrum effects, and can optionally scale any effect’s intensity. Bluetooth is not implemented.

## Status LED

`status_led` drives the WLED-style onboard LED (default **GPIO 2**) with **LEDC PWM** breathing/ramps so modes are visually distinct: boot fade-in, fast Wi‑Fi breath, slow AP breath, soft heartbeat, DMX sawtooth, fault peaks, soft SOS. Automatically disabled if the pin conflicts with LED data/clock.

## Persistence

`config_store` writes WiFi, strip, effect, and universe settings to NVS. Pixel map point clouds (including wire order) persist to the `storage` data partition (raw JSON blob via `map_store`) whenever `/api/map` or `/api/map/grid` changes the map. On boot, the blob is loaded if present; otherwise the map is regenerated from layout parameters in NVS.

## Wi‑Fi (`wifi_mgr`)

- **STA** — join a home/venue network (SSID/password from NVS).
- **SoftAP** — `PixelMap-XXXX` (or custom SSID), WPA2. Setup wizard uses `pixelmap1`; never open auth. UI at `http://192.168.4.1/`.
- **APSTA** — SoftAP with STA when `apen` (wizard default on). **AP fallback** (`apfb`, default off) when STA drops. SoftAP drops when STA healthy unless `apen`.
- **Scan** — blocking `pm_wifi_scan` via `GET /api/wifi/scan` (forces APSTA briefly if AP-only).
- **mDNS** — `hostname.local` HTTP service.

## Security (`security` + `config_store`)

See [SECURITY.md](SECURITY.md). `setup_complete` gates a SoftAP wizard (`pixelmap1`). Optional web auth stores salted password hashes. Open UI allowed when auth is cleared.

## Web UI

Embedded SPA + first-boot **setup wizard** HTML in `web_ui.c`.

While `!setup_complete`, only `/`, vendor assets, and `/api/setup/*` are usable. After setup, optional login (`HttpOnly` session / `X-PixelMap-Auth` token). Factory reset requires `confirmPass` when auth is on.

| Route | Purpose |
|-------|---------|
| `GET /` | Setup wizard, login page, or editor UI |
| `GET /vendor/bootstrap.min.css` | Bootstrap CSS (embedded) |
| `GET /vendor/bootstrap.bundle.min.js` | Bootstrap JS (embedded) |
| `GET/POST /api/auth` | Auth status / login (`token` + Set-Cookie) |
| `POST /api/auth/logout` | Clear session |
| `GET /api/setup/status` | Wizard status (unauthenticated during setup) |
| `GET /api/setup/scan` | Wi‑Fi scan during setup |
| `POST /api/setup/complete` | Finish wizard (STA/APSTA/auth) |
| `GET /api/wifi/status` | STA/AP mode, IPs, SSIDs |
| `GET /api/wifi/scan` | Nearby SSIDs (RSSI) |
| `GET/POST /api/config` | Device + strip + Wi‑Fi/AP/auth settings (`pass` never returned on GET) |
| `GET/POST /api/map` | Spatial map JSON (persisted via `map_store`) |
| `POST /api/map/grid` | Generate normalized lattice / shape |
| `GET/POST /api/fx/lua` | Custom Lua effect script |
| `GET/POST /api/ota` | Firmware OTA (raw app `.bin` body on POST; works over SoftAP or STA) |
| `GET/POST /api/presets` | Effect preset slots |
| `POST /api/factory_reset` | Erase NVS + map storage, reboot |
| `GET /api/audio` | Live mic levels (volume / bands / spectrum / beat) |

## Host virtbench (CI)

`tools/virtbench` builds a Linux host binary (no ESP-IDF) that links `color`, `mapping`, `pov`, `effects`, and `pm_led_encode_frame`. CI runs it to assert solid packing, color order, spatial variation, POV motion, and map builders.

```bash
cmake -S tools/virtbench -B build-virtbench
cmake --build build-virtbench
./build-virtbench/virtbench
```

Firmware releases: push a `v*` tag (or run the Release workflow) to publish `pixelmap-esp32-merged.bin` and `pixelmap-esp32s3-merged.bin`.
