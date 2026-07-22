# PixelMap

[![CI](https://github.com/NicholasTracy/pixelmap/actions/workflows/ci.yml/badge.svg)](https://github.com/NicholasTracy/pixelmap/actions/workflows/ci.yml)
[![CodeQL](https://github.com/NicholasTracy/pixelmap/actions/workflows/codeql.yml/badge.svg)](https://github.com/NicholasTracy/pixelmap/actions/workflows/codeql.yml)
[![Version](https://img.shields.io/github/v/release/NicholasTracy/pixelmap?include_prereleases&sort=semver&label=version)](https://github.com/NicholasTracy/pixelmap/releases/latest)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

PixelMap is LED control software for ESP32 boards — the same kind of controllers people already use with **WLED**.

Instead of only treating lights as a long chain of pixels, PixelMap is built around **where each light sits in space**. That makes it easier to create patterns that move across a wall, wrap around a prop, or flow through a 3D fixture in a natural way.

You can set everything up in a browser over Wi‑Fi, or drive the lights from lighting software using **Art-Net** and **sACN**.

---

## Who this is for

- People with WLED-compatible ESP32 controllers who want stronger **pixel mapping**
- Installations where lights are arranged in grids, rings, strips, or custom shapes
- Users who want simple Wi‑Fi setup **and** professional lighting-network control

If you already flash WLED onto a board, flashing PixelMap feels familiar.

---

## What you can do

- Map pixels in 1D, 2D, or 3D and edit the layout in the browser
- Run patterns based on spatial position (not just “pixel number along the strip”)
- **Persistence of vision (POV)** for spinning fans or waved wands — set fixed RPM / speed so the math paints a stable plane in mid-air
- Use common LED types such as WS2812B, WS2811, SK6812 (RGBW), and more
- Mix RGB, RGBW, and dual-white style chips with color correction similar to FastLED
- Control from a phone/computer UI, or from Art-Net / sACN lighting software
- Keep using typical WLED-style wiring (default data pin **GPIO 16**)

---

## Install on a WLED controller (recommended)

You do **not** need to be a developer for this path. You only need:

1. An **ESP32** (or **ESP32-S3**) controller that already works with WLED
2. A USB cable that can carry data (not charge-only)
3. A computer (Windows, macOS, or Linux)
4. The PixelMap firmware file from the latest release

### 1) Download the right firmware

Open the latest release:

**[Download PixelMap releases](https://github.com/NicholasTracy/pixelmap/releases/latest)**

For the easiest install, grab the **merged** image:

| Your board | Best download for beginners |
|------------|-----------------------------|
| Classic ESP32 (most WLED “ESP32” controllers) | `pixelmap-esp32-merged.bin` |
| ESP32-S3 boards | `pixelmap-esp32s3-merged.bin` |

If you are unsure, most older / common WLED ESP32 boards use the **esp32** build. Newer tiny USB-C boards are often **esp32s3**.

> **Important:** Installing PixelMap replaces WLED on the controller. Your lights and wiring stay the same, but WLED settings are not carried over. You can reinstall WLED later the same way you originally installed it.

### 2) Put the board in flash mode

Most ESP32 WLED controllers flash over USB like this:

1. Plug the controller into your computer over USB
2. Hold the **BOOT** (or **FLASH**) button
3. Press and release **RESET** (or **EN**) once
4. Keep holding **BOOT** for another second, then release it

Some boards enter flash mode automatically when you start the flasher. If the tool cannot connect, repeat the button sequence above.

### 3) Flash with a simple tool

#### Option A — ESP Home Flasher (easiest desktop app)

1. Install [ESPHome Flasher](https://github.com/esphome/esphome-flasher/releases)
2. Select the serial port for your controller
3. Choose the **merged** PixelMap file (`pixelmap-esp32-merged.bin` or the S3 version)
4. Click **Flash ESP**
5. Wait until it finishes successfully, then press **RESET** or unplug/replug the board

> Tip: If flashing fails halfway, erase the flash once in your tool (or with `esptool erase_flash`) and try the merged file again.

#### Option B — Espressif Flash Download Tool (Windows)

1. Download Espressif’s [Flash Download Tools](https://www.espressif.com/en/support/download/other-tools)
2. Choose the ESP32 (or ESP32-S3) mode
3. Load `pixelmap-esp32-merged.bin` (or the S3 merged file)
4. Set the flash address to **`0x0`**
5. Start the flash and wait for success

#### Option C — Command line (`esptool`)

If you already use `esptool.py` (common for WLED power users), flash the merged image at the start of flash memory:

```bash
# Classic ESP32
esptool.py --chip esp32 -p COMx write_flash 0x0 pixelmap-esp32-merged.bin

# ESP32-S3
esptool.py --chip esp32s3 -p COMx write_flash 0x0 pixelmap-esp32s3-merged.bin
```

Replace `COMx` with your port (`COM5`, `/dev/ttyUSB0`, `/dev/cu.usbserial-...`, etc.).

If the board is stubborn, erase once, then flash again:

```bash
esptool.py --chip esp32 -p COMx erase_flash
esptool.py --chip esp32 -p COMx write_flash 0x0 pixelmap-esp32-merged.bin
```

<details>
<summary>Advanced: flash bootloader, partitions, and app separately</summary>

Only needed if you are not using the merged image.

| File | Address |
|------|---------|
| `bootloader-esp32.bin` | `0x1000` |
| `partition-table-esp32.bin` | `0x8000` |
| `pixelmap-esp32.bin` | `0x20000` (OTA slot `ota_0`) |

```bash
esptool.py --chip esp32 -p COMx write_flash \
  0x1000 bootloader-esp32.bin \
  0x8000 partition-table-esp32.bin \
  0x20000 pixelmap-esp32.bin
```

Use the matching `esp32s3` filenames for S3 boards. On some S3 modules the bootloader offset is `0x0` instead of `0x1000` — prefer the merged image when unsure.

</details>

### 4) Connect to PixelMap Wi‑Fi (setup wizard)

After a successful flash — **no USB serial required**:

1. On your phone or computer, join **`PixelMap-XXXX`**
2. Password: **`pixelmap1`**
3. Open **[http://192.168.4.1](http://192.168.4.1)** — the first-boot setup wizard opens
4. Pick your venue Wi‑Fi (optional), SoftAP options (APSTA is on by default), and either set a web UI password or leave the UI open (with a warning)
5. Finish setup — the full PixelMap UI loads

After setup you can:

- Refine LED chip type, pixel count, and maps
- Pick spatial effects
- Turn on Art-Net / sACN

If SoftAP is not kept on (APSTA / fallback), use the LAN IP or `hostname.local` once the controller joins your Wi‑Fi.

---

## First-time setup checklist

1. Confirm your LED data wire is on the controller’s usual WLED data pin (often **GPIO 16**)
2. Set the correct LED type (for example WS2812B)
3. Set the correct number of pixels
4. Build a simple grid or line map that matches your physical layout
5. Lower brightness while testing
6. Save settings, then refresh / reconnect if you changed Wi‑Fi

---

## Mapping layouts

On the **Map** tab (when POV is off):

| Control | Purpose |
| --- | --- |
| Dimension | 2D or 3D |
| Layout (2D) | **Grid**, **Circle**, or **Custom formula** |
| Layout (3D) | **Box**, **Sphere**, **Cylinder**, **Dome**, **Pyramid**, or **Custom formula** |
| Fill | Surface/rings vs solid lattice (shape-dependent) |
| Pixel spacing | Neighbor pitch — preview uses a fixed world window so spacing visibly grows the layout |
| Width / Height / Depth | Lattice size (meaning depends on layout) |
| Edit wire order | Click LEDs in data-signal sequence so each map point gets the correct strip address |
| Auto route | Axis order (all L→R / T→B / F→B permutations) plus **Start of each run** or **Zig-zag** |
| Strip to route | When **Strip → Connected strips** is 2+, pick which strip’s wire order you are editing |

Built maps (including wire order) are saved on the device and restored after reboot.

On **Strip**, set **Connected strips** first. Each strip has its own **data GPIO** (WLED-style multi-bus defaults: 16, 2, 4, 13, …) and pixel count. Mapped LED count is capped by the sum of those lengths; oversized grids show a warning and are clipped.

---

## Built-in effects

Spatial patterns sample each pixel’s mapped **X/Y/Z** (or POV world position), so the same effect works on a flat 2D grid and a 3D volume.

| Group | Effects |
| --- | --- |
| Classic | Solid, Rainbow Spatial, Plasma, Noise Field, Radial Wave, Plane Sweep, POV Image Plane |
| Geometric | Checkerboard, Stripes, Cartesian Grid, Box Rings, Sphere Rings, Crosshair, Diamond Lattice, Spiral, Starburst, Chevron, Helix, Axis Planes, Scan Volume, Interference, Triangle Lattice, Cubic Pulse, Polar Grid, Wave Walls |

Preview any effect live on the **Effects** tab (flat plane in 2D mode, volumetric cube in 3D).

---

## Persistence of vision (POV)

Use this when a strip is **moving fast enough** that your eye blends the light into a 2D image — for example:

- A LED strip across a **rotating fan / propeller**
- A LED **wand** waved side to side in a plane

### How it works

PixelMap computes each pixel’s position in the swept plane from time and your motion settings:

- **Rotation:** `angle = 2π × (RPM / 60) × time`, then `x,y` from strip layout × radius  
- **Linear wand:** strip height along the blade, position along the path from linear speed (triangular back-and-forth)

Patterns (especially **POV Image Plane**) are sampled in that world plane, so with the correct RPM/speed the image appears stable in space.

### Suggested starting values

| Setting | Typical start |
|---------|----------------|
| Mode | Rotation (fan) or Linear (wand) |
| Blade / strip count | 1–10 evenly spaced blades (rotation) |
| Layout | Diameter (strip across hub) or Radius (hub → tip) |
| RPM | Match your fan/wand spin (e.g. 600) |
| Radius / half-span | Physical tip distance in meters (e.g. 0.25) |
| Linear speed | Wand sweep speed in m/s (e.g. 4) |
| Effect | POV Image Plane (or any spatial effect with POV enabled) |

Total controller pixels are split evenly across blades. With 3 blades and 60 LEDs, each blade gets 20 pixels at 120° spacing.

Enable **POV mapping** in the web UI, set RPM/speed to match the real mechanism, save, and spin/wave the strip.

> RPM and linear speed are **fixed configuration values for now** (not auto-measured). Matching them to the real motion is what keeps the math correct.

## Using PixelMap day to day

### Browser control

Open the controller’s IP in a browser to change effects, brightness, mapping, and network options.

### Lighting software (Art-Net / sACN)

PixelMap can listen for standard lighting-network data:

- **Art-Net** — commonly used by lighting desks and media servers
- **sACN (E1.31)** — common in venue / installation workflows

While Art-Net or sACN data is arriving, that live control takes priority over the built-in patterns. When the stream stops, PixelMap returns to its local effects.

### Control priority

1. Art-Net / sACN (while actively receiving)
2. Built-in spatial effects from the web UI
3. Standby / solid behavior

---

## Default wiring (WLED-style)

PixelMap ships with pin defaults that match many WLED ESP32 layouts:

| Function | ESP32 GPIO | Notes |
|----------|------------|-------|
| LED data (main) | 16 | Primary data line on many WLED boards |
| Status LED | 2 | Onboard LED on most ESP32 DevKit-style boards |
| LED clock | 14 | SPI clock for APA102 / SK9822 |

You can change the data pin in the web UI if your board uses a different layout.

### Status LED meanings

The onboard status LED (default **GPIO 2**) uses **PWM breathing / ramps** so modes are easy to tell apart (not just hard on/off blinks):

| Animation | Meaning |
|-----------|---------|
| Soft fade-in, then steady glow | Booting |
| Fast breath (~0.4s cycle) | Connecting to Wi‑Fi |
| Slow calm breath (~2.2s cycle) | Setup mode / PixelMap Wi‑Fi hotspot is on |
| Gentle double-heartbeat every ~2.8s | Running normally (local effects) |
| Medium sawtooth ramp (~0.65s) | Receiving Art-Net or sACN |
| Sharp urgent peaks | LED strip/output fault |
| Soft-edged SOS (`··· ——— ···`) | Serious configuration / startup fault |

If you assign GPIO 2 as LED data instead, the status LED is disabled automatically so it does not fight the pixel bus.

---

## Supported LED types (overview)

- WS2812 / WS2812B
- WS2811
- WS2813
- SK6812 RGBW
- TM1814

APA102 / SK9822 use SPI (data + clock GPIO; one strip on SPI2). Color handling includes RGB, RGBW, HSV, gamma, and color correction. Web OTA, SoftAP / APSTA, mDNS (`hostname.local`), effect presets, required web UI login, and factory reset are available in the Network tab.

**Audio reactive:** optional I2S MEMS mic (INMP441-style). Enable under the Audio tab (WS / BCLK / DOUT GPIOs, gain, squelch). Use effects **Audio Pulse**, **Audio Ripple**, **Audio Spectrum**, or turn on “modulate intensity” for any effect. Bluetooth is not included.

---

## Wi‑Fi, SoftAP, OTA, and security

First boot / factory reset opens a **SoftAP setup wizard** (no serial):

| | |
|--|--|
| SSID | `PixelMap-XXXX` |
| SoftAP password | `pixelmap1` during setup (change afterward for shared venues) |
| Wizard | `http://192.168.4.1/` — Wi‑Fi, APSTA, web password or open UI |
| After setup | Full UI; web auth optional; SoftAP/APSTA as you chose |

In the **Network** tab you can scan for Wi‑Fi, toggle APSTA / AP fallback, change SoftAP password (min 12), and enable/disable web UI login (blank password + confirm → open UI).

**Web OTA:** Network → Firmware update with the **app** `.bin` (not the merged image). Requires login when web auth is on. See [docs/SECURITY.md](docs/SECURITY.md).

---

## Updating PixelMap later

**Preferred — web OTA:** open the UI (LAN or SoftAP), Network → Firmware update, upload the newer **app** `.bin`, wait for reboot.

**Full reflash:** when a new version is published on the Releases page:

1. Download the newer **merged** `.bin` for your board
2. Flash it the same way you did the first time (address `0x0`)
3. Reconnect to Wi‑Fi / the web UI and confirm your settings

Settings are stored on the device, but it is still smart to note your pixel count, pin, and map before major upgrades.

---

## Going back to WLED

PixelMap does not lock the board. To return to WLED:

1. Use your usual WLED installer ([install.wled.me](https://install.wled.me) or your preferred flasher)
2. Flash the WLED build for your board
3. Set up WLED again as you normally would

---

## Troubleshooting

| Problem | What to try |
|---------|-------------|
| Flasher cannot find the board | Try another USB cable/port, install/update the USB serial driver, and repeat the BOOT + RESET sequence |
| Flash succeeds but no Wi‑Fi network appears | Confirm you used the **merged** image, power-cycle the board, and wait ~10 seconds |
| Lights do not respond | Check data pin (often GPIO 16), LED type, pixel count, and that ground is shared between power supply and controller |
| Onboard LED shows sharp urgent peaks | LED output fault — check chipset/pin settings, then reboot |
| Onboard LED breathes slowly | Controller is in setup hotspot mode (`PixelMap-XXXX`) |
| Patterns look wrong spatially | Rebuild or drag-edit the pixel map so it matches the physical layout |
| Want WLED back | Reflash WLED with your normal installer |

---

## For developers

### Preview the web UI on your PC (no flash required)

The on-device UI lives in `components/web_ui/index.html` and talks to `/api/...` endpoints. Opening the HTML file alone is not enough — use the mock server:

```bash
python tools/web_ui_dev_server.py
```

Then open **http://127.0.0.1:8080/** in your browser.

That serves the real UI and fakes config/map APIs in memory so you can tweak settings, POV fields, and the map editor before flashing. Saves do **not** reach a controller.

### Build firmware (ESP-IDF)

If you want to build from source with ESP-IDF v5.1+:

```bash
idf.py set-target esp32   # or esp32s3
idf.py build
idf.py -p COMx flash monitor
```

Project layout and internals are described in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

Continuous integration builds firmware for **ESP32** and **ESP32-S3** on every change, and CodeQL scans the codebase. Current version: see [`VERSION`](VERSION) and [Releases](https://github.com/NicholasTracy/pixelmap/releases).

---

## Helpful links

- [Latest release downloads](https://github.com/NicholasTracy/pixelmap/releases/latest)
- [CI status](https://github.com/NicholasTracy/pixelmap/actions/workflows/ci.yml)
- [Architecture notes](docs/ARCHITECTURE.md)
- [Board pin presets](boards/)

## License

MIT — see [LICENSE](LICENSE).
