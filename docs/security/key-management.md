# Key management & provisioning

This document defines the minimum operational process for OTA signing keys and NVS encryption provisioning.

## OTA signing key lifecycle (ECDSA P-256)

### Demo key behavior for local onboarding
- This repository ships demo keys in `keys/` for out-of-the-box evaluation.
- `generate_sig.sh` uses `keys/ota_signing_private.pem` by default.
- Firmware verification uses a built-in demo public key unless overridden via config.
- Never use demo keys for production deployments.


### 1) Key generation
- Generate the private key on an offline workstation/HSM.
- Keep private keys outside this repository.
- Export only the public key material required by firmware verification.

### 2) Key storage
- Store active private keys in a dedicated secret manager or HSM-backed store.
- Restrict access to release automation identities and approved operators.
- Enable audit logging for every key access event.

### 3) Rotation policy
- Rotate signing keys at least every 12 months, or immediately on incident/suspicion.
- Support overlap windows where old and new public keys are accepted (staged rollout).
- Record key id, activation date, deprecation date, and owner.

### 4) Release signing controls
- Every release artifact must be signed using `generate_sig.sh`.
- CI must verify `.sig` artifact presence and reject unsigned releases.
- A release must include traceability metadata linking firmware version to signing key id.

### 5) Revocation response
- If compromise is suspected:
  - freeze releases,
  - rotate signing key,
  - ship a firmware update with updated trust anchors,
  - invalidate old key in operational runbooks.

## NVS encryption provisioning flow

Use `scripts/provision_nvs_keys.sh` to standardize per-device key generation and optional flashing.

Baseline flow per device:
1. Generate unique NVS keys partition binary.
2. Flash `nvs_keys` partition to the target offset from `partitions.csv`.
3. Flash firmware image.
4. Verify NVS encryption is enabled at runtime (`CONFIG_NVS_ENCRYPTION=y` and runtime guard if enabled).
5. Record provisioning audit metadata (device id, key hash, timestamp, job context).

## Manufacturing checklist

- Unique NVS keys generated per device (no shared key binary reused).
- `nvs_keys` partition flashed before final functional test.
- Device serial/lot linked to provisioning job id and audit record.
- Provisioning logs retained for audit/recovery.
- Failed provisioning attempts quarantined and not shipped.

### Scripted audit support

`scripts/provision_nvs_keys.sh` supports:
- `--device-id <id>` for device traceability.
- `--audit-log <path>` to append JSONL records with key SHA-256 and timestamp.
