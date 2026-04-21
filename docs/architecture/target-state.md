# Target state architecture

- Explicit lifecycle modules with clear ownership: boot, reset, OTA, provisioning, LED.
- Single factory reset path for all triggers with deterministic cleanup ordering.
- Cryptographic OTA authenticity verification as mandatory gate before activation.
- Strong input validation and rate limiting for provisioning endpoints.
- Centralized NVS key catalog and helper APIs.
- Observable OTA state transitions with persistent result metadata.
- Explicit OTA failure-reason taxonomy persisted in NVS (API failure, missing asset, invalid signature, invalid image length, partition/boot activation failures).
