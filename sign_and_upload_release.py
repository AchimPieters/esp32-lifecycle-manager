#!/usr/bin/env python3
"""Sign ``build/main.bin`` and emit the signature path.

This utility searches for ``build/main.bin`` and, when found, generates a
``build/main.bin.sig`` using a private key. The key is read from the ``--key``
argument or the ``OTA_PRIVATE_KEY`` environment variable. If neither is
provided, a ``private_key.pem`` in the current directory is used, generating
one if necessary. Upon success a JSON object describing the signature file is
written to stdout, which can be consumed by GitHub Actions or the GitHub API.
"""
import argparse
import hashlib
import json
import os
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
    p.add_argument('--key', type=Path, help='Path to private key (PEM)')
    args = p.parse_args()

    key_path = args.key or os.environ.get('OTA_PRIVATE_KEY')
    if key_path:
        key_path = Path(key_path)
        if not key_path.exists():
            print(f'Private key {key_path} not found', file=sys.stderr)
            return 1
    else:
        key_path = Path('private_key.pem')
        if not key_path.exists():
            key = ec.generate_private_key(ec.SECP256R1())
            with key_path.open('wb') as f:
                f.write(
                    key.private_bytes(
                        encoding=serialization.Encoding.PEM,
                        format=serialization.PrivateFormat.TraditionalOpenSSL,
                        encryption_algorithm=serialization.NoEncryption(),
                    )
                )
            print(f'Generated new private key at {key_path}', file=sys.stderr)

    fw_path = Path('build/main.bin')
    if not fw_path.exists():
        print('Firmware binary build/main.bin not found', file=sys.stderr)
        return 1

    signature = sign_firmware(fw_path, key_path)
    sig_path = fw_path.with_suffix(fw_path.suffix + '.sig')
    with sig_path.open('wb') as f:
        f.write(signature)

    print(json.dumps({'signature_path': str(sig_path)}))
    return 0


if __name__ == '__main__':
    sys.exit(main())
