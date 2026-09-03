#!/usr/bin/env python3
"""Verify an exact Waveshare CI flash artifact without rebuilding it."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import zipfile
from dataclasses import dataclass, asdict
from pathlib import Path, PurePosixPath

FORMAL_SOURCE_BRANCH = "work/waveshare/stabilization-integration"
FORMAL_SOURCE_EVENT = "push"
EXPECTED_ESP_IDF = "6.0.1"
EXPECTED_PRODUCT_DIR = "boards/waveshare_esp32_s3_touch_lcd_5/screen/product_800x480"

REQUIRED_SUFFIXES = (
    "automatrix_pvdg_waveshare_800x480.bin",
    "bootloader/bootloader.bin",
    "partition_table/partition-table.bin",
    "ota_data_initial.bin",
    "flasher_args.json",
    "flash_args",
    "flash_project_args",
    "effective-sdkconfig",
    "requalification-effective-config.txt",
    "CANDIDATE.txt",
    "SHA256SUMS.txt",
)
CANDIDATE_REQUIRED_KEYS = (
    "candidate_sha",
    "candidate_tree",
    "source_branch",
    "source_event",
    "github_run_id",
    "esp_idf",
    "product_dir",
    "physical_acceptance",
)
EXPECTED_FLASH_OPTIONS = ("--flash-mode dio", "--flash-freq 80m", "--flash-size 16MB")
EXPECTED_FLASH_IMAGES = {
    0x0: "bootloader/bootloader.bin",
    0x8000: "partition_table/partition-table.bin",
    0xF000: "ota_data_initial.bin",
    0x20000: "automatrix_pvdg_waveshare_800x480.bin",
}
HEX40_RE = re.compile(r"^[0-9a-f]{40}$", re.I)
HEX64_RE = re.compile(r"^[0-9a-f]{64}$", re.I)
RUN_ID_RE = re.compile(r"^[1-9][0-9]*$")
FLASH_IMAGE_RE = re.compile(r"^(0x[0-9a-fA-F]+)\s+(\S+)\s*$")


@dataclass
class Verification:
    passed: bool
    zip_sha256: str
    candidate: dict[str, str]
    app_sha256: str | None
    checksums_verified: int
    flash_images: dict[str, str]
    failures: list[str]


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _safe_name(name: str) -> bool:
    p = PurePosixPath(name)
    return not p.is_absolute() and ".." not in p.parts


def _find_suffix(names: list[str], suffix: str) -> list[str]:
    return [n for n in names if n == suffix or n.endswith("/" + suffix)]


def _parse_flash_args(text: str, failures: list[str]) -> dict[int, str]:
    images: dict[int, str] = {}
    option_text = " ".join(line.strip() for line in text.splitlines() if line.strip())
    for option in EXPECTED_FLASH_OPTIONS:
        if option not in option_text:
            failures.append(f"flash_option_missing:{option}")

    for line in text.splitlines():
        match = FLASH_IMAGE_RE.match(line.strip())
        if not match:
            continue
        offset = int(match.group(1), 16)
        image = match.group(2)
        if offset in images:
            failures.append(f"flash_offset_duplicate:0x{offset:x}")
        images[offset] = image

    if set(images) != set(EXPECTED_FLASH_IMAGES):
        failures.append(
            "flash_offsets_mismatch:"
            + ",".join(f"0x{x:x}" for x in sorted(images))
        )
    for offset, expected_suffix in EXPECTED_FLASH_IMAGES.items():
        actual = images.get(offset)
        if actual is not None and not (actual == expected_suffix or actual.endswith("/" + expected_suffix)):
            failures.append(f"flash_image_mismatch:0x{offset:x}:{actual}")
    return images


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
    flash_images: dict[int, str] = {}

    try:
        with zipfile.ZipFile(zip_path) as zf:
            names = [n for n in zf.namelist() if not n.endswith("/")]
            if len(names) != len(set(names)):
                failures.append("duplicate_archive_name")
            unsafe = [n for n in names if not _safe_name(n)]
            if unsafe:
                failures.append("unsafe_archive_path")

            for suffix in REQUIRED_SUFFIXES:
                hits = _find_suffix(names, suffix)
                if len(hits) != 1:
                    failures.append(f"required:{suffix}:count={len(hits)}")

            cand_hits = _find_suffix(names, "CANDIDATE.txt")
            if len(cand_hits) == 1:
                seen_candidate_keys: set[str] = set()
                for line in zf.read(cand_hits[0]).decode("utf-8", "replace").splitlines():
                    if "=" in line:
                        k, v = line.split("=", 1)
                        key = k.strip()
                        if key in seen_candidate_keys:
                            failures.append(f"duplicate_candidate_key:{key}")
                        seen_candidate_keys.add(key)
                        candidate[key] = v.strip()
                for key in CANDIDATE_REQUIRED_KEYS:
                    if not candidate.get(key):
                        failures.append(f"candidate_key_missing:{key}")
                if candidate.get("candidate_sha") and not HEX40_RE.fullmatch(candidate["candidate_sha"]):
                    failures.append("candidate_sha_invalid")
                if candidate.get("candidate_tree") and not HEX40_RE.fullmatch(candidate["candidate_tree"]):
                    failures.append("candidate_tree_invalid")
                if expected_sha and candidate.get("candidate_sha") != expected_sha:
                    failures.append("candidate_sha_mismatch")
                if expected_tree and candidate.get("candidate_tree") != expected_tree:
                    failures.append("candidate_tree_mismatch")
                if candidate.get("source_event") != FORMAL_SOURCE_EVENT:
                    failures.append("artifact_not_from_integration_push")
                if candidate.get("source_branch") != FORMAL_SOURCE_BRANCH:
                    failures.append("artifact_wrong_source_branch")
                if candidate.get("github_run_id") and not RUN_ID_RE.fullmatch(candidate["github_run_id"]):
                    failures.append("github_run_id_invalid")
                if candidate.get("esp_idf") != EXPECTED_ESP_IDF:
                    failures.append("esp_idf_mismatch")
                if candidate.get("product_dir") != EXPECTED_PRODUCT_DIR:
                    failures.append("product_dir_mismatch")
                if candidate.get("physical_acceptance") != "UNTESTED":
                    failures.append("artifact_physical_acceptance_not_untested")

            flash_hits = _find_suffix(names, "flash_args")
            if len(flash_hits) == 1:
                flash_images = _parse_flash_args(
                    zf.read(flash_hits[0]).decode("utf-8", "replace"), failures
                )
                for offset, image in flash_images.items():
                    suffix_hits = _find_suffix(names, image)
                    if len(suffix_hits) != 1:
                        failures.append(f"flash_image_archive_count:0x{offset:x}:{len(suffix_hits)}")

            sums_hits = _find_suffix(names, "SHA256SUMS.txt")
            if len(sums_hits) == 1:
                sums_name = sums_hits[0]
                base = PurePosixPath(sums_name).parent
                sums = zf.read(sums_name).decode("utf-8", "replace").splitlines()
                manifest_targets: set[str] = set()
                for line in sums:
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        digest, rel = line.split(None, 1)
                    except ValueError:
                        failures.append("malformed_sha256sums")
                        continue
                    if not HEX64_RE.fullmatch(digest):
                        failures.append("malformed_sha256_digest")
                        continue
                    rel = rel.lstrip("*").strip()
                    if rel.startswith("./"):
                        rel = rel[2:]
                    if not _safe_name(rel):
                        failures.append(f"unsafe_checksum_path:{rel}")
                        continue
                    target_path = base / PurePosixPath(rel) if str(base) != "." else PurePosixPath(rel)
                    target = str(target_path)
                    if target in manifest_targets:
                        failures.append(f"duplicate_checksum_target:{rel}")
                        continue
                    manifest_targets.add(target)
                    if target not in names:
                        failures.append(f"checksum_missing:{rel}")
                        continue
                    actual = sha256_bytes(zf.read(target))
                    if actual.lower() != digest.lower():
                        failures.append(f"checksum_mismatch:{rel}")
                    else:
                        verified += 1

                expected_targets = set(names) - {sums_name}
                for missing_target in sorted(expected_targets - manifest_targets):
                    failures.append(f"checksum_coverage_missing:{missing_target}")
                for extra_target in sorted(manifest_targets - expected_targets):
                    failures.append(f"checksum_coverage_extra:{extra_target}")

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
        flash_images={f"0x{k:x}": v for k, v in sorted(flash_images.items())},
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
        print(f"source_event={result.candidate.get('source_event')}")
        print(f"source_branch={result.candidate.get('source_branch')}")
        print(f"github_run_id={result.candidate.get('github_run_id')}")
        print(f"app_sha256={result.app_sha256}")
        print(f"checksums_verified={result.checksums_verified}")
        for offset, image in result.flash_images.items():
            print(f"flash_image[{offset}]={image}")
        for failure in result.failures:
            print(f"- {failure}")
    return 0 if result.passed else 1


if __name__ == "__main__":
    sys.exit(main())
