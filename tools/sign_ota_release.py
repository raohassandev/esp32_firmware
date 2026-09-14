#!/usr/bin/env python3
"""Sign and verify an Automatrix ESP32-S3 OTA application image.

The private RSA-3072 signing key is intentionally supplied at release time and
must live outside this repository. This helper delegates cryptographic signing
and verification to the ESP-IDF tooling; it never serializes key material.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from datetime import datetime, timezone

OTA_SLOT_BYTES = 0x300000
KIND = "automatrix_signed_ota_release"
SCHEMA = 1


def fail(message: str) -> "NoReturn":
    raise SystemExit(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(command: list[str]) -> None:
    print("+", " ".join(command[:2] + ["<key>" if "--keyfile" in command else *command[2:]])) if False else None
    subprocess.run(command, check=True)


def git_commit(repo: Path) -> str:
    try:
        return subprocess.check_output(
            ["git", "-C", str(repo), "rev-parse", "HEAD"],
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def key_is_inside_repo(repo: Path, key: Path) -> bool:
    try:
        key.relative_to(repo)
        return True
    except ValueError:
        return False


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Create and verify a production signed OTA image without committing the signing key."
    )
    parser.add_argument("--input", required=True, type=Path, help="secure-padded unsigned app from the signed-OTA build profile")
    parser.add_argument("--key", required=True, type=Path, help="RSA-3072 private signing key outside the repository")
    parser.add_argument("--output", required=True, type=Path, help="destination signed .bin")
    parser.add_argument("--manifest", type=Path, help="release manifest path; defaults beside output")
    parser.add_argument("--idf-py", default="idf.py", help="ESP-IDF idf.py executable")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    source = args.input.expanduser().resolve()
    key = args.key.expanduser().resolve()
    output = args.output.expanduser().resolve()
    manifest = (args.manifest.expanduser().resolve() if args.manifest else output.with_suffix(output.suffix + ".manifest.json"))

    if not source.is_file():
        fail(f"Unsigned application not found: {source}")
    if not key.is_file():
        fail(f"Signing key not found: {key}")
    if key_is_inside_repo(repo, key):
        fail("Refusing to use a private signing key stored inside the repository tree. Move it to controlled external storage.")
    if source == output:
        fail("Output must be different from the unsigned source image.")
    if source.stat().st_size <= 0:
        fail("Unsigned application is empty.")
    if source.stat().st_size >= OTA_SLOT_BYTES:
        fail(f"Unsigned application is already too large for the 0x{OTA_SLOT_BYTES:X}-byte OTA slot.")

    output.parent.mkdir(parents=True, exist_ok=True)
    manifest.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        output.unlink()

    # ESP-IDF owns the exact RSA-PSS signature block format. Passing the key path
    # as a process argument avoids copying key material into generated files.
    subprocess.run(
        [args.idf_py, "secure-sign-data", "--keyfile", str(key), "--output", str(output), str(source)],
        check=True,
    )
    if not output.is_file():
        fail("ESP-IDF signing command returned without producing the signed image.")
    if output.stat().st_size <= source.stat().st_size:
        fail("Signed image did not grow; expected an appended Secure Boot v2 signature block.")
    if output.stat().st_size > OTA_SLOT_BYTES:
        output.unlink(missing_ok=True)
        fail(f"Signed image exceeds the 0x{OTA_SLOT_BYTES:X}-byte OTA slot.")

    # Verify the just-produced image before it can be packaged. ESP-IDF accepts
    # either the matching public or private key for this verification command.
    subprocess.run(
        [args.idf_py, "secure-verify-signature", "--keyfile", str(key), str(output)],
        check=True,
    )

    record = {
        "schema": SCHEMA,
        "kind": KIND,
        "generated_at": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "source_commit": git_commit(repo),
        "security_profile": "signed-ota-without-hardware-secure-boot",
        "signature_scheme": "rsa-v2",
        "hardware_secure_boot": False,
        "signed_ota_verification_required": True,
        "private_key_embedded": False,
        "unsigned_image": {
            "name": source.name,
            "bytes": source.stat().st_size,
            "sha256": sha256(source),
        },
        "signed_image": {
            "name": output.name,
            "bytes": output.stat().st_size,
            "sha256": sha256(output),
            "ota_slot_bytes": OTA_SLOT_BYTES,
        },
        "verification": {
            "tool": "idf.py secure-verify-signature",
            "result": "pass",
        },
        "release_boundaries": {
            "requires_initial_signed_app": True,
            "physical_interruption_test_claimed": False,
            "hardware_secure_boot_claimed": False,
        },
    }
    manifest.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")

    print(f"Signed OTA image: {output}")
    print(f"SHA-256: {record['signed_image']['sha256']}")
    print(f"Manifest: {manifest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
