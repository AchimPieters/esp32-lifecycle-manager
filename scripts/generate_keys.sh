#!/usr/bin/env bash
set -euo pipefail

OUT_DIR="${1:-keys/private}"
BASE_NAME="${2:-publisher}"

mkdir -p "$OUT_DIR"
PRIVATE_KEY="$OUT_DIR/${BASE_NAME}_private.pem"
PUBLIC_KEY="$OUT_DIR/${BASE_NAME}_public.pem"

openssl ecparam -name prime256v1 -genkey -noout -out "$PRIVATE_KEY"
openssl ec -in "$PRIVATE_KEY" -pubout -out "$PUBLIC_KEY"

chmod 600 "$PRIVATE_KEY"

echo "Generated private key: $PRIVATE_KEY"
echo "Generated public key : $PUBLIC_KEY"
echo "Keep the private key secret and out of git."
