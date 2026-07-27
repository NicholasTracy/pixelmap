# Board pin profiles

Drop a JSON file in this folder to add a board. PixelMap loads them into the
**LED lights** tab (Board dropdown) and serves them from `GET /api/boards`.

After adding or editing a board file, regenerate the catalog:

```bash
python tools/gen_boards_catalog.py
```

Firmware builds also regenerate `catalog.json` at CMake configure time.

## Schema

| Field | Required | Notes |
|-------|----------|--------|
| `id` | yes | Stable key (filename stem is fine) |
| `name` | yes | Shown in the UI |
| `mcu` | yes | `esp32` or `esp32s3` — used for auto-select |
| `default` | no | Prefer this profile when MCU matches |
| `pins.led_data` | yes | Array of data GPIOs (up to 8 strips) |
| `pins.led_clock` | no | APA102/SK9822 clock (−1 unused) |
| `pins.status_led` | no | Status LED GPIO (−1 disables) |
| `pins.status_led_active_high` | no | Default true |
| `pins.button` | no | Documented only — no handler yet |
| `pins.audio_ws` / `audio_sck` / `audio_sd` | no | I2S mic pins |
| `notes` | no | Short help for the UI |
| `defaults` | no | Optional chipset / color_order / pixel_count / max_ma |

## Files

| File | Typical use |
|------|-------------|
| `wled_esp32_default.json` | Classic ESP32 DevKit / WLED pins |
| `wled_esp32_eth.json` | ESP32 with Ethernet (LED data on GPIO 4) |
| `wled_esp32s3_default.json` | ESP32-S3 DevKit-style pins |
| `catalog.json` | Generated — do not edit by hand |
