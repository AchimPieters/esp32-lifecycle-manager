#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/provision_nvs_keys.sh --chip <chip> --port <serial_port> [--offset <hex>] [--keys-bin <path>] [--device-id <id>] [--audit-log <path>] [--flash]

Description:
  Generates a unique NVS keys partition binary and optionally flashes it to a target.

Options:
  --chip <chip>        ESP chip type (e.g. esp32, esp32c6)
  --port <port>        Serial port for flashing (e.g. /dev/ttyUSB0)
  --offset <hex>       Partition offset for nvs_keys (default: 0x10000)
  --keys-bin <path>    Output key binary (default: ./out/nvs_keys.bin)
  --device-id <id>     Device identifier for provisioning audit records
  --audit-log <path>   JSONL audit log file (default: ./out/provisioning_audit.jsonl)
  --flash              Actually flash generated keys (dry-run when omitted)
  -h, --help           Show this help

Environment:
  IDF_PATH must be set so nvs_partition_gen.py and esptool.py can be located.
EOF
}

CHIP=""
PORT=""
OFFSET="0x10000"
KEYS_BIN="./out/nvs_keys.bin"
DEVICE_ID=""
AUDIT_LOG="./out/provisioning_audit.jsonl"
DO_FLASH=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --chip) CHIP="${2:-}"; shift 2 ;;
    --port) PORT="${2:-}"; shift 2 ;;
    --offset) OFFSET="${2:-}"; shift 2 ;;
    --keys-bin) KEYS_BIN="${2:-}"; shift 2 ;;
    --device-id) DEVICE_ID="${2:-}"; shift 2 ;;
    --audit-log) AUDIT_LOG="${2:-}"; shift 2 ;;
    --flash) DO_FLASH=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage; exit 2 ;;
  esac
done

if [[ -z "$CHIP" || -z "$PORT" ]]; then
  echo "Error: --chip and --port are required." >&2
  usage
  exit 2
fi

if [[ -z "${IDF_PATH:-}" ]]; then
  echo "Error: IDF_PATH is not set." >&2
  exit 2
fi

NVS_GEN="$IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py"
if [[ ! -f "$NVS_GEN" ]]; then
  echo "Error: nvs_partition_gen.py not found at $NVS_GEN" >&2
  exit 2
fi

mkdir -p "$(dirname "$KEYS_BIN")"
mkdir -p "$(dirname "$AUDIT_LOG")"

echo "[1/2] Generating unique NVS key partition: $KEYS_BIN"
python3 "$NVS_GEN" generate-key --keyfile "$KEYS_BIN"
KEY_SHA256="$(sha256sum "$KEYS_BIN" | awk '{print $1}')"

if [[ -n "$DEVICE_ID" ]]; then
  printf '{"ts":"%s","device_id":"%s","chip":"%s","keys_bin":"%s","sha256":"%s","flash_requested":%s}\n' \
    "$(date -u +%FT%TZ)" "$DEVICE_ID" "$CHIP" "$KEYS_BIN" "$KEY_SHA256" "$DO_FLASH" >> "$AUDIT_LOG"
  echo "Audit record written to $AUDIT_LOG"
fi

if [[ "$DO_FLASH" -eq 0 ]]; then
  echo "[2/2] Dry-run: skipping flash. Use --flash to write keys to device."
  echo "Would run:"
  echo "  python -m esptool --chip $CHIP -p $PORT write_flash $OFFSET $KEYS_BIN"
  exit 0
fi

if [[ -z "$DEVICE_ID" ]]; then
  echo "Error: --device-id is required when --flash is used (for provisioning traceability)." >&2
  exit 2
fi

echo "[2/2] Flashing NVS keys partition to $PORT at offset $OFFSET"
python -m esptool --chip "$CHIP" -p "$PORT" write_flash "$OFFSET" "$KEYS_BIN"
echo "Done."
