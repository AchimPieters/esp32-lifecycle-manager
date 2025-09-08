# esp32-lifecycle-manager

## Signing firmware

Use `sign_and_upload_release.py` to sign the firmware binary located at
`build/main.bin`. The script writes the signature to `build/main.bin.sig` using
an ECDSA or RSA private key that matches the public key embedded in the device.

```bash
python3 sign_and_upload_release.py --key ota_private_key.pem
```

The generated signature is roughly 64–72 bytes for ECDSA or 256 bytes for RSA
keys.
