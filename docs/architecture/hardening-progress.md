# Hardening progress audit (2026-03-22)

## Scope
This audit maps the requested hardening program to the current repository state and identifies remaining work.

## Completed (verified in repository)
- **Fase 0 baseline document present**: `docs/architecture/current-state.md` includes boot, provisioning, OTA, reset flows, NVS keys, partitions and failures.
- **Real firmware signing path implemented (P0)**:
  - `generate_sig.sh` now creates an ECDSA P-256 signature blob with versioned metadata.
  - `main/github_update.c` validates magic/version/algorithm/length/hash and verifies asymmetric signature before activation.
- **Central factory reset flow (P0)**:
  - `lifecycle_factory_reset_execute()` is present in `main/lifecycle_reset.c`.
  - Both button-trigger and powercycle-trigger route to this flow from `main/main.c`.
- **Factory reset cleanup breadth (P0)**:
  - Flow performs `esp_wifi_restore()`, NVS erase/reinit, `otadata` erase, and OTA partition erase.
- **Powercycle robustness improvements (P0)**:
  - Timeout uses `CONFIG_LCM_RESTART_COUNTER_TIMEOUT_MS` when available, with a safe fallback default of 10000ms.
  - Boot logs include reset reason and powercycle count.
- **Captive portal size limit (P0)**:
  - `WIFI_CONFIG_HTTP_MAX_BODY_SIZE` (4KB) enforced in HTTP body parser.
- **CI skeleton present**:
  - `.github/workflows/ci.yml` runs Python tests, signature artifact check, and ESP-IDF cross-target builds.

## Completed in this change set
- Hardened critical Wi-Fi API wrappers to check and log return values instead of ignoring failures.
- Replaced hard-coded `"wifi_cfg"` namespace literal with `NVS_NS_WIFI_CFG`.
- Fixed custom HTML response allocation to avoid 8-bit size overflow/truncation risk.
- Added explicit LED form input-length guards and centralized SSID/password max constants.
- Added GPIO operation error checks in `main/main.c` to avoid silent hardware failures.
- Added an explicit OTA state model (`IDLE`, `CHECKING_RELEASE`, `DOWNLOADING`, `VERIFYING`, `STAGING`, `ACTIVATING`, `FAILED`, `REBOOTING`) with persisted telemetry (last error/time/version/repo) in `fwcfg` NVS.
- Added reusable `nvs_store` helpers and started migrating OTA telemetry writes to the shared helper API.
- Migrated restart-counter NVS reads/writes onto `nvs_store` helper APIs to reduce duplicate direct NVS handling code.
- Added runtime hardware guard that enforces minimum flash size of 4MB before lifecycle startup continues.
- Added explicit OTA failure-reason persistence (`release_api_failure`, `missing_asset`, `invalid_signature`, `invalid_image_length`, `partition_unavailable`, `boot_partition_set_failure`, `http_failure`).
- Added runtime security guard to require NVS encryption (`CONFIG_NVS_ENCRYPTION`) when `CONFIG_LCM_REQUIRE_NVS_ENCRYPTION` is enabled.
- Split powercycle restart-counter logic out of `main.c` into `lifecycle_restart_counter.c` for clearer lifecycle ownership.

## Remaining high-priority gaps
- **Signature algorithm hardening choice**:
  - ECDSA is implemented; key management, rotation policy, and secure key provisioning process are not yet documented/automated.
- **NVS encryption rollout**:
  - Runtime enforcement is implemented; remaining work is rollout tooling for provisioning key partitions in manufacturing.
- **OTA state machine explicitness**:
  - State machine persistence is now implemented; remaining work is validating transitions and recovery behavior on target hardware.
- **NVS helper API centralization**:
  - Helper abstraction now exists, but migration is not complete across all modules yet.
- **Architecture modularization depth**:
  - Restart-counter module split is done; boot and LED responsibilities still need further decomposition.
- **Expanded automated tests**:
  - Additional targeted tests for version compare, reset window logic, and partition selection should be added.

## Recommended execution order (next iterations)
1. Add NVS helper layer + migrate all namespaces/keys through it.
2. Implement NVS encryption path with documented provisioning requirements.
3. Split lifecycle logic from `main.c` into dedicated modules while preserving behavior.
4. Extend tests for reset window logic and OTA partition/failure classification.
5. Add on-device integration tests for OTA-state persistence and reboot/rollback outcomes.
