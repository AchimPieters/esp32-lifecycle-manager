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
- ECDSA P-256 / SHA-256 signed firmware validated on-device against the
  configured public key.
- OTA verification fails closed: the image is streamed to the inactive OTA
  partition and the boot partition is only switched (via
  `esp_https_ota_finish`) after the signature verifies. A failed/aborted update
  leaves the running firmware as the boot target (no boot switch, no reboot).
- The firmware builds with the bundled demo key by default for out-of-the-box
  evaluation, but logs a runtime warning whenever that key is in use (its
  private key is published in this repo, so it provides no authenticity). Real
  devices must set `LCM_USE_CUSTOM_OTA_PUBLIC_KEY=y` with their own key.
- Lightweight anti-rollback floor in NVS refuses releases below the highest
  version ever installed. Note: this floor is cleared by a factory reset;
  hardware anti-rollback (Secure Boot v2 secure_version) is required for a
  reset-proof guarantee.
- Scanned SSIDs are HTML-escaped before rendering in the captive portal to
  prevent script injection (XSS) from a hostile nearby access point.
- HTTP body size limits, strict repo format validation, and rate limiting.
- URL decoding and the DNS responder are bounds-checked against malformed,
  attacker-supplied input.
- Centralized NVS key management to avoid accidental secret sprawl.

## Known residual risks
- The provisioning access point is open and the captive portal uses plain HTTP,
  so Wi-Fi credentials can be observed over the air during provisioning. NVS is
  not encrypted by default, so stored credentials are readable with physical
  flash access (enable `LCM_REQUIRE_NVS_ENCRYPTION` + `CONFIG_NVS_ENCRYPTION`
  and provision per-device keys for production).
- SNTP is unauthenticated; TLS validity depends on the device clock.
