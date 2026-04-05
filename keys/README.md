# LCM key files

- `official_public_key.h`: default trust anchor used by official mode builds.
- `custom_public_key.example.h`: template for advanced users.

## Custom mode

1. Copy `custom_public_key.example.h` to `custom_public_key.h`.
2. Replace the PEM body with your own ECDSA P-256 public key.
3. Enable `CONFIG_LCM_PUBLISHER_MODE_CUSTOM=y` in menuconfig/sdkconfig.
4. Sign firmware with your matching private key, for example:
   `./scripts/sign_firmware.sh build/main.bin keys/private/publisher_private.pem esp32c3 --custom`

## Official mode signing

- Standard LCM builds trust `official_public_key.h`.
- Sign release firmware with the matching private key (kept outside git):
  `LCM_OFFICIAL_SIGNING_KEY=/secure/path/official_private.pem ./scripts/sign_firmware.sh build/main.bin "" esp32c3 --official`
- Easy publisher flow: copy `scripts/sign.py` into the app build directory and run:
  `python3 sign.py`

> Private keys are never required by the firmware build and must never be committed.
