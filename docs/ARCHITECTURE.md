# PixelMap Architecture

## Render path

```
map / Art-Net / sACN / UI
        │
        ▼
  effect engine  ──► per-pixel RGB (spatial eval)
        │
        ▼
  color engine   ──► correction, gamma, temp, RGBW/RGBWW mix
        │
        ▼
  led_driver     ──► pack color order ──► RMT NRZ (or clocked SPI path)
        │
        ▼
     GPIO data (/ clock)
```

## Control priority

1. Live Art-Net or sACN within timeout (default 1.5s)
2. Procedural spatial effect engine
3. Clear / standby

## Spatial effects

Effects do **not** walk strip index as a 1D rainbow by default. They sample `pm_mapped_pixel_t.pos` (`x,y,z`) so the same shader works on lines, grids, rings, and irregular fixtures.

## Bare-metal signaling

WS281x / SK6812 / TM1814 use the ESP32 **RMT** TX peripheral with nanosecond timing tables (`led_chipsets.c`). Bit patterns are DMA-friendly via `rmt_bytes_encoder`. APA102 / SK9822 are modeled as clocked buses (SPI host binding is the intended production path).

## Persistence

`config_store` writes WiFi, strip, effect, and universe settings to NVS. Pixel maps are edited live through `/api/map` (persist map blobs to NVS/SPIFFS can be added next).

## Web UI

Embedded single-page app (`components/web_ui/www/index.html`) served by `esp_http_server`:

| Route | Purpose |
|-------|---------|
| `GET /` | Editor UI |
| `GET/POST /api/config` | Device + strip + protocol settings |
| `GET/POST /api/map` | Spatial map JSON |
| `POST /api/map/grid` | Generate normalized grid |
