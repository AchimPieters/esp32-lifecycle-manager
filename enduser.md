# ESP32 Lifecycle Manager (LCM) – End User Guide

This guide is written for installers, support staff, and operators. It explains
how to provision a device, perform updates, recover from failure states, and
apply safe key-handling practices.

## 1) What LCM does for you

LCM is the management firmware that boots before your application firmware and
handles lifecycle operations:

- onboarding devices through AP/captive portal,
- storing Wi-Fi credentials,
- fetching signed firmware from your configured repository,
- validating signatures before booting updates,
- and offering recovery/reset flows.

## 2) What you need before onboarding

Prepare the following:

- device flashed with ESP32 Lifecycle Manager,
- phone/laptop that can connect to Wi-Fi,
- Wi-Fi SSID + password,
- GitHub repo/release source that contains:
  - `main.bin`
  - `main.bin.sig`.

## 3) First-time onboarding

1. Power on the device.
2. Join the LCM hotspot (AP mode).
3. Let captive portal open automatically (or browse to `http://192.168.4.1`).
4. In the portal:
   - choose visible SSID or enter hidden SSID,
   - enter Wi-Fi password,
   - configure repository URL used for firmware fetch,
   - optionally blink LED to identify the target board.
5. Save/apply settings.
6. Wait for LCM to download firmware and signature.
7. Device validates signature and then boots application firmware.

## 4) OTA update behavior (operator view)

- LCM only accepts update binaries when verification succeeds.
- If verification fails (signature mismatch, bad metadata, corrupted transfer),
  update is rejected and device should remain on a known good image.
- Trigger update using your integration's OTA trigger flow.

## 5) Reset and recovery

### Software factory reset
Use your exposed API/control path to erase stored configuration and reboot.

### Hardware factory reset pattern
Power-cycle or reset the board **10 consecutive times** to trigger the
hardware reset flow and return to onboarding mode.

## 6) Quick troubleshooting

### Captive portal does not appear

- Confirm you are connected to the LCM AP.
- Open `http://192.168.4.1` manually.
- Disable mobile data temporarily to force captive portal behavior on phones.

### Firmware fails to install

- Confirm both `main.bin` and `main.bin.sig` are published.
- Ensure the signature matches the exact binary version.
- Verify repository URL and release asset names.

### Device keeps returning to setup mode

- Re-enter Wi-Fi credentials and ensure signal is stable.
- Re-run onboarding and re-check repository settings.
- Use factory reset and provision again.

## 7) Security note about keys in this repository

This repository currently includes sample key material in `keys/`:

- `keys/ota_signing_private.pem`
- `keys/ota_signing_public.pem`

These must be treated as **example-only** and **not trusted for production**.

For real deployments:

1. Generate your own key pair.
2. Keep private keys out of Git and CI logs.
3. Re-sign all release binaries using your private key.
4. Distribute only the matching public key to devices.
5. Rotate keys immediately if exposure is suspected.

## 8) Where to find technical docs

- Engineer/developer reference: `README.md`
- Security docs: `docs/security/`
- Ops/recovery docs: `docs/operations/`
