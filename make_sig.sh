#!/bin/bash
# Maak een geldige main.bin.sig (52 bytes) voor ESP32 OTA
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

echo "🔹 Voeg bestandsgrootte toe..."
# Cross-platform bestandsgrootte bepalen
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS
    FILESIZE=$(stat -f%z "$BINFILE")
else
    # Linux
    FILESIZE=$(stat -c%s "$BINFILE")
fi

# 4 bytes bestandsgrootte (big endian) toevoegen
printf "%08x" "$FILESIZE" | xxd -r -p >> "$SIGFILE"

echo "✅ Klaar! Bestandsinfo:"
ls -l "$SIGFILE"
