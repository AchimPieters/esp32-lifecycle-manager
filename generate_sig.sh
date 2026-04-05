#!/usr/bin/env bash
set -euo pipefail

TARGET_PATH="${1:-build/main.bin}"
PRIVATE_KEY_PATH="${2:-keys/private/publisher_private.pem}"
TARGET_NAME="${3:-esp32}"

"$(dirname "$0")/scripts/sign_firmware.sh" "$TARGET_PATH" "$PRIVATE_KEY_PATH" "$TARGET_NAME"
