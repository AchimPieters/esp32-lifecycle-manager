#!/usr/bin/env python3
"""Sign the firmware at ``build/main.bin`` and generate ``build/main.bin.sig``.

This script generates an ECDSA or RSA signature for the firmware binary located
in the ``build`` directory using a private key that matches the public key
embedded on the ESP32 device. The resulting signature is written next to the
firmware image.
"""
import argparse
import hashlib
import sys
from pathlib import Path

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, padding, rsa
from cryptography.hazmat.primitives import serialization


def load_private_key(path: Path):
    with path.open('rb') as f:
        data = f.read()
    return serialization.load_pem_private_key(data, password=None)


def sign_firmware(fw_path: Path, key_path: Path) -> bytes:
    """Return raw binary signature of the firmware image."""
    key = load_private_key(key_path)

    with fw_path.open('rb') as f:
        firmware = f.read()

    digest = hashlib.sha256(firmware).digest()

    if isinstance(key, rsa.RSAPrivateKey):
        signature = key.sign(
            digest,
            padding.PKCS1v15(),
            hashes.SHA256(),
        )
    elif isinstance(key, ec.EllipticCurvePrivateKey):
        signature = key.sign(digest, ec.ECDSA(hashes.SHA256()))
    else:
        raise ValueError("Unsupported key type")

    sig_len = len(signature)
    if sig_len not in (256,) and not (64 <= sig_len <= 72):
        raise ValueError(
            f"Unexpected signature length {sig_len} bytes; expected 64–72 or 256"
        )
    return signature


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--key', required=True, type=Path, help='Path to private key (PEM)')
    args = p.parse_args()

    fw_path = Path('build/main.bin')
    if not fw_path.exists():
        print('Firmware binary build/main.bin not found', file=sys.stderr)
        return 1

    signature = sign_firmware(fw_path, args.key)
    sig_path = fw_path.with_suffix(fw_path.suffix + '.sig')
    with sig_path.open('wb') as f:
        f.write(signature)

    print(f"Generated {sig_path} from {fw_path}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
