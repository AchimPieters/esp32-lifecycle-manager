# ESP32 Lifecycle Manager (LCM)

ESP32 Lifecycle Manager (LCM) is a lightweight helper firmware that shepherds an
ESP32-based device through its entire lifecycle. You can think of it as the
"operations center" that boots first, prepares network access, installs your
application firmware, keeps it up to date, and offers recovery tools when
something goes wrong. Even if you are new to ESP-IDF, LCM guides you from an
unconfigured module to a fully managed device in a few straightforward steps.


## Documentation map

To make this repository easier to navigate:

- **End users / installers / support**: see [`enduser.md`](./enduser.md) for onboarding, OTA operation, reset procedures, and troubleshooting.
- **Developers / integrators**: continue with this `README.md` for build, signing, and flashing workflows.
- **Security and operations detail**: see [`docs/security`](./docs/security) and [`docs/operations`](./docs/operations).

### Important security warning

This repository documents the signing flow but does **not** ship production-safe
private keys for you. Always generate and protect your own signing keys before
shipping devices, and never commit private keys to Git.

## How LCM operates

At every boot LCM starts before your application. It evaluates whether the
system can continue normal operation or whether maintenance is required. The
firmware stores Wi-Fi credentials, checks for new releases, validates firmware
signatures, and triggers recovery flows when necessary. This means you do not
need to build these management utilities yourself—LCM bundles the essentials for
remote lifecycle control of ESP32 devices.

## Core capabilities

### Access Point (AP) mode

LCM can launch a temporary access point so the device remains reachable even
before any Wi-Fi network has been configured. Connect to this hotspot to perform
initial setup.

### Captive portal (CNA)

When you join the LCM access point a captive portal automatically opens. From
this portal you can:

- **Select a visible Wi-Fi network** and provide its password.
- **Add a hidden Wi-Fi network manually** by entering the SSID and password.
- **Configure the GitHub repository URL** where LCM should look for new
  firmware releases (`main.bin` and `main.bin.sig`).
- **Blink the status LED for identification** so you can confirm you are working
  with the correct device during installation or servicing.

<img src="https://github.com/AchimPieters/esp32-lifecycle-manager/blob/main/main_screen.png" width="400">

### Downloading and installing `main.bin`

After provisioning, LCM downloads `main.bin` and the accompanying
`main.bin.sig`. It validates an ECDSA P-256 / SHA-256 signature (plus metadata
such as expected image length) and only activates firmware when verification
succeeds.

### Software update (`ota_trigger`)

You can initiate an over-the-air update through an OTA trigger (for example via
an HTTP request) whenever a new release becomes available.

### Update triggering model

LCM can execute updates when requested by your integration (for example via an
application/API-controlled OTA trigger flag), and it also checks GitHub release
state during normal lifecycle flow.

### Reset and recovery options

- **Software factory reset** – Use the LCM API to erase the stored configuration
  and reboot the device.
- **Hardware factory reset** – Quickly power-cycle or reset the device 10 times
  in succession. Once LCM detects the pattern it starts an ~11 second countdown
  and performs the factory reset automatically.

### Automatic firmware version stamping

During installation or update LCM writes the value of
`LIFECYCLE_DEFAULT_FW_VERSION` so you can verify remotely which firmware version
is running.

## Typical installation flow

1. **First boot** – LCM starts in AP mode and exposes the captive portal.
2. **Network provisioning** – The installer links the device to a Wi-Fi network
   and stores the GitHub repository that hosts signed firmware releases.
3. **Firmware acquisition** – LCM downloads `main.bin` and `main.bin.sig`,
   validates both files, and activates the firmware upon success.
4. **Normal operation** – Your application firmware now runs while LCM remains on
   standby to handle OTA updates and recovery tasks.

## Building LCM with ESP-IDF

1. Install ESP-IDF by following the official Espressif documentation.
2. Select the correct target for your board:

   ```bash
   idf.py set-target esp32        # or esp32s2, esp32s3, esp32c3, ...
   ```

3. Build the lifecycle manager firmware:

   ```bash
   idf.py build
   ```

   The resulting `main.bin` will be placed inside the `build/` directory.

### ESP-IDF compatibility status

- **Primary validated line:** ESP-IDF `v5.4.2` (full build + tests in CI).
- **ESP-IDF `v6.0` status:** pending full CI validation; use staging before production rollout.
- **Officially targeted by this repository:** `esp32`, `esp32s2`, `esp32s3`, `esp32c2`, `esp32c3`.
- **Not currently targeted:** `esp32c5`, `esp32c6`, `esp32c61`.

## Signing `main.bin`

Every firmware release must be accompanied by a signature file generated by
`scripts/sign_firmware.sh` (compatibility wrapper) or `generate_sig.sh`
(direct script). Both produce the ECDSA P-256 metadata/signature blob expected
by the OTA verifier in this project:

```bash
./scripts/sign_firmware.sh build/main.bin /path/to/ota_signing_private.pem esp32
# or
./generate_sig.sh build/main.bin /path/to/ota_signing_private.pem
```

Upload both files to a GitHub release (for example version `0.0.1`). Follow
[Semantic Versioning](https://semver.org/) when deciding on the version number:

- Increase **MAJOR** for incompatible API changes.
- Increase **MINOR** when you add backward-compatible functionality.
- Increase **PATCH** for backward-compatible bug fixes.

## Flash instructions by chipset

Use `esptool.py` (installable via `pip install esptool`) and replace the
filenames with the artifacts produced by your build.

### ESP32

```bash
python -m esptool --chip esp32 -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m \
  0x1000 bootloader.bin \
  0x8000 partition-table.bin \
  0xe000 ota_data_initial.bin \
  0x20000 esp32-lifecycle-manager.bin
```

### ESP32-S2

```bash
python -m esptool --chip esp32s2 -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m \
  0x1000 bootloader.bin \
  0x8000 partition-table.bin \
  0xe000 ota_data_initial.bin \
  0x20000 esp32-lifecycle-manager.bin
```

### ESP32-S3

```bash
python -m esptool --chip esp32s3 -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m \
  0x1000 bootloader.bin \
  0x8000 partition-table.bin \
  0xe000 ota_data_initial.bin \
  0x20000 esp32-lifecycle-manager.bin
```

### ESP32-C2

```bash
python -m esptool --chip esp32c2 -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 60m \
  0x1000 bootloader.bin \
  0x8000 partition-table.bin \
  0xe000 ota_data_initial.bin \
  0x20000 esp32-lifecycle-manager.bin
```

### ESP32-C3

```bash
python -m esptool --chip esp32c3 -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m \
  0x1000 bootloader.bin \
  0x8000 partition-table.bin \
  0xe000 ota_data_initial.bin \
  0x20000 esp32-lifecycle-manager.bin
```

### ESP32-C5 / ESP32-C6 / ESP32-C61

These targets are currently **not part of the official targeted set** in this
repository. Bring-up can be done experimentally, but production support is only
claimed for the targets listed above.

## Reset and recovery recap

- **Software factory reset** – Triggered through the LCM API to wipe stored
  configuration and reboot.
- **Hardware factory reset** – Power-cycle or reset the device 10 consecutive
  times. LCM detects the pattern, waits roughly 11 seconds, and then executes the
  factory reset.

Armed with these steps you can quickly provision an ESP32 device, deploy your
own firmware, and keep it managed remotely through LCM.

## Documentation note (audit status)

A security audit from **April 6, 2026** is available at
[`docs/security/code-audit-2026-04-06.md`](./docs/security/code-audit-2026-04-06.md).
If you run verbose/debug logging in production-like environments, review the
audit recommendations first.
