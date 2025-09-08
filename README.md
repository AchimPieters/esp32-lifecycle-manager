# esp32-lifecycle-manager

## Signing and uploading firmware

Use `sign_and_upload_release.py` to create a release on GitHub and upload a
firmware image along with its signature. The script signs the firmware using
an ECDSA or RSA private key that matches the public key embedded in the
device.

```bash
python3 sign_and_upload_release.py \
    --owner <github-user> \
    --repo <repository> \
    --tag v1.0.0 \
    --firmware build/main.bin \
    --key ota_private_key.pem
```

The script generates `build/main.bin.sig` (~64–72 bytes for ECDSA or 256 bytes
for RSA) and uploads both files to the release specified by `--tag`.
