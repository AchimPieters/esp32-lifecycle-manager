#!/usr/bin/env python3
"""Sign a firmware image and upload it plus its signature to a GitHub release.

This script generates an ECDSA or RSA signature for a given firmware binary
using a private key that matches the public key embedded on the ESP32 device.
The firmware and its signature are then uploaded as assets to a GitHub release
using the GitHub REST API.
"""
import argparse
import hashlib
import os
import sys
from pathlib import Path

import requests
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


def github_request(method: str, url: str, token: str, **kwargs):
    headers = kwargs.pop('headers', {})
    headers['Authorization'] = f'token {token}'
    headers.setdefault('Accept', 'application/vnd.github+json')
    response = requests.request(method, url, headers=headers, **kwargs)
    if response.status_code >= 400:
        raise RuntimeError(f"GitHub API error {response.status_code}: {response.text}")
    return response


def get_or_create_release(owner: str, repo: str, tag: str, token: str) -> dict:
    headers = {
        'Authorization': f'token {token}',
        'Accept': 'application/vnd.github+json',
    }
    url = f"https://api.github.com/repos/{owner}/{repo}/releases/tags/{tag}"
    r = requests.get(url, headers=headers)
    if r.status_code == 200:
        return r.json()
    if r.status_code != 404:
        raise RuntimeError(f"GitHub API error {r.status_code}: {r.text}")

    # Create release if it does not exist
    url = f"https://api.github.com/repos/{owner}/{repo}/releases"
    data = {"tag_name": tag, "name": tag, "draft": False}
    r = requests.post(url, headers=headers, json=data)
    if r.status_code >= 400:
        raise RuntimeError(f"GitHub API error {r.status_code}: {r.text}")
    return r.json()


def upload_asset(upload_url: str, file_path: Path, token: str):
    upload_url = upload_url.split('{')[0]
    params = {'name': file_path.name}
    with file_path.open('rb') as f:
        data = f.read()
    headers = {'Content-Type': 'application/octet-stream'}
    github_request('POST', upload_url, token, params=params, data=data, headers=headers)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--owner', required=True, help='GitHub repository owner')
    p.add_argument('--repo', required=True, help='GitHub repository name')
    p.add_argument('--tag', required=True, help='Release tag to create/update')
    p.add_argument('--firmware', required=True, type=Path, help='Path to firmware binary')
    p.add_argument('--key', required=True, type=Path, help='Path to private key (PEM)')
    p.add_argument('--token', default=os.getenv('GITHUB_TOKEN'), help='GitHub token')
    args = p.parse_args()

    if not args.token:
        print('GitHub token must be provided via --token or GITHUB_TOKEN env var', file=sys.stderr)
        return 1

    signature = sign_firmware(args.firmware, args.key)
    sig_path = args.firmware.with_suffix(args.firmware.suffix + '.sig')
    with sig_path.open('wb') as f:
        f.write(signature)

    release = get_or_create_release(args.owner, args.repo, args.tag, args.token)
    upload_url = release['upload_url']
    upload_asset(upload_url, args.firmware, args.token)
    upload_asset(upload_url, sig_path, args.token)
    print(f"Uploaded {args.firmware.name} and {sig_path.name} to release {args.tag}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
