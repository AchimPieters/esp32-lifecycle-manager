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

### Signing modes: Noob vs Advanced

LCM supports two signing workflows:

1. **Noob (default, zero key setup)**
   - Use the bundled keypair in `keys/`.
   - Sign with one command (`./generate_sig.sh build/main.bin`).
   - LCM already contains the matching public key, so OTA install works immediately.

2. **Advanced (custom keypair)**
   - Bring your own private/public keypair.
   - Sign with your custom private key path.
   - Compile LCM with the matching public key before flashing devices.

> The default keypair is intentionally for quick onboarding, development, and demos.
> For production deployments, use the advanced workflow.

## Noob quick start (out-of-the-box)

Use this when you want a fresh clone to "just work".

1. **Clone and enter the repo**.
2. **Pick your ESP32 target**:

   ```bash
   idf.py set-target esp32        # or esp32s2, esp32s3, esp32c2, esp32c3
   ```

3. **Build LCM**:

   ```bash
   idf.py build
   ```

4. **Sign your application firmware** (uses bundled default key automatically):

   ```bash
   ./generate_sig.sh build/main.bin
   ```

5. **Publish both files** to your release source (GitHub release assets):
   - `main.bin`
   - `main.bin.sig`

6. **Onboard device in captive portal** and set your repository URL.
7. **LCM downloads + verifies + installs** the firmware.

If you download this repository again later and build a new firmware, the same
command still works by default because the bundled dev key is part of the repo.

## Advanced signing (custom keys)

Use this for production or stricter security requirements.

1. Generate your own ECDSA P-256 keypair.
2. Keep your private key outside source control.
3. Sign firmware with your custom private key:

   ```bash
   ./generate_sig.sh build/main.bin /path/to/ota_signing_private.pem
   ```

4. Update the public key used by LCM verification (`OTA_PUBLIC_KEY_PEM` in
   `main/github_update.c`) so it matches your private key.
5. Rebuild and flash LCM.
6. Publish `main.bin` + `main.bin.sig` from the same signed build.

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
`main.bin.sig`. It validates the ECDSA P-256 / SHA-256 signature as well as the
reported file size and only activates the firmware when the verification succeeds.

### Software update (`ota_trigger`)

You can initiate an over-the-air update through an OTA trigger (for example via
an HTTP request) whenever a new release becomes available.

### Hardware-assisted update

LCM can listen to hardware controls such as a physical button to start or
confirm the update flow. This is useful for devices that must update without a
connected computer.

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
- **CI-validated targets:** `esp32`, `esp32s2`, `esp32s3`, `esp32c2`, `esp32c3`.
- **Declared in component manifest, not yet CI-validated:** `esp32c5`, `esp32c6` — bring-up is possible but production support is not guaranteed.
- **Not targeted:** `esp32c61`.

## Signing `main.bin`

Every firmware release must include a matching `.sig` file generated with
`generate_sig.sh`.

- Noob/default flow: `./generate_sig.sh build/main.bin`
- Advanced/custom flow: `./generate_sig.sh build/main.bin /path/to/private.pem`

In all cases, upload both `main.bin` and `main.bin.sig` to the release source.
Follow [Semantic Versioning](https://semver.org/) when deciding on the version number:

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

`esp32c5` and `esp32c6` are declared in the component manifest and can be built,
but they are **not yet CI-validated**. Production use is at your own risk until
full cross-target build checks pass. `esp32c61` is not targeted at all.

## Reset and recovery recap

- **Software factory reset** – Triggered through the LCM API to wipe stored
  configuration and reboot.
- **Hardware factory reset** – Power-cycle or reset the device 10 consecutive
  times. LCM detects the pattern, waits roughly 11 seconds, and then executes the
  factory reset.

Armed with these steps you can quickly provision an ESP32 device, deploy your
own firmware, and keep it managed remotely through LCM.
