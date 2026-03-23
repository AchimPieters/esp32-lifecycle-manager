# Release checklist

- [ ] Build succeeds for every supported target (ESP32/S/C matrix).
- [ ] Unit tests/checks succeed.
- [ ] Signed artifacts generated (`main.bin` + `main.bin.sig`).
- [ ] Version bump verified.
- [ ] Partition compatibility confirmed.
- [ ] `nvs_keys` partition present and NVS encryption enabled (`CONFIG_NVS_ENCRYPTION=y`).
- [ ] OTA signature verification tested (valid + invalid + corrupt + wrong algorithm + wrong length).
