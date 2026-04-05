#!/usr/bin/env bash
set -euo pipefail

TARGETS=(esp32 esp32c2 esp32c3)
OUT_DIR="${1:-dist}"
mkdir -p "$OUT_DIR"

for target in "${TARGETS[@]}"; do
  echo "==> Building $target"
  idf.py set-target "$target"
  idf.py build
  cp build/main.bin "$OUT_DIR/lcm-${target}.bin"
  cp build/bootloader/bootloader.bin "$OUT_DIR/lcm-${target}-bootloader.bin"
  cp build/partition_table/partition-table.bin "$OUT_DIR/lcm-${target}-partition-table.bin"
done

echo "Artifacts written to $OUT_DIR"
