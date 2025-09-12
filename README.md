# esp32-lifecycle-manager

## Signing firmware

Use `sign_and_upload_release.py` to sign the firmware binary located at
`build/main.bin`. The script writes the signature to `build/main.bin.sig` using
an ECDSA or RSA private key that matches the public key embedded in the device.
Specify the key with `--key` or the `OTA_PRIVATE_KEY` environment variable.
`OTA_PRIVATE_KEY` accepts either a path to the PEM file or the PEM data itself.
If neither is supplied, `private_key.pem` in the current directory is used.

```bash
python3 sign_and_upload_release.py --key ota_private_key.pem
```

The generated signature is roughly 64–72 bytes for ECDSA or 256 bytes for RSA
keys.

### Generating a private key

If you do not yet have a signing key, create one before running the script.
`espsecure.py` from ESP-IDF can generate an ECDSA key suitable for signing:

```bash
espsecure.py generate_signing_key --version 2 private_key.pem
```

Alternatively, OpenSSL can create ECDSA or RSA keys:

```bash
# ECDSA P-256
openssl ecparam -genkey -name prime256v1 -noout -out private_key.pem

# RSA 2048-bit
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out private_key.pem
```

Keep the private key secure and ensure the corresponding public key is
embedded in your firmware.
