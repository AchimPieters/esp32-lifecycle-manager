# Release checklist

- [ ] Build succeeds for every supported target (ESP32/S/C matrix).
- [ ] Unit tests/checks succeed.
- [ ] Signed artifacts generated (`main.bin` + `main.bin.sig`).
- [ ] Signing key used for release is recorded (key id + activation window).
- [ ] Version bump verified.
- [ ] Partition compatibility confirmed.
- [ ] `nvs_keys` partition present and NVS encryption enabled (`CONFIG_NVS_ENCRYPTION=y`).
- [ ] Manufacturing provisioning flow validated with `scripts/provision_nvs_keys.sh` (generate + dry-run/flash path).
- [ ] OTA signature verification tested (valid + invalid + corrupt + wrong algorithm + wrong length).
- [ ] Security process reviewed against `docs/security/key-management.md`.
