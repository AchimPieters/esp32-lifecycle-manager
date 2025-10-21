# ESP32 Lifecycle Manager (LCM)

ESP32 Lifecycle Manager (LCM) is a lightweight helper firmware that shepherds an
ESP32-based device through its entire lifecycle. You can think of it as the
"operations center" that boots first, prepares network access, installs your
application firmware, keeps it up to date, and offers recovery tools when
something goes wrong. Even if you are new to ESP-IDF, LCM guides you from an
unconfigured module to a fully managed device in a few straightforward steps.

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
`main.bin.sig`. It validates the SHA-384 signature as well as the reported file
size and only activates the firmware when the verification succeeds.

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

## Using LCM inside your OTA application

LCM can be compiled directly alongside your own ESP-IDF firmware. You can either
consume the component from the [Espressif component registry](https://components.espressif.com/components/achimpieters/esp32-lifecycle-manager)
or vendor it locally. The reusable component itself lives under
`components/ota_lifecycle` in this repository.

```yaml
# idf_component.yml in the root of your application
dependencies:
  achimpieters/esp32-lifecycle-manager: "*"
```

If you prefer to ship the source with your project, add it as a submodule inside
your app’s `components/` directory:

```bash
git submodule add https://github.com/AchimPieters/esp32-lifecycle-manager \
  components/esp32-lifecycle-manager
```

Point `EXTRA_COMPONENT_DIRS` (for example in your project `CMakeLists.txt`) to
`components/esp32-lifecycle-manager/components/ota_lifecycle` so the helper API
is picked up during the build.

Once the component is available, initialize it early in your `app_main` (or the
equivalent entry point of your firmware):

```c
#include "esp32_lifecycle_manager.h"

static void perform_factory_reset(void *ctx) {
    // Wipe your application state and reboot.
}

void app_main(void) {
    ESP_ERROR_CHECK(lifecycle_nvs_init());
    lifecycle_register_factory_reset_callback(perform_factory_reset, NULL);
    lifecycle_log_post_reset_state();

    // Continue with the rest of your application boot sequence.
}
```

`lifecycle_log_post_reset_state()` persists the rapid restart counter in NVS and
executes your callback once the threshold is reached (10 power cycles inside the
configured window by default). Adjust the behaviour through the Kconfig options
provided by the component:

- **Rapid restart threshold** – `CONFIG_LCM_FACTORY_RESET_TRIGGER_COUNT`
- **Countdown before invoking the callback** – `CONFIG_LCM_FACTORY_RESET_COUNTDOWN_SECONDS`
- **Time window that qualifies as a "rapid" restart** – `CONFIG_LCM_RESTART_COUNTER_TIMEOUT_MS`
- **NVS storage location** – `CONFIG_LCM_RESTART_COUNTER_NAMESPACE` and
  `CONFIG_LCM_RESTART_COUNTER_KEY`
- **Logging tag** – `CONFIG_LCM_LOG_TAG`

Set the values via `idf.py menuconfig` or pin them in your
`sdkconfig.defaults`. With these hooks in place you can build and flash your
firmware as usual. Rapid resets should increment the counter and trigger the
factory reset countdown exactly as documented in the LCM README.

With these hooks in place you can build and flash your firmware as usual. Rapid
resets should increment the counter and trigger the factory reset countdown
exactly as documented in the LCM README.

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

## Signing `main.bin`

Every firmware release must be accompanied by a signature file. From the project
root run:

```bash
openssl sha384 -binary -out build/main.bin.sig build/main.bin
printf "%08x" "$(wc -c < build/main.bin)" | xxd -r -p >> build/main.bin.sig
```

> Tip: the repository includes `./generate_sig.sh` to automate these commands.

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
  0x1000 esp32-bootloader.bin \
  0x8000 esp32-partition-table.bin \
  0xe000 esp32-ota_data_initial.bin \
  0x20000 esp32-lifecycle-manager.bin
```

### ESP32-S2

```bash
python -m esptool --chip esp32s2 -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m \
  0x1000 esp32s2-bootloader.bin \
  0x8000 esp32s2-partition-table.bin \
  0xe000 esp32s2-ota_data_initial.bin \
  0x20000 esp32s2-lifecycle-manager.bin
```

### ESP32-S3

```bash
python -m esptool --chip esp32s3 -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m \
  0x0    esp32s3-bootloader.bin \
  0x8000 esp32s3-partition-table.bin \
  0xe000 esp32s3-ota_data_initial.bin \
  0x20000 esp32s3-lifecycle-manager.bin
```

### ESP32-C2

```bash
python -m esptool --chip esp32c2 -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 60m \
  0x0    esp32c2-bootloader.bin \
  0x8000 esp32c2-partition_table/partition-table.bin \
  0xe000 esp32c2-ota_data_initial.bin \
  0x20000 esp32c2-lifecycle-manager.bin
```

### ESP32-C3

```bash
python -m esptool --chip esp32c3 -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m \
  0x0    esp32c3-bootloader.bin \
  0x8000 esp32c3-partition-table.bin \
  0xe000 esp32c3-ota_data_initial.bin \
  0x20000 esp32c3-lifecycle-manager.bin
```

### ESP32-C5

```bash
python -m esptool --chip esp32c5 -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m \
  0x2000 esp32c5-bootloader.bin \
  0x8000 esp32c5-partition-table.bin \
  0xe000 esp32c5-ota_data_initial.bin \
  0x20000 esp32c5-lifecycle-manager.bin
```

### ESP32-C6 / ESP32-C61

```bash
python -m esptool --chip esp32c6 -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m \
  0x0    esp32c6-bootloader.bin \
  0x8000 esp32c6-partition-table.bin \
  0xe000 esp32c6-ota_data_initial.bin \
  0x20000 esp32c6-lifecycle-manager.bin
```

> For ESP32-C61 use the same offsets and swap only the filenames.

## Reset and recovery recap

- **Software factory reset** – Triggered through the LCM API to wipe stored
  configuration and reboot.
- **Hardware factory reset** – Power-cycle or reset the device 10 consecutive
  times. LCM detects the pattern, waits roughly 11 seconds, and then executes the
  factory reset.

Armed with these steps you can quickly provision an ESP32 device, deploy your
own firmware, and keep it managed remotely through LCM.
