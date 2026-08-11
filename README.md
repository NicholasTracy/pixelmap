<p align="center">
  <img src="docs/PixelMapLogo.png" alt="PixelMap" width="200" />
</p>

<h1 align="center">PixelMap</h1>

<p align="center">
  <a href="https://github.com/NicholasTracy/pixelmap/actions/workflows/ci.yml"><img src="https://github.com/NicholasTracy/pixelmap/actions/workflows/ci.yml/badge.svg" alt="CI" /></a>
  <a href="https://github.com/NicholasTracy/pixelmap/releases/latest"><img src="https://img.shields.io/github/v/release/NicholasTracy/pixelmap?include_prereleases&sort=semver&label=version" alt="Version" /></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPLv3-blue.svg" alt="License: GPL v3" /></a>
</p>

PixelMap turns an ESP32 LED controller into a **spatial** light engine — patterns follow where your LEDs sit in space, not only their order on the strip.

It installs like **WLED**, sets up from your phone over Wi‑Fi, and can also take cues from lighting software (**Art-Net** / **sACN**).

---

## Quick start

1. [Download the latest release](https://github.com/NicholasTracy/pixelmap/releases/latest)
2. Install **without USB** from WLED (OTA app `.bin`) **or** flash the **merged** file over USB (see [Install](#install))
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

Two paths: **USB/serial** (full factory image) or **OTA from WLED** (no USB/FTDI — works on sealed controllers).

### Download

| Board | First flash (USB) | OTA from WLED / updates |
|-------|-------------------|-------------------------|
| Most classic ESP32 / WLED “ESP32” boards | `pixelmap-esp32-merged.bin` | `pixelmap-esp32.bin` |
| ESP32-S3 boards | `pixelmap-esp32s3-merged.bin` | `pixelmap-esp32s3.bin` |

Not sure? Older controllers are usually **esp32**. Newer small USB-C boards are often **esp32s3**.

> Installing PixelMap replaces WLED. Your wiring stays the same; WLED settings do not transfer. You can return to WLED later over the network (see [Going back to WLED](#going-back-to-wled)).

### Option A — Install from WLED (no USB)

Use this when the board already runs WLED and has no serial port, or you prefer not to open the case.

1. Open the [latest PixelMap release](https://github.com/NicholasTracy/pixelmap/releases/latest) and download the **app** file for your chip (`pixelmap-esp32.bin` or `pixelmap-esp32s3.bin` — **not** the `-merged` file).
2. On the controller’s WLED UI: **Config → Security & Updates** (or **Settings → Security**), open **Manual OTA Update**.
3. Choose the PixelMap `.bin` and start the update. Wait for the board to reboot (often 30–90 seconds).
4. Join Wi‑Fi **`PixelMap-XXXX`**, password **`pixelmap1`**, open **http://192.168.4.1**, and finish setup as usual.

Requirements:

- Board must already have **dual OTA** partitions large enough for the app (recent WLED 4 MB builds usually do; PixelMap is under ~1 MB).
- Use the binary that matches the **chip** (ESP32 vs ESP32-S3).
- OTA only swaps firmware; it does **not** rewrite the partition table. PixelMap still boots and reuses the board’s existing filesystem area for maps.

If update fails (“too large” / won’t boot SoftAP), use [Option B](#option-b--usbserial-first-flash) once so the board gets PixelMap’s partition layout.

### Option B — USB/serial first flash

You need: an ESP32 or ESP32-S3 controller, a data-capable USB cable (or FTDI on TTL boards), and a computer.

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

No USB serial cable needed after flashing (or after the WLED OTA install).

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

- **Easiest:** Network → Software update → upload the smaller **`pixelmap-esp32.bin`** / **`pixelmap-esp32s3.bin`** app file (not the merged file), then wait for reboot.
- **Or** reflash the full **merged** image the same way as the first install.

### Going back to WLED

**Over Wi‑Fi (no USB):**

1. Download a WLED release **app** image for your exact board/chip (from [install.wled.me](https://install.wled.me) or [WLED releases](https://github.com/wled/WLED/releases)) — same rules as PixelMap: **not** a full multi-offset flash pack unless the site documents it as OTA-safe.
2. PixelMap → **Network → Software update** → upload that WLED `.bin` → wait for reboot.
3. Rejoin WLED’s Wi‑Fi / AP and reconfigure (settings do **not** come back automatically).

**Full factory restore (USB):** use [install.wled.me](https://install.wled.me) or your usual WLED serial flasher if OTA is unavailable or the partition layout is nonstandard.

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
| Flash OK but no `PixelMap-…` Wi‑Fi | Confirm you used the **merged** file (USB) or the **app** file (WLED OTA); power-cycle; wait ~10 seconds |
| WLED OTA rejects PixelMap | File too large or wrong chip — try USB merged install once, or a newer WLED build with larger app slots |
| Lights don’t move | Check data pin, LED type, pixel count, and shared ground with the power supply |
| Status LED sharp / urgent | Strip/output fault — check chipset and pin, then reboot |
| Status LED slow calm breath | Setup hotspot is on — join `PixelMap-XXXX` |
| Patterns look wrong | Rebuild or edit the map / wire order to match the physical layout |
| Want WLED again | Network → Software update with a WLED app `.bin`, or serial via install.wled.me |

---

## More help

- [User guide (Wiki)](https://github.com/NicholasTracy/pixelmap/wiki) — install, everyday use, common setups, custom effects & layouts
- [Releases](https://github.com/NicholasTracy/pixelmap/releases/latest)
- [Report a bug, request a feature, or ask a question](https://github.com/NicholasTracy/pixelmap/issues/new/choose)
- [Security tips (short)](docs/SECURITY.md)
- [Board pin profiles](boards/README.md) — add a JSON file, run `python tools/gen_boards_catalog.py`, then pick the board in **LED lights**

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

## A note about AI usage in this project

PixelMap has made use of AI assistance tools such as Cursor Agents to assist with debugging, repository automation and maintenance, and setup of CI and other automation. That being said, the vast majority of the code base has been developed by a human with the assistance of code completion tools and questions asked on google, reddit, Stack Exchange, ChatGPT and more. AI is a tool, not a crutch, this project was developed with passion and love and would not be possible without the help of AI to make it fully functional and available on Github for free... This project is GPL v3 Open Source and I take not money from developing or releasing this. 

---

## License

PixelMap is **[GNU GPL v3](LICENSE)** (or later).

That is a *copyleft* license: you can use, modify, and share the project, but if you distribute PixelMap or a modified version (including firmware binaries), you must also provide the corresponding source under GPL terms. Closed-source forks are not allowed.

Bundled third-party pieces keep their own licenses where noted (for example Lua is MIT, which is compatible with GPL).
