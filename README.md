# PixelMap

[![CI](https://github.com/NicholasTracy/pixelmap/actions/workflows/ci.yml/badge.svg)](https://github.com/NicholasTracy/pixelmap/actions/workflows/ci.yml)
[![Version](https://img.shields.io/github/v/release/NicholasTracy/pixelmap?include_prereleases&sort=semver&label=version)](https://github.com/NicholasTracy/pixelmap/releases/latest)
[![License: GPL v3](https://img.shields.io/badge/license-GPLv3-blue.svg)](LICENSE)

PixelMap turns an ESP32 LED controller into a **spatial** light engine — patterns follow where your LEDs sit in space, not only their order on the strip.

It installs like **WLED**, sets up from your phone over Wi‑Fi, and can also take cues from lighting software (**Art-Net** / **sACN**).

---

## Quick start

1. [Download the latest release](https://github.com/NicholasTracy/pixelmap/releases/latest)
2. Flash the **merged** file for your board (see [Install](#install))
3. Join Wi‑Fi **`PixelMap-XXXX`**, password **`pixelmap1`**
4. Open **http://192.168.4.1** and finish the setup wizard
5. Set LED type, pixel count, and a simple map — then pick an effect

---

## Who it’s for

- People already using WLED-style ESP32 controllers
- Walls, props, grids, rings, and 3D fixtures where layout matters
- Shows that need a simple web UI **and** Art-Net / sACN when ready

---

## Install

You need: an ESP32 or ESP32-S3 controller, a data-capable USB cable, and a computer.

### Download

| Board | File to use |
|-------|-------------|
| Most classic ESP32 / WLED “ESP32” boards | `pixelmap-esp32-merged.bin` |
| ESP32-S3 boards | `pixelmap-esp32s3-merged.bin` |

Not sure? Older controllers are usually **esp32**. Newer small USB-C boards are often **esp32s3**.

> Installing PixelMap replaces WLED. Your wiring stays the same; WLED settings do not transfer. You can flash WLED again anytime.

### Flash the board

1. Plug in USB. If needed: hold **BOOT**, tap **RESET**, release **BOOT**.
2. Flash the **merged** `.bin` at address **`0x0`** using one of:
   - [ESPHome Flasher](https://github.com/esphome/esphome-flasher/releases) (easiest)
   - [Espressif Flash Download Tools](https://www.espressif.com/en/support/download/other-tools) (Windows)
   - `esptool` (see below)
3. When it finishes, reset or unplug/replug the board.

```bash
# Classic ESP32 — replace COMx with your port
esptool.py --chip esp32 -p COMx write_flash 0x0 pixelmap-esp32-merged.bin

# ESP32-S3
esptool.py --chip esp32s3 -p COMx write_flash 0x0 pixelmap-esp32s3-merged.bin
```

If flashing fails, erase once (`esptool.py … erase_flash`) and try again with a different USB cable or port.

---

## First setup (phone or laptop)

No USB serial cable needed after flashing.

1. Join the Wi‑Fi network named like **`PixelMap-XXXX`**
2. Password: **`pixelmap1`**
3. Open **[http://192.168.4.1](http://192.168.4.1)**
4. In the wizard:
   - Connect to your home or venue Wi‑Fi (optional)
   - Choose whether to keep PixelMap’s own hotspot on as well (handy for phones)
   - Set a web password, or leave the UI open if you trust the network (you’ll see a warning)
5. Tap **Finish** — the full control page loads

Then on the main UI:

1. Confirm the data pin (often **GPIO 16**, same as many WLED boards)
2. Set LED type and pixel count
3. Build a simple map that matches your layout
4. Start with low brightness
5. Save

When the controller is on your normal Wi‑Fi, open its IP address or try **`http://pixelmap.local`** (hostname can be changed in Network settings).

---

## Everyday use

| Goal | Where |
|------|--------|
| Effects & colors | **Effects** tab |
| Layout / wire order | **Map** tab |
| LED type, pins, brightness | **Strip** tab |
| Wi‑Fi, hotspot, password, updates | **Network** tab |
| Mic-reactive patterns | **Audio** tab (optional microphone) |

### Lighting desks and software

Turn on **Art-Net** or **sACN** in the UI when you want an external console or media server to drive the lights. While that data is arriving, it takes priority over built-in patterns. When it stops, local effects resume.

### Updating firmware later

- **Easiest:** Network → Firmware update → upload the smaller **`pixelmap-esp32.bin`** / **`pixelmap-esp32s3.bin`** app file (not the merged file), then wait for reboot.
- **Or** reflash the full **merged** image the same way as the first install.

### Going back to WLED

Use [install.wled.me](https://install.wled.me) or your usual WLED flasher. PixelMap does not lock the board.

---

## Mapping & effects (short guide)

- **2D:** grid, circle, or custom points  
- **3D:** box, sphere, cylinder, dome, pyramid, or custom  
- Edit **wire order** so the map matches how data runs down the strip  
- Effects use each LED’s place in space, so the same pattern works on a flat wall or a volume  

**Persistence of vision (POV):** for spinning fans or waved wands. Enable POV, set RPM or sweep speed to match the real motion, and use an effect such as **POV Image Plane**.

**Supported LEDs (common):** WS2812 / WS2812B, WS2811, WS2813, SK6812 RGBW, TM1814, APA102 / SK9822 (needs data + clock pins).

Default wiring (typical WLED ESP32):

| Function | GPIO |
|----------|------|
| LED data | 16 |
| Status LED | 2 |
| LED clock (APA102) | 14 |

You can change pins in the UI. The status LED “breathes” differently for boot, Wi‑Fi, hotspot, normal run, DMX, and faults — slow calm breath usually means the setup hotspot is on.

---

## Wi‑Fi & passwords (plain language)

| Situation | What to know |
|-----------|----------------|
| First setup | Hotspot password is **`pixelmap1`**. Change it later if others might join that hotspot. |
| Web password | Optional. Recommended on shared or public networks. Leave empty only if you trust everyone on the network. |
| Keep hotspot + home Wi‑Fi | Available in the wizard and Network tab — useful so a phone can always reach **192.168.4.1**. |
| Safety notes | See [docs/SECURITY.md](docs/SECURITY.md) for short recommendations. |

---

## Troubleshooting

| Problem | Try this |
|---------|----------|
| Computer won’t see the board | Different USB cable/port, install serial drivers, BOOT + RESET again |
| Flash OK but no `PixelMap-…` Wi‑Fi | Confirm you used the **merged** file; power-cycle; wait ~10 seconds |
| Lights don’t move | Check data pin, LED type, pixel count, and shared ground with the power supply |
| Status LED sharp / urgent | Strip/output fault — check chipset and pin, then reboot |
| Status LED slow calm breath | Setup hotspot is on — join `PixelMap-XXXX` |
| Patterns look wrong | Rebuild or edit the map / wire order to match the physical layout |
| Want WLED again | Reflash WLED with your normal installer |

---

## More help

- [Releases](https://github.com/NicholasTracy/pixelmap/releases/latest)
- [Security tips (short)](docs/SECURITY.md)
- [Board pin notes](boards/README.md)

---

## For developers

Pull requests get automatic **area** and **size** labels plus a triage comment (changed areas, largest diffs, suggested checks). See `.github/workflows/pr-automation.yml`.

Preview the UI without hardware:

```bash
python tools/web_ui_dev_server.py
# → http://127.0.0.1:8080/
```

Build with ESP-IDF v5.1+:

```bash
idf.py set-target esp32   # or esp32s3
idf.py build
idf.py -p COMx flash monitor
```

Internals: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). Roadmap notes: [docs/PROJECT_EVALUATION_AND_ROADMAP.md](docs/PROJECT_EVALUATION_AND_ROADMAP.md).

---

## License

PixelMap is **[GNU GPL v3](LICENSE)** (or later).

That is a *copyleft* license: you can use, modify, and share the project, but if you distribute PixelMap or a modified version (including firmware binaries), you must also provide the corresponding source under GPL terms. Closed-source forks are not allowed.

Bundled third-party pieces keep their own licenses where noted (for example Lua is MIT, which is compatible with GPL).
