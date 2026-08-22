#!/usr/bin/env python3
"""One-shot repair for the fresh-controller persistence mismatch.

Publishes one shared-Core commit to phase1-fix, then one merge/alignment commit to
board/waveshare-esp32-s3-touch-lcd-5. Uses detached temporary worktrees so the
user's current checkout, build tree and serial workflow are not switched or reset.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

CORE_BRANCH = "phase1-fix"
BOARD_BRANCH = "board/waveshare-esp32-s3-touch-lcd-5"


def git(root: Path, *args: str, capture: bool = False) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=str(root),
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.STDOUT if capture else None,
    )
    if result.returncode != 0:
        if capture and result.stdout:
            print(result.stdout, file=sys.stderr)
        raise RuntimeError(f"git failed ({result.returncode}): git {' '.join(args)}")
    return (result.stdout or "").strip()


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(
            f"Refusing ambiguous patch in {path}: expected exactly one match, found {count}"
        )
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


def identity(worktree: Path) -> None:
    git(worktree, "config", "user.name", "RAO Hassan")
    git(
        worktree,
        "config",
        "user.email",
        "139758709+raohassandev@users.noreply.github.com",
    )


def remove_worktree(repo: Path, worktree: Path) -> None:
    try:
        git(repo, "worktree", "remove", "--force", str(worktree))
    finally:
        if worktree.exists():
            shutil.rmtree(worktree, ignore_errors=True)


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

    test_anchor = "\nwrite_api_paths = [\n"
    test_addition = r'''
assert "c->wifi.primary.enabled = CONFIG_PVDG_PRIMARY_WIFI_SSID[0] != '\\0';" in SOURCE, \
    "a fresh unit with no compiled station SSID must start with that station disabled"
assert "!c->wifi.primary.enabled ||" not in SOURCE, \
    "commissioning from the secured recovery AP must not require station Wi-Fi"
assert "c->interval_ms >= 20U" in SOURCE and "c->interval_ms >= 50U" not in SOURCE, \
    "the validator must accept the shipped 20 ms loop cadence"
assert "c->control.interval_ms = 20;" in SOURCE, \
    "the fresh-config contract must stay pinned to the shipped loop cadence"
'''
    replace_once(test, test_anchor, "\n" + test_addition + "write_api_paths = [\n")


def patch_board(worktree: Path) -> None:
    ui = worktree / "boards/waveshare_esp32_s3_touch_lcd_5/screen/pages/commissioning_screen.c"
    contract = worktree / "tests/waveshare_screen_source_contract.py"

    replace_once(
        ui,
        "if (!parse_ulong(s_ui.control_interval, 50U, 3600000U, &value)) return false;",
        "if (!parse_ulong(s_ui.control_interval, 20U, 3600000U, &value)) return false;",
    )

    contract_anchor = '    assert "DISARM automatic control" in commissioning_ui\n'
    contract_addition = (
        '    assert "parse_ulong(s_ui.control_interval, 20U, 3600000U" in commissioning_ui, '
        '"touchscreen commissioning must accept the Core shipped 20 ms loop cadence"\n'
    )
    replace_once(contract, contract_anchor, contract_anchor + contract_addition)


def main() -> int:
    repo = Path.cwd().resolve()
    if not (repo / ".git").exists():
        print("ERROR: run from D:\\Working\\esp32_firmware", file=sys.stderr)
        return 2

    origin = git(repo, "remote", "get-url", "origin", capture=True)
    if "esp32_firmware" not in origin:
        print(f"ERROR: unexpected origin {origin!r}", file=sys.stderr)
        return 2

    base = repo.parent
    core_wt = base / f"{repo.name}__fresh_config_core_tmp"
    board_wt = base / f"{repo.name}__fresh_config_board_tmp"
    for path in (core_wt, board_wt):
        if path.exists():
            shutil.rmtree(path, ignore_errors=True)

    core_sha = ""
    board_sha = ""
    try:
        print("[1/6] Fetching integration branches")
        git(repo, "fetch", "origin", CORE_BRANCH, BOARD_BRANCH)

        print("[2/6] Repairing shared Core defaults/validator")
        git(repo, "worktree", "add", "--detach", str(core_wt), f"origin/{CORE_BRANCH}")
        identity(core_wt)
        patch_core(core_wt)
        git(core_wt, "diff", "--check")
        git(
            core_wt,
            "add",
            "--",
            "components/config_manager/config_manager.c",
            "tests/config_import_safety_source_contract.py",
        )
        git(core_wt, "commit", "-m", "fix: make fresh controller configuration persistable")
        core_sha = git(core_wt, "rev-parse", "HEAD", capture=True)
        git(core_wt, "push", "origin", f"HEAD:{CORE_BRANCH}")
        remove_worktree(repo, core_wt)

        print("[3/6] Fetching repaired Core and current board head")
        git(repo, "fetch", "origin", CORE_BRANCH, BOARD_BRANCH)

        print("[4/6] Merging shared fix and aligning touchscreen input contract")
        git(repo, "worktree", "add", "--detach", str(board_wt), f"origin/{BOARD_BRANCH}")
        identity(board_wt)
        git(board_wt, "merge", "--no-commit", "--no-ff", f"origin/{CORE_BRANCH}")
        patch_board(board_wt)
        git(board_wt, "diff", "--check")
        git(
            board_wt,
            "add",
            "--",
            "boards/waveshare_esp32_s3_touch_lcd_5/screen/pages/commissioning_screen.c",
            "tests/waveshare_screen_source_contract.py",
        )
        git(board_wt, "commit", "-m", "screen: sync fresh-config persistence repair")
        board_sha = git(board_wt, "rev-parse", "HEAD", capture=True)
        git(board_wt, "push", "origin", f"HEAD:{BOARD_BRANCH}")
        remove_worktree(repo, board_wt)

        print("[5/6] Refreshing remote-tracking refs")
        git(repo, "fetch", "origin", CORE_BRANCH, BOARD_BRANCH)

        print("[6/6] Published")
        print(f"CORE_SHA={core_sha}")
        print(f"BOARD_SHA={board_sha}")
        print("NVS was not erased. Automatic control was not armed.")
        return 0
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        for path in (core_wt, board_wt):
            if path.exists():
                try:
                    remove_worktree(repo, path)
                except Exception:
                    pass
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
