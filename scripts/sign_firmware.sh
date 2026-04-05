#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "Usage: $0 <main.bin|build_dir> <private_key.pem> [target]"
  exit 1
fi

TARGET_PATH="$1"
PRIVATE_KEY_PATH="$2"
TARGET_NAME="${3:-esp32}"

case "$TARGET_NAME" in
  esp32) TARGET_ID=1 ;;
  esp32c2) TARGET_ID=2 ;;
  esp32c3) TARGET_ID=3 ;;
  esp32s2) TARGET_ID=4 ;;
  esp32s3) TARGET_ID=5 ;;
  esp32c5) TARGET_ID=6 ;;
  esp32c6) TARGET_ID=7 ;;
  *) echo "Unsupported target '$TARGET_NAME'"; exit 1 ;;
esac

if [[ -d "$TARGET_PATH" ]]; then
  TARGET_PATH="${TARGET_PATH%/}/main.bin"
fi

if [[ ! -f "$TARGET_PATH" ]]; then
  echo "Firmware image not found: $TARGET_PATH" >&2
  exit 1
fi

if [[ ! -f "$PRIVATE_KEY_PATH" ]]; then
  echo "Private key not found: $PRIVATE_KEY_PATH" >&2
  exit 1
fi

SIGNATURE_PATH="$TARGET_PATH.sig"
TMP_HASH="$(mktemp)"
TMP_DER="$(mktemp)"
trap 'rm -f "$TMP_HASH" "$TMP_DER"' EXIT

openssl dgst -sha256 -binary -out "$TMP_HASH" "$TARGET_PATH"
openssl dgst -sha256 -sign "$PRIVATE_KEY_PATH" -out "$TMP_DER" "$TARGET_PATH"

python3 - "$TARGET_PATH" "$TMP_HASH" "$TMP_DER" "$SIGNATURE_PATH" "$TARGET_ID" <<'PY'
import os
import struct
import sys

firmware_path, hash_path, sig_der_path, output_path, target_id = sys.argv[1:6]
firmware_len = os.path.getsize(firmware_path)
firmware_hash = open(hash_path, 'rb').read()
signature_der = open(sig_der_path, 'rb').read()

MAGIC = 0x4C434D53  # LCMS
VERSION = 2
ALGORITHM = 1  # ECDSA P-256 SHA-256
TARGET_ID = int(target_id)

header = struct.pack('<IBBHI32sH', MAGIC, VERSION, ALGORITHM, TARGET_ID,
                     firmware_len, firmware_hash, len(signature_der))
with open(output_path, 'wb') as f:
    f.write(header)
    f.write(signature_der)

print(f"Wrote {output_path} ({len(header) + len(signature_der)} bytes, target_id={TARGET_ID})")
PY
