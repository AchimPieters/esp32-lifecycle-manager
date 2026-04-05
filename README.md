# ESP32 Lifecycle Manager (LCM)

LCM is a generic **signed update loader** for ESP32-family chips. End users flash an LCM image once, configure an update source, and LCM securely downloads `main.bin` + `main.bin.sig`, verifies them, and installs updates.

## Quick start for end users (official mode)

1. Download and flash the binary matching your chip (examples):
   - `lcm-esp32.bin`
   - `lcm-esp32c2.bin`
   - `lcm-esp32c3.bin`
2. Open the captive portal and configure Wi-Fi + update source.
3. Set update source as either:
   - GitHub repository slug: `owner/repo` (release assets `main.bin`, `main.bin.sig`), or
   - Direct HTTP/HTTPS base URL containing `main.bin` and `main.bin.sig`.
4. Trigger update.

In official mode, no key generation and no rebuild is needed.

## Publishing signed firmware

Use local scripts:

```bash
# Official signing for standard LCM builds (default mode)
export LCM_OFFICIAL_SIGNING_KEY=/secure/path/official_private.pem
./scripts/sign_firmware.sh build/main.bin "" esp32c3 --official

# Advanced/custom signing for custom-key LCM builds
./scripts/generate_keys.sh keys/private publisher
./scripts/sign_firmware.sh build/main.bin keys/private/publisher_private.pem esp32c3 --custom
```

For the easiest publisher flow, copy `scripts/sign.py` into your app `build/`
directory and run it there:

```bash
cp /path/to/esp32-lifecycle-manager/scripts/sign.py .
export LCM_OFFICIAL_SIGNING_KEY=/secure/path/official_private.pem
python3 sign.py
```

This generates `main.bin.sig` next to `main.bin` and auto-detects target from
`../sdkconfig`.

Outputs:
- private key (local only)
- public key (shareable)
- `main.bin.sig`

## Building LCM with a custom key

1. Copy `keys/custom_public_key.example.h` to `keys/custom_public_key.h`.
2. Paste your public key PEM into `LCM_CUSTOM_PUBLIC_KEY_PEM`.
3. Enable custom mode:

```bash
idf.py menuconfig
# StudioPieters -> Trusted publisher mode -> Custom publisher mode
# (sets CONFIG_LCM_CUSTOM_KEY=y)
```

4. Build per target:

```bash
idf.py set-target esp32 && idf.py build
idf.py set-target esp32c2 && idf.py build
idf.py set-target esp32c3 && idf.py build
```

## Security model

- **Official builds** trust the built-in official public key (`keys/official_public_key.h`).
- **Custom builds** trust your custom public key (`keys/custom_public_key.h`).
- In Official mode, end users explicitly trust the official signer trust-chain shipped by this project.
- If signature verification fails, update is rejected.
- If firmware target in signature does not match device target, update is rejected.
- Private signing keys must never be committed.

## Architecture notes

LCM separates:
- network/release fetch,
- signature verification + trusted key provider,
- OTA install + reboot.

Manifest support (`manifest.json`) is intentionally prepared as a next step; v1 keeps minimal package support with `main.bin` and `main.bin.sig`.

## Key rotation (operational guidance)

- Official mode should rotate signing keys periodically.
- Rotation should be done with overlap windows (old + new trusted public keys) in a later release.
- Custom publishers should keep a migration plan for replacing their own trust anchor.

## Multi-target release artifacts

CI builds target-aware artifacts for release distribution. Naming convention:
- `lcm-esp32.bin`
- `lcm-esp32c2.bin`
- `lcm-esp32c3.bin`

Extendable to additional supported ESP32 variants.
