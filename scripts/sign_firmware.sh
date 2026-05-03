#!/bin/sh
# Backward-compatible wrapper used by CI to produce main.bin.sig.
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 3 ]; then
  echo "Usage: $0 <firmware.bin> [private_key.pem] [target]" >&2
  exit 1
fi

FIRMWARE_PATH="$1"
PRIVATE_KEY_PATH="${2:-keys/ota_signing_private.pem}"
# Third argument is accepted for legacy compatibility but not required by
# the current signature format.
TARGET="${3:-}"

if [ -n "$TARGET" ]; then
  echo "Info: target '$TARGET' argument accepted for compatibility." >&2
fi

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

exec "$REPO_ROOT/generate_sig.sh" "$FIRMWARE_PATH" "$PRIVATE_KEY_PATH"
