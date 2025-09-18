# esp32-lifecycle-manager

## Signing firmware

After building your firmware, generate a `main.bin.sig` file containing the
SHA-384 hash of the image followed by its length in bytes. From the directory
where `make` is run execute:

```bash
openssl sha384 -binary -out build/main.bin.sig build/main.bin
printf "%08x" `cat build/main.bin | wc -c` | xxd -r -p >> build/main.bin.sig
```

The resulting `build/main.bin.sig` must accompany `build/main.bin` when
publishing releases. During an OTA update the device downloads both files,
computes the SHA-384 hash of the image, checks the expected length, and
activates the update only if they match.

## OTA verification

The device downloads `main.bin` and `main.bin.sig` during an OTA update. The
signature file contains the expected SHA-384 hash and image length, allowing the
device to verify the firmware before applying it.
