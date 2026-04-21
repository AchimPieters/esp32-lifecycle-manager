# Current state architecture baseline

## Boot sequence
1. `app_main()` initializes NVS and loads the powercycle restart counter.
2. Powercycle flow evaluates `esp_reset_reason()` and restart counter threshold.
3. If threshold is reached, factory reset flow is started.
4. LED config is loaded from NVS and Wi-Fi config service is started.
5. After Wi-Fi gets IP, SNTP sync is performed and OTA check is started.

## Wi-Fi provisioning flow
1. Device starts in STA mode and attempts to connect with saved credentials (`wifi_cfg` namespace).
2. If no credentials or connect fails, device enables AP+STA mode and starts DNS + HTTP captive portal.
3. User submits `/settings` form with SSID/password/repo/LED config.
4. Values are persisted to NVS and STA connect is retried.

## OTA update flow
1. OTA checks GitHub release API (`/latest` or release list).
2. Firmware asset (`main.bin`) and signature asset (`main.bin.sig`) are located.
3. OTA image is downloaded to the next OTA partition.
4. Downloaded image metadata and SHA-256 hash are computed.
5. Signature file structure is parsed and ECDSA P-256 signature is verified with embedded public key.
6. On success, installed version metadata is stored and device reboots.

## Powercycle factory reset flow
1. Reset reason + restart counter are logged on every boot.
2. Counter increments for `ESP_RST_POWERON`, `ESP_RST_EXT`, and `ESP_RST_SW`.
3. Counter timeout is 10 seconds.
4. If counter reaches 10-12 window, factory reset is triggered.

## Button factory reset flow
1. Button trigger calls `factory_reset()`.
2. `factory_reset_task` calls `lifecycle_factory_reset_execute()`.
3. Cleanup steps are identical to powercycle trigger.

## NVS namespaces + keys
- `lcm`
  - `restart_count`
  - `do_update`
- `fwcfg`
  - `repo`, `pre`
  - `installed_ver`, `installed_part`
  - `led_en`, `led_gpio`, `led_lvl`
- `wifi_cfg`
  - `wifi_ssid`, `wifi_password`

## Partition layout
From `partitions.csv`:
- `nvs` data/nvs at `0x9000`, size `0x5000`
- `otadata` data/ota at `0xe000`, size `0x2000`
- `nvs_keys` data/nvs_keys at `0x10000`, size `0x1000` (encrypted key partition)
- `phy_init` data/phy at `0x11000`, size `0x1000`
- `factory` app/factory at `0x20000`, size `1M`
- `ota_0` app/ota_0 at `0x120000`, size `1M`
- `ota_1` app/ota_1 at `0x220000`, size `1M`

## Failure cases
- NVS init/open/read/write can fail and is logged.
- OTA API failure, missing assets, download failures, invalid signature, partition selection errors fail closed.
- Factory reset aborts on critical cleanup error and logs failure.
- HTTP provisioning rejects oversized body, invalid repo, invalid credential lengths, and rate-limited updates.
