# LCM key files

- `official_public_key.h`: default trust anchor used by official mode builds.
- `custom_public_key.example.h`: template for advanced users.

## Custom mode

1. Copy `custom_public_key.example.h` to `custom_public_key.h`.
2. Replace the PEM body with your own ECDSA P-256 public key.
3. Enable `CONFIG_LCM_PUBLISHER_MODE_CUSTOM=y` in menuconfig/sdkconfig.

> Private keys are never required by the firmware build and must never be committed.
