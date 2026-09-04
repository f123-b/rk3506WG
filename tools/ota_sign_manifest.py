#!/usr/bin/env python3
"""Generate a signed RK3506 OTA version.json using RSA-PSS + SHA-256.

The private key remains on the release machine. The generated signature covers
all fields that affect update selection and artifact identity.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import pathlib
import subprocess
import tempfile


ALG = "RSA-PSS-SHA256"


def sha256_file(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def canonical_manifest(meta: dict) -> bytes:
    text = (
        "OTA-MANIFEST-V1\n"
        f"version={meta['version']}\n"
        f"type={meta['type']}\n"
        f"build_date={meta['build_date']}\n"
        f"filename={meta['filename']}\n"
        f"size={meta['size']}\n"
        f"sha256={meta['sha256']}\n"
        f"force_update={1 if meta['force_update'] else 0}\n"
        f"delta_url={meta.get('delta_url', '')}\n"
        f"delta_sha256={meta.get('delta_sha256', '')}\n"
        f"delta_size={meta.get('delta_size', 0)}\n"
        f"base_version={meta.get('base_version', '')}\n"
    )
    return text.encode("utf-8")


def sign_manifest(manifest: bytes, private_key: pathlib.Path) -> str:
    with tempfile.TemporaryDirectory() as td:
        td = pathlib.Path(td)
        manifest_path = td / "manifest.txt"
        sig_path = td / "manifest.sig"
        manifest_path.write_bytes(manifest)

        subprocess.run(
            [
                "openssl", "dgst", "-sha256",
                "-sign", str(private_key),
                "-sigopt", "rsa_padding_mode:pss",
                "-sigopt", "rsa_pss_saltlen:digest",
                "-out", str(sig_path),
                str(manifest_path),
            ],
            check=True,
        )
        return sig_path.read_bytes().hex()


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--artifact", required=True, type=pathlib.Path)
    p.add_argument("--private-key", required=True, type=pathlib.Path)
    p.add_argument("--version", required=True)
    p.add_argument("--type", choices=["app", "firmware"], default="app")
    p.add_argument("--build-date", default=dt.date.today().isoformat())
    p.add_argument("--filename")
    p.add_argument("--output", default="version.json", type=pathlib.Path)
    p.add_argument("--changelog", default="")
    p.add_argument("--force-update", action="store_true")

    p.add_argument("--delta-file", type=pathlib.Path)
    p.add_argument("--delta-url", default="")
    p.add_argument("--base-version", default="")
    args = p.parse_args()

    if not args.artifact.is_file():
        p.error(f"artifact not found: {args.artifact}")
    if not args.private_key.is_file():
        p.error(f"private key not found: {args.private_key}")

    meta = {
        "version": args.version,
        "type": args.type,
        "build_date": args.build_date,
        "filename": args.filename or args.artifact.name,
        "size": args.artifact.stat().st_size,
        "sha256": sha256_file(args.artifact),
        "force_update": bool(args.force_update),
        "changelog": args.changelog,
        "delta_url": "",
        "delta_sha256": "",
        "delta_size": 0,
        "base_version": "",
        "signature_alg": ALG,
    }

    if args.delta_file:
        if not args.delta_file.is_file():
            p.error(f"delta file not found: {args.delta_file}")
        if not args.base_version:
            p.error("--base-version is required with --delta-file")
        meta["delta_url"] = args.delta_url or args.delta_file.name
        meta["delta_sha256"] = sha256_file(args.delta_file)
        meta["delta_size"] = args.delta_file.stat().st_size
        meta["base_version"] = args.base_version

    meta["signature"] = sign_manifest(canonical_manifest(meta), args.private_key)

    args.output.write_text(
        json.dumps(meta, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(f"wrote signed manifest: {args.output}")
    print(f"artifact sha256: {meta['sha256']}")
    print(f"signature algorithm: {ALG}")


if __name__ == "__main__":
    main()
