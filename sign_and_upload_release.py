#!/usr/bin/env python3
"""Sign the firmware at ``build/main.bin`` and upload it plus its signature to a
GitHub release.

This script generates an ECDSA or RSA signature for the firmware binary located
in the ``build`` directory using a private key that matches the public key
embedded on the ESP32 device. The firmware and its signature are then uploaded
as assets to a GitHub release using the GitHub REST API.
"""
import argparse
import hashlib
import os
import sys
from pathlib import Path

import json
import urllib.error
import urllib.parse
import urllib.request
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


class _Response:
    def __init__(self, status_code: int, data: bytes, headers):
        self.status_code = status_code
        self.data = data
        self.headers = headers

    def json(self):
        return json.loads(self.data.decode())


def github_request(method: str, url: str, token: str, **kwargs):
    params = kwargs.pop('params', None)
    data = kwargs.pop('data', None)
    json_data = kwargs.pop('json', None)
    headers = kwargs.pop('headers', {})

    if params:
        url = f"{url}?{urllib.parse.urlencode(params)}"

    if json_data is not None:
        data = json.dumps(json_data).encode()
        headers.setdefault('Content-Type', 'application/json')

    headers['Authorization'] = f'token {token}'
    headers.setdefault('Accept', 'application/vnd.github+json')

    req = urllib.request.Request(url, data=data, headers=headers, method=method)

    try:
        with urllib.request.urlopen(req) as resp:
            return _Response(resp.status, resp.read(), resp.headers)
    except urllib.error.HTTPError as e:
        return _Response(e.code, e.read(), e.headers)


def get_or_create_release(owner: str, repo: str, tag: str, token: str) -> dict:
    url = f"https://api.github.com/repos/{owner}/{repo}/releases/tags/{tag}"
    r = github_request('GET', url, token)
    if r.status_code == 200:
        return r.json()
    if r.status_code != 404:
        raise RuntimeError(f"GitHub API error {r.status_code}: {r.data.decode()}")

    # Create release if it does not exist
    url = f"https://api.github.com/repos/{owner}/{repo}/releases"
    data = {"tag_name": tag, "name": tag, "draft": False}
    r = github_request('POST', url, token, json=data)
    if r.status_code >= 400:
        raise RuntimeError(f"GitHub API error {r.status_code}: {r.data.decode()}")
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
    p.add_argument('--key', required=True, type=Path, help='Path to private key (PEM)')
    p.add_argument('--token', default=os.getenv('GITHUB_TOKEN'), help='GitHub token')
    args = p.parse_args()

    if not args.token:
        print('GitHub token must be provided via --token or GITHUB_TOKEN env var', file=sys.stderr)
        return 1

    fw_path = Path('build/main.bin')
    if not fw_path.exists():
        print('Firmware binary build/main.bin not found', file=sys.stderr)
        return 1

    signature = sign_firmware(fw_path, args.key)
    sig_path = fw_path.with_suffix(fw_path.suffix + '.sig')
    with sig_path.open('wb') as f:
        f.write(signature)

    release = get_or_create_release(args.owner, args.repo, args.tag, args.token)
    upload_url = release['upload_url']
    upload_asset(upload_url, fw_path, args.token)
    upload_asset(upload_url, sig_path, args.token)
    print(f"Uploaded {fw_path.name} and {sig_path.name} to release {args.tag}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
