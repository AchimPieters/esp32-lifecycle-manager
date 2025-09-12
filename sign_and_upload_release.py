#!/usr/bin/env python3
"""Sign ``build/main.bin`` and emit the signature path.

This utility searches for ``build/main.bin`` and, when found, generates a
``build/main.bin.sig`` using a private key. The key is read from the ``--key``
argument or the ``OTA_PRIVATE_KEY`` environment variable. ``OTA_PRIVATE_KEY``
may contain either a filesystem path or the PEM-encoded key itself. If neither
is provided, ``private_key.pem`` in the current directory is used. The key must
already exist – one will not be generated automatically – to prevent signing
with a key that doesn't match the device firmware. The signature is verified
against a public key loaded from ``--pubkey``, ``ota_pubkey.pem`` or
``main/ota_pubkey.c``; if none are found a built-in default is used. Upon
success a JSON object describing the signature file is written to stdout, which
can be consumed by GitHub Actions or the GitHub API.
"""
import argparse
import hashlib
import json
import os
import sys
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, padding, rsa, utils
from cryptography.hazmat.primitives import serialization


DEFAULT_PUBLIC_KEY_PEM = (
    b"-----BEGIN PUBLIC KEY-----\n"
    b"MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEdvNFFIe+YXpUxiwFYlWAy3M3t6Sa\n"
    b"BP6750XmINFU950HVj8YfJIa/ILfYQKMxiCrhiyzcz09kkRKY8iW8zrfhQ==\n"
    b"-----END PUBLIC KEY-----\n"
)


def load_private_key(path: Path):
    with path.open('rb') as f:
        data = f.read()
    return serialization.load_pem_private_key(data, password=None)


def load_public_key(path: Path):
    with path.open('rb') as f:
        data = f.read()
    return serialization.load_pem_public_key(data)


def extract_public_key_from_c(path: Path) -> bytes:
    """Return PEM bytes embedded in a C source file."""
    lines: list[str] = []
    with path.open('r') as f:
        for line in f:
            line = line.strip()
            if not line.startswith('"'):
                continue
            line = line[1:]  # drop leading quote
            if line.endswith('";'):
                line = line[:-2]
            elif line.endswith('"'):
                line = line[:-1]
            lines.append(line.encode().decode('unicode_escape'))
    return ''.join(lines).encode()

def sign_firmware(fw_path: Path, key) -> tuple[bytes, bytes]:
    """Return (digest, raw signature) of the firmware image."""

    with fw_path.open('rb') as f:
        firmware = f.read()

    digest = hashlib.sha256(firmware).digest()

    if isinstance(key, rsa.RSAPrivateKey):
        signature = key.sign(
            digest,
            padding.PKCS1v15(),
            utils.Prehashed(hashes.SHA256()),
        )
    elif isinstance(key, ec.EllipticCurvePrivateKey):
        signature = key.sign(digest, ec.ECDSA(utils.Prehashed(hashes.SHA256())))
    else:
        raise ValueError("Unsupported key type")

    sig_len = len(signature)
    if sig_len not in (256,) and not (64 <= sig_len <= 72):
        raise ValueError(
            f"Unexpected signature length {sig_len} bytes; expected 64–72 or 256"
        )
    return digest, signature


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--key', type=Path, help='Path to private key (PEM)')
    p.add_argument('--pubkey', type=Path, help='Path to public key (PEM) for verification')
    args = p.parse_args()

    key: ec.EllipticCurvePrivateKey | rsa.RSAPrivateKey | None = None
    if args.key:
        if not args.key.exists():
            print(f"Private key {args.key} not found", file=sys.stderr)
            return 1
        key = load_private_key(args.key)
    else:
        key_env = os.environ.get('OTA_PRIVATE_KEY')
        if key_env:
            env_path = Path(key_env)
            if env_path.exists():
                key = load_private_key(env_path)
            else:
                try:
                    key = serialization.load_pem_private_key(key_env.encode(), password=None)
                except Exception:
                    print('Failed to parse private key from OTA_PRIVATE_KEY', file=sys.stderr)
                    return 1
        else:
            default_path = Path('private_key.pem')
            if not default_path.exists():
                print(
                    "Private key private_key.pem not found. Provide a key with --key or set "
                    "the OTA_PRIVATE_KEY environment variable.",
                    file=sys.stderr,
                )
                return 1
            key = load_private_key(default_path)

    pubkey = None
    if args.pubkey:
        if not args.pubkey.exists():
            print(f"Public key {args.pubkey} not found", file=sys.stderr)
            return 1
        pubkey = load_public_key(args.pubkey)
    else:
        pubkey_env = os.environ.get('OTA_PUBLIC_KEY')
        if pubkey_env:
            env_path = Path(pubkey_env)
            if env_path.exists():
                pubkey = load_public_key(env_path)
            else:
                try:
                    pubkey = serialization.load_pem_public_key(pubkey_env.encode())
                except Exception:
                    print('Failed to parse public key from OTA_PUBLIC_KEY', file=sys.stderr)
                    return 1
        else:
            candidate = Path('ota_pubkey.pem')
            if candidate.exists():
                pubkey = load_public_key(candidate)
            else:
                ota_c = Path('main/ota_pubkey.c')
                if ota_c.exists():
                    try:
                        pem_data = extract_public_key_from_c(ota_c)
                        pubkey = serialization.load_pem_public_key(pem_data)
                    except Exception as e:
                        print(f'Failed to parse {ota_c}: {e}', file=sys.stderr)
                        return 1
                else:
                    try:
                        pubkey = serialization.load_pem_public_key(DEFAULT_PUBLIC_KEY_PEM)
                    except Exception as e:
                        print(f'Failed to load built-in public key: {e}', file=sys.stderr)
                        return 1
    if pubkey is None:
        print('Public key not found; provide --pubkey or ota_pubkey.pem', file=sys.stderr)
        return 1

    fw_path = Path('build/main.bin')
    if not fw_path.exists():
        print('Firmware binary build/main.bin not found', file=sys.stderr)
        return 1

    digest, signature = sign_firmware(fw_path, key)

    sig_path = fw_path.with_suffix(fw_path.suffix + '.sig')
    with sig_path.open('wb') as f:
        f.write(signature)

    derived = key.public_key().public_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PublicFormat.SubjectPublicKeyInfo,
    )
    expected = pubkey.public_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PublicFormat.SubjectPublicKeyInfo,
    )
    if derived != expected:
        print('Private key does not match provided public key', file=sys.stderr)
        return 1
    try:
        if isinstance(pubkey, rsa.RSAPublicKey):
            pubkey.verify(
                signature,
                digest,
                padding.PKCS1v15(),
                utils.Prehashed(hashes.SHA256()),
            )
        else:
            pubkey.verify(
                signature,
                digest,
                ec.ECDSA(utils.Prehashed(hashes.SHA256())),
            )
    except InvalidSignature:
        print('Signature verification failed', file=sys.stderr)
        return 1

    print(json.dumps({'signature_path': str(sig_path)}))
    return 0


if __name__ == '__main__':
    sys.exit(main())
