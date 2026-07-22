# Security tips (for installers)

PixelMap is meant to be easy to set up from a phone. That means the first Wi‑Fi hotspot uses a **known password**. After setup, you choose how locked-down the web page should be.

## First connection

| | |
|--|--|
| Network name | `PixelMap-XXXX` (letters/numbers at the end vary by device) |
| Hotspot password | `pixelmap1` |
| Address | http://192.168.4.1 |

Only use that hotspot while you’re setting the device up, or when you intentionally keep it on. On a busy venue Wi‑Fi, change the hotspot password afterward.

## Web page password

During setup (or later under **Network**) you can:

- **Set a password** (at least 12 characters) — recommended if other people share the network  
- **Leave the UI open** — fine for a private home or locked-down show network; anyone who can open the page can change settings and upload firmware  

You can turn the web password on or off later. Turning it off asks for a confirmation.

## Hotspot after setup

- **Off while on home/venue Wi‑Fi** — normal if you only use the LAN address or `pixelmap.local`
- **Stay on with home Wi‑Fi** — useful so a phone can always reach `192.168.4.1` without knowing the venue password
- **Come back if Wi‑Fi drops** — optional recovery hotspot

If you leave the hotspot on in public, change its password away from `pixelmap1`.

## Firmware updates

Use **Network → Firmware update** and upload the app file from a release (`pixelmap-esp32.bin` or `pixelmap-esp32s3.bin`), not the big “merged” install file. If a web password is set, you must be signed in first.

## Factory reset

Erases settings and maps, then shows the first-time setup wizard again (hotspot password back to `pixelmap1`). If a web password is set, you’ll need to enter it to confirm.

## Simple rules of thumb

1. Finish the setup wizard, then change default passwords if the device isn’t on a trusted network.  
2. Prefer a web password anywhere guests or other gear share Wi‑Fi.  
3. Don’t put the control page on the open internet.  
4. Treat the first-setup hotspot as temporary and local.

Commercial / locked-down hardware builds can add stronger options (encrypted storage, signed updates, HTTPS). Those are optional manufacturing choices, not required for normal DIY use.
