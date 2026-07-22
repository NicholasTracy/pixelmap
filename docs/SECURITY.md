# PixelMap security model

## Defaults (production-oriented)

| Control | Behavior |
|---------|----------|
| Web UI auth | Always required once credentials exist (forced on first boot) |
| SoftAP password | Unique random (≥16 chars); never `pixelmap1` |
| Web password storage | Salted SHA-256 (`$5$salt$hex`); plaintext only on first UART print |
| SoftAP while on LAN | Off when STA is healthy unless APSTA or AP fallback enabled |
| AP fallback | Off by default |
| Sessions | 128-bit token, `HttpOnly` + `SameSite=Lax`, 8 h sliding TTL |
| Login | Rate-limited (5 failures → 60 s lockout) |
| Factory reset | Session + re-entered web password |
| OTA | Session required; ESP-IDF image checks (not signature by default) |

## First boot / recovery

1. Open USB serial at **115200**.
2. Copy **SoftAP password** and **Web UI password** from the log.
3. Join `PixelMap-XXXX`, open `http://192.168.4.1`, sign in.
4. Change the web password (banner) before leaving the device unattended.

SoftAP password is also logged on each boot (physical UART access). The web password is **not** shown again; factory reset regenerates both.

## Threat model

- **Trusted:** USB serial / physical possession, SoftAP clients who know the unique SoftAP PSK during setup.
- **Not trusted by default:** Shared LAN peers (HTTP is cleartext), SoftAP with a leaked PSK.
- **Not covered yet in stock builds:** TLS, Secure Boot, flash encryption, signed OTA. Enable these in a production SKU `sdkconfig` if you ship commercial hardware.

## Recommended production SKU extras (ESP-IDF)

- `CONFIG_SECURE_BOOT` + signed app images in CI
- `CONFIG_SECURE_FLASH_ENC_ENABLED` (flash encryption)
- Custom partition / NVS encryption as needed
- Prefer isolating controllers on a show/control VLAN

## Operator checklist

1. Change bootstrap web password (≥12 chars).
2. Join venue Wi‑Fi; leave SoftAP / AP fallback off unless you need recovery.
3. If SoftAP must stay on, use a strong unique AP password and treat `192.168.4.1` as sensitive.
4. Do not expose the HTTP UI to the public internet.
