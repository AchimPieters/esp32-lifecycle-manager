#!/usr/bin/env python3
"""
Simple firmware signer for publishers.

Designed to be copied into an app build directory and run as:
  python3 sign.py

Default behavior:
  - input firmware: ./main.bin
  - output signature: ./main.bin.sig
  - mode: official
  - target: auto-detect from ../sdkconfig, else esp32
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


TARGET_IDS = {
    "esp32": 1,
    "esp32c2": 2,
    "esp32c3": 3,
    "esp32s2": 4,
    "esp32s3": 5,
    "esp32c5": 6,
    "esp32c6": 7,
}


def detect_target(sdkconfig_path: Path) -> str | None:
    if not sdkconfig_path.is_file():
        return None
    for line in sdkconfig_path.read_text(encoding="utf-8", errors="ignore").splitlines():
        if line.startswith("CONFIG_IDF_TARGET="):
            raw = line.split("=", 1)[1].strip()
            return raw.strip('"')
    return None


def resolve_private_key(mode: str, explicit_key: str | None) -> Path:
    if explicit_key:
        return Path(explicit_key)
    if mode == "official":
        env_key = os.environ.get("LCM_OFFICIAL_SIGNING_KEY", "").strip()
        if not env_key:
            raise SystemExit(
                "Official mode requires LCM_OFFICIAL_SIGNING_KEY (path to official private key)."
            )
        return Path(env_key)
    return Path("keys/private/publisher_private.pem")


def main() -> int:
    parser = argparse.ArgumentParser(description="Sign main.bin into main.bin.sig for LCM OTA.")
    parser.add_argument("--firmware", default="main.bin", help="Firmware file (default: ./main.bin)")
    parser.add_argument("--output", default="main.bin.sig", help="Output signature path")
    parser.add_argument(
        "--mode",
        choices=("official", "custom"),
        default=os.environ.get("LCM_SIGN_MODE", "official"),
        help="Signing mode (default: official)",
    )
    parser.add_argument("--key", default=None, help="Private key PEM path")
    parser.add_argument("--target", default=None, help="Chip target (esp32, esp32c3, ...)")
    parser.add_argument(
        "--sdkconfig",
        default="../sdkconfig",
        help="Path to sdkconfig for target auto-detect (default: ../sdkconfig)",
    )
    args = parser.parse_args()

    firmware_path = Path(args.firmware)
    if not firmware_path.is_file():
        raise SystemExit(f"Firmware not found: {firmware_path}")

    target = args.target or detect_target(Path(args.sdkconfig)) or "esp32"
    if target not in TARGET_IDS:
        raise SystemExit(f"Unsupported target '{target}'. Supported: {', '.join(sorted(TARGET_IDS))}")
    target_id = TARGET_IDS[target]

    private_key = resolve_private_key(args.mode, args.key)
    if not private_key.is_file():
        if args.mode == "official":
            raise SystemExit(
                f"Private key not found: {private_key}\n"
                "Set LCM_OFFICIAL_SIGNING_KEY=/secure/path/official_private.pem"
            )
        raise SystemExit(
            f"Private key not found: {private_key}\n"
            "Generate one via: ./scripts/generate_keys.sh keys/private publisher"
        )

    if shutil.which("openssl") is None:
        raise SystemExit("openssl not found in PATH.")

    firmware_bytes = firmware_path.read_bytes()
    firmware_len = len(firmware_bytes)
    firmware_hash = hashlib.sha256(firmware_bytes).digest()

    with tempfile.NamedTemporaryFile(delete=False) as tmp_sig:
        der_sig_path = Path(tmp_sig.name)

    try:
        subprocess.run(
            [
                "openssl",
                "dgst",
                "-sha256",
                "-sign",
                str(private_key),
                "-out",
                str(der_sig_path),
                str(firmware_path),
            ],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        signature_der = der_sig_path.read_bytes()
    finally:
        try:
            der_sig_path.unlink(missing_ok=True)
        except OSError:
            pass

    magic = 0x4C434D53  # LCMS
    version = 2
    algorithm = 1  # ECDSA P-256 SHA-256
    header = struct.pack(
        "<IBBHI32sH",
        magic,
        version,
        algorithm,
        target_id,
        firmware_len,
        firmware_hash,
        len(signature_der),
    )

    out_path = Path(args.output)
    out_path.write_bytes(header + signature_der)

    print(f"Signed: {firmware_path} -> {out_path}")
    print(f"Mode  : {args.mode}")
    print(f"Target: {target} (id={target_id})")
    print(f"Key   : {private_key}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
