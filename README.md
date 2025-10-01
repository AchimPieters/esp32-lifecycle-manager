# esp32-lifecycle-manager

## Supported ESP-IDF version and targets

The project now requires ESP-IDF **v5.5.1** (or newer within the v5 release
line) and has been validated across all Wi-Fi capable ESP32 variants that share
the same lifecycle and OTA flow:

* ESP32
* ESP32-S2 / ESP32-S3
* ESP32-C2 / ESP32-C3 / ESP32-C5 / ESP32-C6
* ESP32-C61

Select the desired target before building, for example:

```bash
idf.py set-target esp32c6
```

For the ESP32-C61 preview target, use the preview subcommand included with
ESP-IDF v5.5.1:

```bash
idf.py --preview set-target esp32c61
```

## Signing firmware

After building your firmware, generate a `main.bin.sig` file containing the
SHA-384 hash of the image followed by its length in bytes by running the helper
script:

```bash
./generate_sig.sh [path/to/build_or_main.bin]
```

When no argument is supplied the script expects the ESP-IDF artefacts inside
`build/` and operates on `build/main.bin`, producing `build/main.bin.sig`. You
may also pass a different build directory (for example `./generate_sig.sh
example/build`) or the explicit path to a `main.bin` image. The resulting
`main.bin.sig` must accompany the image when publishing releases. During an OTA
update the device downloads both files, computes the SHA-384 hash of the image,
checks the expected length, and activates the update only if they match.

## OTA verification

The device downloads `main.bin` and `main.bin.sig` during an OTA update. The
signature file contains the expected SHA-384 hash and image length, allowing the
device to verify the firmware before applying it.

## Consecutive restart factory reset window

The Lifecycle Manager performs a factory reset after detecting between 10 and
12 consecutive restarts. The restarts must all occur within the configurable
`CONFIG_LCM_RESTART_COUNTER_TIMEOUT_MS` window (5 seconds by default). If the
device runs longer than that window without restarting, the counter resets and
the sequence must be repeated from the beginning. Once the restart counter
reaches the configured window, the device enters an on-device countdown that
lasts roughly 11 seconds before the factory reset routine runs. Make sure to
leave the device powered on during this countdown; toggling power again during
this period keeps the counter inside the 10–12 restart window so the
`lifecycle_factory_reset_and_reboot()` helper eventually executes.
