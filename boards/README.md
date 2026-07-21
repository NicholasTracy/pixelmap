# Board presets

JSON pin maps aligned with common WLED ESP32 layouts. These files are **reference documentation** for now — the firmware does not import them automatically, and the web UI does not yet have a board-preset picker.

| File | Description |
|------|-------------|
| `wled_esp32_default.json` | Data on GPIO 16, status LED on GPIO 2, clock GPIO 14 |

Enter matching GPIO / chipset values manually on the **Strip** and **Network** tabs. A future release may seed NVS or offer an import button.
