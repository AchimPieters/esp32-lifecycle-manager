#!/bin/sh
# Generate firmware/main.bin.sig containing SHA-384 hash and image length.
set -eu
if [ ! -f firmware/main.bin ]; then
  echo "firmware/main.bin not found" >&2
  exit 1
fi
openssl sha384 -binary -out firmware/main.bin.sig firmware/main.bin
printf "%08x" $(wc -c < firmware/main.bin) | xxd -r -p >> firmware/main.bin.sig
