#!/usr/bin/env python3
"""One-shot branch-disciplined repair for fresh-controller config persistence.

This helper is intentionally kept off the product branches. It patches the shared
Core on phase1-fix, then merges that shared fix into the Waveshare board branch and
aligns the board-local commissioning form with the Core's shipped 20 ms loop cadence.
It never erases NVS, never arms control, and never changes commissioning/safety gates.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

REPO = "raohassandev/esp32_firmware"
CORE_BRANCH = "phase1-fix"
BOARD_BRANCH = "board/waveshare-esp32-s3-touch-lcd-5"


def run(*args: str, cwd: Path, capture: bool = False) -> str:
    cmd = ["git", *args]
    result = subprocess.run(
        cmd,
        cwd=str(cwd),
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.STDOUT if capture else None,
    )
    if result.returncode != 0:
        if capture and result.stdout:
            print(result.stdout, file=sys.stderr)
        raise SystemExit(f"git command failed ({result.returncode}): {' '.join(cmd)}")
    return (result.stdout or "").strip()


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(
            f"Refusing ambiguous patch in {path}: expected exactly one match, found {count}."
        )
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


def configure_identity(worktree: Path) -> None:
    run("config", "user.name", "RAO Hassan", cwd=worktree)
    run("config", "user.email", "139758709+raohassandev@users.noreply.github.com", cwd=worktree)


def remove_worktree(root: Path, path: Path) -> None:
    try:
        run("worktree", "remove", "--force", str(path), cwd=root)
    finally:
        if path.exists():
            shutil.rmtree(path, ignore_errors=True)


def patch_core(worktree: Path) -> None:
    source = worktree / "components/config_manager/config_manager.c"
    test = worktree / "tests/config_import_safety_source_contract.py"

    replace_once(
        source,
        "    c->wifi.primary.enabled = true;\n"
        "    strlcpy(c->wifi.primary.ssid, CONFIG_PVDG_PRIMARY_WIFI_SSID, sizeof(c->wifi.primary.ssid));",
        "    /* A fresh controller is intentionally reachable from its always-on recovery AP\n"
        "     * before a station network has been commissioned. An empty build SSID therefore\n"
        "     * describes a disabled station profile, not an invalid controller configuration. */\n"
        "    c->wifi.primary.enabled = CONFIG_PVDG_PRIMARY_WIFI_SSID[0] != '\\0';\n"
        "    strlcpy(c->wifi.primary.ssid, CONFIG_PVDG_PRIMARY_WIFI_SSID, sizeof(c->wifi.primary.ssid));",
    )

    replace_once(
        source,
        "           c->interval_ms >= 50U && c->meter_stale_timeout_ms >= 100U &&",
        "           /* The shipped loop cadence is 20 ms. Plant decisions are independently\n"
        "            * rate-gated by CONTROL_MIN_DECISION_INTERVAL_MS in the control engine,\n"
        "            * so rejecting 20 ms here only makes the safe factory defaults unsaveable. */\n"
        "           c->interval_ms >= 20U && c->meter_stale_timeout_ms >= 100U &&",
    )

    replace_once(
        source,
        "        !profile_valid(&c->wifi.primary) || !profile_valid(&c->wifi.fallback) ||\n"
        "        !c->wifi.primary.enabled || c->wifi.max_retries_per_profile == 0 ||",
        "        !profile_valid(&c->wifi.primary) || !profile_valid(&c->wifi.fallback) ||\n"
        "        /* Station Wi-Fi is optional during commissioning. The secured, always-on\n"
        "         * recovery AP is the reachability guarantee for a factory-fresh unit. */\n"
        "        c->wifi.max_retries_per_profile == 0 ||",
    )

    anchor = (
        'assert "if (err == ESP_OK && !valid(c))" in SOURCE, \\\n'
        '    "imported configuration must pass full structural/numeric validation before persistence"\n'
    )
    addition = (
        'assert "c->wifi.primary.enabled = CONFIG_PVDG_PRIMARY_WIFI_SSID[0] != \'\\\\0\';" in SOURCE, \\\n'
        '    "a fresh unit with no compiled station SSID must start with that station disabled"\n'
        'assert "!c->wifi.primary.enabled ||" not in SOURCE, \\\n'
        '    "commissioning from the secured recovery AP must not require station Wi-Fi"\n'
        'assert "c->interval_ms >= 20U" in SOURCE and "c->interval_ms >= 50U" not in SOURCE, \\\n'
        '    "the validator must accept the shipped 20 ms loop cadence"\n'
        'assert "c->control.interval_ms = 20;" in SOURCE, \\\n'
        '    "the fresh-config contract must stay pinned to the shipped loop cadence"\n'
    )
    replace_once(test, anchor, anchor + addition)


def patch_board(worktree: Path) -> None:
    ui = worktree / "boards/waveshare_esp32_s3_touch_lcd_5/screen/pages/commissioning_screen.c"
    contract = worktree / "tests/waveshare_screen_source_contract.py"

    replace_once(
        ui,
        "if (!parse_ulong(s_ui.control_interval, 50U, 3600000U, &value)) return false;",
        "if (!parse_ulong(s_ui.control_interval, 20U, 3600000U, &value)) return false;",
    )

    anchor = '    assert "DISARM automatic control" in commissioning_ui\n'
    addition = (
        '    assert "parse_ulong(s_ui.control_interval, 20U, 3600000U" in commissioning_ui, '
        '"touchscreen commissioning must accept the Core shipped 20 ms loop cadence"\n'
    )
    replace_once(contract, anchor, anchor + addition)


def main() -> None:
    root = Path.cwd().resolve()
    if not (root / ".git").exists():
        raise SystemExit("Run this helper from the esp32_firmware repository root.")

    origin = run("remote", "get-url", "origin", cwd=root, capture=True)
    if not origin or "esp32_firmware" not in origin:
        raise SystemExit(f"Unexpected origin: {origin!r}")

    print("[1/6] Fetching exact integration branches...")
    run("fetch", "origin", CORE_BRANCH, BOARD_BRANCH, cwd=root)

    base_dir = root.parent
    core_wt = base_dir / f"{root.name}__fresh_config_core_tmp"
    board_wt = base_dir / f"{root.name}__fresh_config_board_tmp"
    for path in (core_wt, board_wt):
        if path.exists():
            shutil.rmtree(path, ignore_errors=True)

    core_sha = ""
    board_sha = ""
    try:
        print("[2/6] Patching shared Core on phase1-fix...")
        run("worktree", "add", "--detach", str(core_wt), f"origin/{CORE_BRANCH}", cwd=root)
        configure_identity(core_wt)
        patch_core(core_wt)
        run("diff", "--check", cwd=core_wt)
        run(
            "add",
            "--",
            "components/config_manager/config_manager.c",
            "tests/config_import_safety_source_contract.py",
            cwd=core_wt,
        )
        run("commit", "-m", "fix: make fresh controller configuration persistable", cwd=core_wt)
        core_sha = run("rev-parse", "HEAD", cwd=core_wt, capture=True)
        run("push", "origin", f"HEAD:{CORE_BRANCH}", cwd=core_wt)
        remove_worktree(root, core_wt)

        print("[3/6] Syncing the shared Core fix into the Waveshare board branch...")
        run("fetch", "origin", CORE_BRANCH, BOARD_BRANCH, cwd=root)
        run("worktree", "add", "--detach", str(board_wt), f"origin/{BOARD_BRANCH}", cwd=root)
        configure_identity(board_wt)
        run("merge", "--no-commit", "--no-ff", f"origin/{CORE_BRANCH}", cwd=board_wt)

        print("[4/6] Aligning touchscreen commissioning with the corrected Core contract...")
        patch_board(board_wt)
        run("diff", "--check", cwd=board_wt)
        run(
            "add",
            "--",
            "boards/waveshare_esp32_s3_touch_lcd_5/screen/pages/commissioning_screen.c",
            "tests/waveshare_screen_source_contract.py",
            cwd=board_wt,
        )
        run("commit", "-m", "screen: sync fresh-config persistence repair", cwd=board_wt)
        board_sha = run("rev-parse", "HEAD", cwd=board_wt, capture=True)
        run("push", "origin", f"HEAD:{BOARD_BRANCH}", cwd=board_wt)
        remove_worktree(root, board_wt)

        print("[5/6] Refreshing local remote-tracking refs...")
        run("fetch", "origin", CORE_BRANCH, BOARD_BRANCH, cwd=root)

        print("[6/6] Repair published successfully.")
        print(f"CORE_SHA={core_sha}")
        print(f"BOARD_SHA={board_sha}")
        print("Automatic control was not armed and NVS was not erased.")
    except BaseException:
        for path in (core_wt, board_wt):
            if path.exists():
                try:
                    remove_worktree(root, path)
                except Exception:
                    pass
        raise


if __name__ == "__main__":
    main()
