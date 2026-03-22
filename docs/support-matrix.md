# Support matrix

## Supported chip families
- ESP32 series: `esp32`
- ESP32-S series: `esp32s2`, `esp32s3`
- ESP32-C series: `esp32c2`, `esp32c3`

## Explicitly not supported
- ESP32-H series (not targeted in this repository)
- ESP32-C5 / ESP32-C6 / ESP32-C61 (excluded from current CI support scope due app-partition size constraints with current partition profile)
- Any target not listed in `idf_component.yml`

## Requirements
- Flash layout compatible with `partitions.csv` (factory + 2 OTA slots + otadata + nvs).
- TLS root bundle support required for GitHub HTTPS.
- OTA signature verification requires embedded ECDSA public key support in mbedTLS.

## Compatibility policy
- Semantic versioning for firmware tags (`MAJOR.MINOR.PATCH`).
- OTA upgrades must preserve partition compatibility and signature format version.
- A target is considered supported only if CI cross-target build checks pass.
