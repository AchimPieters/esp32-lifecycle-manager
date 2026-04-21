# Code audit — 2026-04-06

## Scope

Manual, source-level audit of the ESP32 Lifecycle Manager core firmware components:

- `main/wifi_config.c`
- `main/github_update.c`
- supporting NVS abstractions

Focus areas: input validation, memory safety, secret handling, and OTA/network robustness.

## Findings

### 1) Plaintext Wi-Fi password is written to logs (High)

**Where**
- `main/wifi_config.c` logs the submitted password value directly.

**Why this matters**
- Wi-Fi credentials become readable in serial logs, debug captures, and any centralized log sink.
- This is credential disclosure and can materially weaken device and network security.

**Evidence**
- `DEBUG("Setting wifi_password param = %s", password_param ? password_param->value : "(none)");`

**Recommendation**
- Never log secrets. Replace with masked/boolean logging, e.g. `"(provided)"` vs `"(none)"`.
- Treat SSID as potentially sensitive in some deployments and avoid printing full values in production logs.

---

### 2) `fd_set` used without `FD_ZERO` initialization (Medium)

**Where**
- `main/wifi_config.c` HTTP server setup path.

**Why this matters**
- `fd_set fds;` is stack-allocated and then immediately used with `FD_SET`/`select` without being zeroed.
- This is undefined behavior and can cause intermittent misbehavior (spurious descriptors, unstable select loop, or hard-to-reproduce failures).

**Evidence**
- `fd_set fds;`
- `FD_SET(listenfd, &fds);`

**Recommendation**
- Add `FD_ZERO(&fds);` before first `FD_SET`.
- Keep the existing pattern of copying into `read_fds` per iteration.

---

### 3) OTA release JSON allocation trusts remote-reported length without explicit upper bound (Medium)

**Where**
- `main/github_update.c` in both primary and fallback release-fetch paths.

**Why this matters**
- The code allocates `malloc(len + 1)` where `len` comes from `esp_http_client_fetch_headers` (effectively content length metadata).
- A very large or malformed response length can trigger memory pressure or allocation failure loops (DoS behavior), especially on constrained ESP32 devices.

**Evidence**
- `int len = esp_http_client_fetch_headers(client);`
- `char *data = malloc(len + 1);`

**Recommendation**
- Enforce a strict maximum JSON size (for example 16 KiB–64 KiB depending on target memory budget).
- Reject responses above that cap and record OTA failure reason.
- Optionally stream/process JSON incrementally to avoid single large allocations.

---

## Positive notes

- HTTP request body size in captive portal is bounded by `WIFI_CONFIG_HTTP_MAX_BODY_SIZE` before reallocation growth.
- OTA flow validates signed image metadata and cryptographic signature before activation.
- Input lengths for SSID/password/LED parameters are constrained.

## Risk summary

- **High**: 1
- **Medium**: 2
- **Low**: 0

## Suggested remediation order

1. Remove secret logging immediately (finding 1).
2. Initialize `fd_set` with `FD_ZERO` (finding 2).
3. Cap OTA JSON response size and harden read strategy (finding 3).
