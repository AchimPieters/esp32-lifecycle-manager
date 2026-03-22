# Support matrix

## Supported chip families
- ESP32 series: `esp32`
- ESP32-S series: `esp32s2`, `esp32s3`
- ESP32-C series: `esp32c2`, `esp32c3`, `esp32c5`, `esp32c6`, `esp32c61`

## Explicitly not supported
- ESP32-H series (not targeted in this repository)
- Any target not listed in `idf_component.yml`

## Requirements
- Flash layout compatible with `partitions.csv` (factory + 2 OTA slots + otadata + nvs).
- TLS root bundle support required for GitHub HTTPS.
- OTA signature verification requires embedded ECDSA public key support in mbedTLS.

## Compatibility policy
- Semantic versioning for firmware tags (`MAJOR.MINOR.PATCH`).
- OTA upgrades must preserve partition compatibility and signature format version.
- A target is considered supported only if CI cross-target build checks pass.
