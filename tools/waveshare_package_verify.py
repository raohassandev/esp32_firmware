#!/usr/bin/env python3
"""Verify an exact Waveshare CI flash artifact without rebuilding it."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import zipfile
from dataclasses import dataclass, asdict
from pathlib import Path, PurePosixPath

REQUIRED_SUFFIXES = (
    "automatrix_pvdg_waveshare_800x480.bin",
    "bootloader/bootloader.bin",
    "partition_table/partition-table.bin",
    "flasher_args.json",
    "flash_args",
    "flash_project_args",
    "effective-sdkconfig",
    "requalification-effective-config.txt",
    "CANDIDATE.txt",
    "SHA256SUMS.txt",
)


@dataclass
class Verification:
    passed: bool
    zip_sha256: str
    candidate: dict[str, str]
    app_sha256: str | None
    checksums_verified: int
    failures: list[str]


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _safe_name(name: str) -> bool:
    p = PurePosixPath(name)
    return not p.is_absolute() and ".." not in p.parts


def _find_suffix(names: list[str], suffix: str) -> list[str]:
    return [n for n in names if n == suffix or n.endswith("/" + suffix)]


def verify(
    zip_path: Path,
    expected_sha: str | None = None,
    expected_tree: str | None = None,
    expected_zip_sha256: str | None = None,
) -> Verification:
    raw = zip_path.read_bytes()
    zip_digest = sha256_bytes(raw)
    failures: list[str] = []
    if expected_zip_sha256 and zip_digest.lower() != expected_zip_sha256.lower():
        failures.append("zip_sha256_mismatch")

    candidate: dict[str, str] = {}
    app_digest: str | None = None
    verified = 0

    try:
        with zipfile.ZipFile(zip_path) as zf:
            names = [n for n in zf.namelist() if not n.endswith("/")]
            unsafe = [n for n in names if not _safe_name(n)]
            if unsafe:
                failures.append("unsafe_archive_path")

            for suffix in REQUIRED_SUFFIXES:
                hits = _find_suffix(names, suffix)
                if len(hits) != 1:
                    failures.append(f"required:{suffix}:count={len(hits)}")

            cand_hits = _find_suffix(names, "CANDIDATE.txt")
            if len(cand_hits) == 1:
                for line in zf.read(cand_hits[0]).decode("utf-8", "replace").splitlines():
                    if "=" in line:
                        k, v = line.split("=", 1)
                        candidate[k.strip()] = v.strip()
                if expected_sha and candidate.get("candidate_sha") != expected_sha:
                    failures.append("candidate_sha_mismatch")
                if expected_tree and candidate.get("candidate_tree") != expected_tree:
                    failures.append("candidate_tree_mismatch")
                if candidate.get("physical_acceptance") not in (None, "UNTESTED"):
                    failures.append("artifact_claims_physical_acceptance")

            sums_hits = _find_suffix(names, "SHA256SUMS.txt")
            if len(sums_hits) == 1:
                sums_name = sums_hits[0]
                base = PurePosixPath(sums_name).parent
                sums = zf.read(sums_name).decode("utf-8", "replace").splitlines()
                for line in sums:
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        digest, rel = line.split(None, 1)
                    except ValueError:
                        failures.append("malformed_sha256sums")
                        continue
                    rel = rel.lstrip("*").strip()
                    if rel.startswith("./"):
                        rel = rel[2:]
                    target = str(base / PurePosixPath(rel)) if str(base) != "." else rel
                    if target not in names:
                        failures.append(f"checksum_missing:{rel}")
                        continue
                    actual = sha256_bytes(zf.read(target))
                    if actual.lower() != digest.lower():
                        failures.append(f"checksum_mismatch:{rel}")
                    else:
                        verified += 1

            app_hits = _find_suffix(names, "automatrix_pvdg_waveshare_800x480.bin")
            if len(app_hits) == 1:
                app_digest = sha256_bytes(zf.read(app_hits[0]))
    except (zipfile.BadZipFile, OSError) as exc:
        failures.append(f"archive_error:{type(exc).__name__}")

    return Verification(
        passed=not failures,
        zip_sha256=zip_digest,
        candidate=candidate,
        app_sha256=app_digest,
        checksums_verified=verified,
        failures=failures,
    )


def main() -> int:
    p = argparse.ArgumentParser(description="Verify a Waveshare exact CI artifact ZIP")
    p.add_argument("zip", type=Path)
    p.add_argument("--expected-sha")
    p.add_argument("--expected-tree")
    p.add_argument("--expected-zip-sha256")
    p.add_argument("--json", action="store_true")
    args = p.parse_args()
    result = verify(args.zip, args.expected_sha, args.expected_tree, args.expected_zip_sha256)
    if args.json:
        print(json.dumps(asdict(result), indent=2, sort_keys=True))
    else:
        print("PACKAGE VERIFY PASS" if result.passed else "PACKAGE VERIFY FAIL")
        print(f"zip_sha256={result.zip_sha256}")
        print(f"candidate_sha={result.candidate.get('candidate_sha')}")
        print(f"candidate_tree={result.candidate.get('candidate_tree')}")
        print(f"app_sha256={result.app_sha256}")
        print(f"checksums_verified={result.checksums_verified}")
        for failure in result.failures:
            print(f"- {failure}")
    return 0 if result.passed else 1


if __name__ == "__main__":
    sys.exit(main())
