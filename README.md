# esp32-lifecycle-manager

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

The Lifecycle Manager performs a factory reset after detecting 10 consecutive
restarts. The restarts must all occur within the configurable
`CONFIG_LCM_RESTART_COUNTER_TIMEOUT_MS` window (60 seconds by default). If the
device runs longer than that window without restarting, the counter resets and
the sequence must be repeated from the beginning.
