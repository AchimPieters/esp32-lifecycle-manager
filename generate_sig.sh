#!/bin/sh
# Generate a main.bin.sig file containing SHA-384 hash and image length.
set -eu

TARGET_PATH="${1:-build/main.bin}"

if [ -d "$TARGET_PATH" ]; then
  TARGET_PATH="${TARGET_PATH%/}/main.bin"
fi

if [ ! -f "$TARGET_PATH" ]; then
  echo "Firmware image not found: $TARGET_PATH" >&2
  exit 1
fi

SIGNATURE_PATH="$TARGET_PATH.sig"

openssl sha384 -binary -out "$SIGNATURE_PATH" "$TARGET_PATH"
printf "%08x" "$(wc -c < "$TARGET_PATH")" | xxd -r -p >> "$SIGNATURE_PATH"
