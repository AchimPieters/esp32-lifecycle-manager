#!/usr/bin/env sh
# Compatibility wrapper for CI and local tooling.
# Usage: ./scripts/sign_firmware.sh <firmware.bin|build_dir> <private_key.pem> [target]
# The optional [target] argument is accepted for backward compatibility but is
# not required by the current signature format.
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)

FIRMWARE_PATH="${1:-build/main.bin}"
PRIVATE_KEY_PATH="${2:-keys/ota_signing_private.pem}"
TARGET="${3:-}"

if [ -n "$TARGET" ]; then
  echo "[sign_firmware] Note: target '$TARGET' is accepted but not used by generate_sig.sh" >&2
fi

exec "$REPO_ROOT/generate_sig.sh" "$FIRMWARE_PATH" "$PRIVATE_KEY_PATH"
