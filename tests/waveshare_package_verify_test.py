#!/usr/bin/env python3
import importlib.util
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("waveshare_package_verify", ROOT / "tools" / "waveshare_package_verify.py")
MOD = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
sys.modules[SPEC.name] = MOD
SPEC.loader.exec_module(MOD)

SHA = "1" * 40
TREE = "2" * 40
APP = "boards/w/screen/product_800x480/build/automatrix_pvdg_waveshare_800x480.bin"
FLASH_ARGS = (
    b"--flash-mode dio --flash-freq 80m --flash-size 16MB\n"
    b"0x0 bootloader/bootloader.bin\n"
    b"0x8000 partition_table/partition-table.bin\n"
    b"0xf000 ota_data_initial.bin\n"
    b"0x20000 automatrix_pvdg_waveshare_800x480.bin\n"
)


def make_zip(
    path: Path,
    tamper: bool = False,
    omit_checksum_for: str | None = None,
    physical_acceptance: str = "UNTESTED",
    flash_args: bytes = FLASH_ARGS,
    source_event: str = "push",
    source_branch: str = "work/waveshare/stabilization-integration",
    github_run_id: str = "123456789",
    esp_idf: str = "6.0.1",
    product_dir: str = "boards/waveshare_esp32_s3_touch_lcd_5/screen/product_800x480",
) -> str:
    files = {
        APP: b"app-binary",
        "boards/w/screen/product_800x480/build/bootloader/bootloader.bin": b"boot",
        "boards/w/screen/product_800x480/build/partition_table/partition-table.bin": b"partitions",
        "boards/w/screen/product_800x480/build/ota_data_initial.bin": b"ota-data",
        "boards/w/screen/product_800x480/build/flasher_args.json": b"{}",
        "boards/w/screen/product_800x480/build/flash_args": flash_args,
        "boards/w/screen/product_800x480/build/flash_project_args": b"project",
        "effective-sdkconfig": b"CONFIG_FOO=y\n",
        "boards/w/screen/product_800x480/build/requalification-effective-config.txt": b"CONFIG_FOO=y\n",
        "CANDIDATE.txt": (
            f"candidate_sha={SHA}\n"
            f"candidate_tree={TREE}\n"
            f"source_branch={source_branch}\n"
            f"source_event={source_event}\n"
            f"github_run_id={github_run_id}\n"
            f"esp_idf={esp_idf}\n"
            f"product_dir={product_dir}\n"
            f"physical_acceptance={physical_acceptance}\n"
        ).encode(),
    }
    sums = []
    for name, data in files.items():
        if name != omit_checksum_for:
            sums.append(f"{MOD.sha256_bytes(data)}  ./{name}")
    files["SHA256SUMS.txt"] = ("\n".join(sums) + "\n").encode()
    if tamper:
        files[APP] = b"tampered"
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for name, data in files.items():
            zf.writestr(name, data)
    return MOD.sha256_bytes(path.read_bytes())


def main() -> None:
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        good_zip = td / "good.zip"
        digest = make_zip(good_zip)
        good = MOD.verify(good_zip, expected_sha=SHA, expected_tree=TREE, expected_zip_sha256=digest)
        assert good.passed, good.failures
        assert good.checksums_verified == 10
        assert good.candidate["candidate_sha"] == SHA
        assert good.candidate["source_event"] == "push"
        assert good.candidate["source_branch"] == "work/waveshare/stabilization-integration"
        assert good.app_sha256 == MOD.sha256_bytes(b"app-binary")
        assert good.flash_images == {
            "0x0": "bootloader/bootloader.bin",
            "0x8000": "partition_table/partition-table.bin",
            "0xf000": "ota_data_initial.bin",
            "0x20000": "automatrix_pvdg_waveshare_800x480.bin",
        }

        pr_zip = td / "pr.zip"
        make_zip(pr_zip, source_event="pull_request", source_branch="41/merge")
        pr_result = MOD.verify(pr_zip, expected_sha=SHA, expected_tree=TREE)
        assert not pr_result.passed
        assert "artifact_not_from_integration_push" in pr_result.failures
        assert "artifact_wrong_source_branch" in pr_result.failures

        bad_run_zip = td / "bad-run.zip"
        make_zip(bad_run_zip, github_run_id="not-a-run")
        bad_run = MOD.verify(bad_run_zip, expected_sha=SHA, expected_tree=TREE)
        assert not bad_run.passed
        assert "github_run_id_invalid" in bad_run.failures

        wrong_idf_zip = td / "wrong-idf.zip"
        make_zip(wrong_idf_zip, esp_idf="6.0-dev")
        wrong_idf = MOD.verify(wrong_idf_zip, expected_sha=SHA, expected_tree=TREE)
        assert not wrong_idf.passed
        assert "esp_idf_mismatch" in wrong_idf.failures

        wrong_product_zip = td / "wrong-product.zip"
        make_zip(wrong_product_zip, product_dir="boards/other")
        wrong_product = MOD.verify(wrong_product_zip, expected_sha=SHA, expected_tree=TREE)
        assert not wrong_product.passed
        assert "product_dir_mismatch" in wrong_product.failures

        wrong_sha = MOD.verify(good_zip, expected_sha="3" * 40)
        assert not wrong_sha.passed
        assert "candidate_sha_mismatch" in wrong_sha.failures

        wrong_zip = MOD.verify(good_zip, expected_zip_sha256="0" * 64)
        assert not wrong_zip.passed
        assert "zip_sha256_mismatch" in wrong_zip.failures

        bad_zip = td / "bad.zip"
        make_zip(bad_zip, tamper=True)
        bad = MOD.verify(bad_zip, expected_sha=SHA, expected_tree=TREE)
        assert not bad.passed
        assert any(x.startswith("checksum_mismatch:") for x in bad.failures)

        incomplete_zip = td / "incomplete.zip"
        make_zip(incomplete_zip, omit_checksum_for=APP)
        incomplete = MOD.verify(incomplete_zip, expected_sha=SHA, expected_tree=TREE)
        assert not incomplete.passed
        assert f"checksum_coverage_missing:{APP}" in incomplete.failures

        claimed_zip = td / "claimed.zip"
        make_zip(claimed_zip, physical_acceptance="PASSED")
        claimed = MOD.verify(claimed_zip, expected_sha=SHA, expected_tree=TREE)
        assert not claimed.passed
        assert "artifact_physical_acceptance_not_untested" in claimed.failures

        wrong_layout_zip = td / "wrong-layout.zip"
        make_zip(
            wrong_layout_zip,
            flash_args=FLASH_ARGS.replace(b"0x20000 automatrix", b"0x30000 automatrix"),
        )
        wrong_layout = MOD.verify(wrong_layout_zip, expected_sha=SHA, expected_tree=TREE)
        assert not wrong_layout.passed
        assert any(x.startswith("flash_offsets_mismatch:") for x in wrong_layout.failures)

        wrong_mode_zip = td / "wrong-mode.zip"
        make_zip(wrong_mode_zip, flash_args=FLASH_ARGS.replace(b"--flash-mode dio", b"--flash-mode qio"))
        wrong_mode = MOD.verify(wrong_mode_zip, expected_sha=SHA, expected_tree=TREE)
        assert not wrong_mode.passed
        assert "flash_option_missing:--flash-mode dio" in wrong_mode.failures

    print("Waveshare package verifier tests passed")


if __name__ == "__main__":
    main()
