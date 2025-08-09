#!/bin/bash
# Maak een geldige main.bin.sig (52 bytes) voor ESP32 OTA
# 48 bytes: SHA-384 digest
# 4 bytes: firmware size (uint32, little-endian)
# Gebruik: ./make_sig.sh build/main.bin

# Input controleren
if [ -z "$1" ]; then
    echo "Gebruik: $0 <pad/naar/main.bin>"
    exit 1
fi

BINFILE="$1"
SIGFILE="${BINFILE}.sig"

# Bestaat het bin-bestand?
if [ ! -f "$BINFILE" ]; then
    echo "Bestand niet gevonden: $BINFILE"
    exit 1
fi

echo "🔹 Maak SHA-384 hash..."
openssl sha384 -binary -out "$SIGFILE" "$BINFILE"

echo "🔹 Voeg bestandsgrootte toe (little-endian)..."
# Cross-platform bestandsgrootte bepalen
if [[ "$OSTYPE" == "darwin"* ]]; then
    FILESIZE=$(stat -f%z "$BINFILE")
else
    FILESIZE=$(stat -c%s "$BINFILE")
fi

# Append 4-byte little-endian bestandsgrootte aan het .sig bestand
printf "$(printf '\\x%02x' $(($FILESIZE        & 0xFF)) \
                        $((($FILESIZE >> 8)  & 0xFF)) \
                        $((($FILESIZE >> 16) & 0xFF)) \
                        $((($FILESIZE >> 24) & 0xFF)))" >> "$SIGFILE"

echo "✅ Klaar! Bestandsinfo:"
ls -l "$SIGFILE"
