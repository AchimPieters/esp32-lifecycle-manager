#!/bin/sh
# Generate a cryptographic main.bin.sig using ECDSA P-256 + SHA-256.
set -eu

TARGET_PATH="${1:-build/main.bin}"
PRIVATE_KEY_PATH="${2:-keys/ota_signing_private.pem}"

if [ -d "$TARGET_PATH" ]; then
  TARGET_PATH="${TARGET_PATH%/}/main.bin"
fi

if [ ! -f "$TARGET_PATH" ]; then
  echo "Firmware image not found: $TARGET_PATH" >&2
  exit 1
fi

if [ ! -f "$PRIVATE_KEY_PATH" ]; then
  echo "Private key not found: $PRIVATE_KEY_PATH" >&2
  echo "Tip: use the bundled default key at keys/ota_signing_private.pem or pass your own path as arg 2." >&2
  echo "Example: ./generate_sig.sh build/main.bin /path/to/custom_private.pem" >&2
  exit 1
fi

SIGNATURE_PATH="$TARGET_PATH.sig"
TMP_HASH="$(mktemp)"
TMP_DER="$(mktemp)"
trap 'rm -f "$TMP_HASH" "$TMP_DER"' EXIT

openssl dgst -sha256 -binary -out "$TMP_HASH" "$TARGET_PATH"
openssl dgst -sha256 -sign "$PRIVATE_KEY_PATH" -out "$TMP_DER" "$TARGET_PATH"

python3 - "$TARGET_PATH" "$TMP_HASH" "$TMP_DER" "$SIGNATURE_PATH" <<'PY'
import os
import struct
import sys

firmware_path, hash_path, sig_der_path, output_path = sys.argv[1:5]
firmware_len = os.path.getsize(firmware_path)
firmware_hash = open(hash_path, 'rb').read()
signature_der = open(sig_der_path, 'rb').read()

MAGIC = 0x4C434D53  # LCMS
VERSION = 1
ALGORITHM = 1  # ECDSA P-256 SHA-256
RESERVED = 0

header = struct.pack('<IBBHI32sH', MAGIC, VERSION, ALGORITHM, RESERVED,
                     firmware_len, firmware_hash, len(signature_der))
with open(output_path, 'wb') as f:
    f.write(header)
    f.write(signature_der)

print(f"Wrote {output_path} ({len(header) + len(signature_der)} bytes)")
PY
