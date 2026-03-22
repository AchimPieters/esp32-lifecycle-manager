# Threat model

## Assets
- Firmware integrity and authenticity.
- Wi-Fi credentials and provisioning config.
- Boot partition state.

## Main threats
- Malicious firmware replacement via tampered OTA payload/signature.
- Captive portal abuse via oversized payloads or malformed input.
- Credential exfiltration from weak storage handling.

## Mitigations
- ECDSA-signed firmware manifest validated on-device with embedded public key.
- OTA verification fails closed: no boot partition switch, no reboot.
- HTTP body size limits, strict repo format validation, and rate limiting.
- Centralized NVS key management to avoid accidental secret sprawl.
