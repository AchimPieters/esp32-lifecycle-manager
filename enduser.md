# ESP32 Lifecycle Manager (LCM) – End User Guide

This guide explains how an end user or installer uses LCM after flashing it to an ESP32-family board.

## 1) Official mode vs custom mode

### Official mode (default)
For most users.

- The flashed LCM build already contains the official public verification key.
- You do **not** need to generate keys.
- You do **not** need to rebuild LCM.
- LCM accepts firmware only when it is signed by the matching official private key.

### Custom publisher mode (advanced)
For advanced users/integrators.

- You build LCM yourself with your own public key.
- You sign your own firmware with your own private key.
- You control your own trust chain.

## 2) What you need before onboarding

Prepare:

- an ESP32 board flashed with the correct LCM image for that chip type,
- phone/laptop with Wi-Fi,
- Wi-Fi SSID + password,
- update source that provides at least:
  - `main.bin`
  - `main.bin.sig`

Supported source formats:

1. GitHub repository slug: `owner/repo` (release assets), or
2. Direct HTTP/HTTPS base URL that hosts `main.bin` and `main.bin.sig`.

## 3) First-time onboarding

1. Power on the device.
2. Connect to the LCM AP hotspot.
3. Open captive portal (or browse to `http://192.168.4.1`).
4. Configure:
   - Wi-Fi credentials,
   - update source,
   - optional LED blink for board identification.
5. Save settings.
6. LCM downloads update artifacts.
7. LCM verifies signature and target compatibility.
8. If valid, firmware is activated and device reboots.

## 4) Update validation behavior

LCM rejects updates when one of these checks fails:

- package incomplete (`main.bin` / `main.bin.sig` missing),
- signature invalid,
- firmware not signed by trusted publisher,
- firmware target does not match device,
- download/HTTP failure.

When validation fails, LCM should stay on a known good image.

## 5) Quick troubleshooting

### Captive portal does not open

- Confirm you are connected to the LCM AP.
- Open `http://192.168.4.1` manually.
- Disable mobile data temporarily (phone CNA behavior).

### Update fails

- Verify update source path/repo is correct.
- Confirm both `main.bin` and `main.bin.sig` are available.
- Confirm signature was generated for the same `main.bin` and correct chip target.

### Device keeps returning to setup mode

- Re-enter Wi-Fi credentials and verify signal quality.
- Re-check update source configuration.
- Run factory reset and onboard again.

## 6) Reset and recovery

### Software factory reset
Use your control/API path to clear stored configuration and reboot.

### Hardware reset pattern
Power-cycle or reset the board **10 consecutive times** to trigger hardware factory reset and return to onboarding mode.

## 7) Security notes

- Never publish private signing keys.
- In official mode, end users trust the official signer chain shipped in LCM.
- In custom mode, users trust the custom key compiled into their own build.

## 8) More documentation

- Main project docs: `README.md`
- Security docs: `docs/security/`
- Operations/recovery docs: `docs/operations/`
