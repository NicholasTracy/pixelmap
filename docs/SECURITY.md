# PixelMap security model

## First-boot SoftAP wizard (no USB serial)

On first flash or after factory reset (`setup_complete = false`):

1. Join Wi‑Fi **`PixelMap-XXXX`** with password **`pixelmap1`**
2. Open **http://192.168.4.1/** — a walled setup wizard is served (main UI/API blocked)
3. Choose venue Wi‑Fi (optional), SoftAP options (**APSTA** default on), and web UI password
4. Leave web password blank + acknowledge → **open UI** (warned)
5. Finish → normal UI; SoftAP password remains `pixelmap1` unless you changed it

## Defaults after setup

| Control | Behavior |
|---------|----------|
| Web UI auth | Optional — set a password (≥12) or leave open with acknowledgement |
| SoftAP password | Setup default `pixelmap1` until changed (min 12 when changing) |
| SoftAP + STA | APSTA selectable in wizard (default on); SoftAP drops when STA healthy if APSTA off |
| AP fallback | Off by default; opt-in in wizard / Network tab |
| Sessions | When auth on: 128-bit token, `HttpOnly` + `SameSite=Lax`, 8 h idle TTL |
| Login | Rate-limited (5 failures → 60 s lockout) |
| Factory reset | Session + re-entered password (if auth on); returns to SoftAP wizard |

## Disabling web auth later

Network → uncheck **Require password**, leave password blank, confirm the warning. Clears the stored hash.

## Threat model

- **Setup SoftAP** uses a **known** password (`pixelmap1`) so phones can onboard without serial. Treat first-boot SoftAP as physically trusted / short-lived.
- After setup, change SoftAP and enable web auth on any shared or hostile network.
- UI remains **HTTP** (not HTTPS). Prefer isolating controllers on a show VLAN for venues.
- Stock builds do not enable Secure Boot / flash encryption / signed OTA — see ESP-IDF for commercial SKUs.

## Operator checklist

1. Complete SoftAP wizard from a phone or laptop.
2. Prefer a web UI password (≥12) unless the LAN is fully trusted.
3. Change SoftAP password away from `pixelmap1` if SoftAP/APSTA stays enabled.
4. Do not expose the HTTP UI to the public internet.
