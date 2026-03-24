# Support matrix

## Supported chip families
- ESP32 series: `esp32` (minimum flash: 4MB)
- ESP32-E series: mapped to ESP32 target compatibility profile (`esp32`) (minimum flash: 4MB)
- ESP32-S series: `esp32s2`, `esp32s3` (minimum flash: 4MB)
- ESP32-C series: `esp32c2`, `esp32c3`, `esp32c6` (minimum flash: 4MB)

## Explicitly not supported
- ESP32-H series (not targeted in this repository)
- ESP32-C5 / ESP32-C61 (not currently targeted in this repository)
- Any target not listed in `idf_component.yml`

## Requirements
- Flash size must be **4MB or higher** (enforced at runtime by startup check).
- Flash layout compatible with `partitions.csv` (factory + 2 OTA slots + otadata + nvs).
- NVS encryption is required by default (`CONFIG_LCM_REQUIRE_NVS_ENCRYPTION=y` + `CONFIG_NVS_ENCRYPTION=y`).
- Partition table must include an `nvs_keys` data partition for encrypted NVS key material.
- TLS root bundle support required for GitHub HTTPS.
- OTA signature verification requires embedded ECDSA public key support in mbedTLS.

## Compatibility policy
- Semantic versioning for firmware tags (`MAJOR.MINOR.PATCH`).
- OTA upgrades must preserve partition compatibility and signature format version.
- A target is considered supported only if CI cross-target build checks pass.
- Hardware-level behavior is guaranteed only after on-device validation for that exact board/flash variant.
