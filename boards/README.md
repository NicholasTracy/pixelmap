# Board pin notes

These JSON files describe common WLED-style ESP32 pin layouts. They’re **reference only** — PixelMap does not load them automatically yet. Enter the same pins in the **Strip** tab of the web UI.

| File | Typical use |
|------|-------------|
| `wled_esp32_default.json` | Data on GPIO **16**, status LED on GPIO **2**, clock on GPIO **14** |

If your board uses different pins, set them in the UI to match your wiring.
