#!/bin/sh
# Backward-compatible firmware signing entrypoint used by CI/integrations.
# Usage: ./scripts/sign_firmware.sh <firmware_bin> [private_key] [target]
# The optional target argument is accepted for compatibility but not required.
set -eu

FIRMWARE_PATH="${1:-build/main.bin}"
PRIVATE_KEY_PATH="${2:-keys/ota_signing_private.pem}"
TARGET="${3:-}"

if [ -n "$TARGET" ]; then
  echo "Info: target '$TARGET' supplied (kept for compatibility)."
fi

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
REPO_ROOT="$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)"

exec "$REPO_ROOT/generate_sig.sh" "$FIRMWARE_PATH" "$PRIVATE_KEY_PATH"
