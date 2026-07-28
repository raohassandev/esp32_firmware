#!/usr/bin/env python3
"""Fail closed when a workflow is explicitly requested as a production release.

Normal pull-request and development builds remain allowed while clearly reporting
which release blockers are active. A workflow_dispatch run with
PVDG_PRODUCTION_RELEASE=1 must satisfy every compile-time release invariant.
"""

import os
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUTH = (ROOT / "components/web_server/engineering_auth.c").read_text(encoding="utf-8")
KCONFIG = (ROOT / "main/Kconfig.projbuild").read_text(encoding="utf-8")
SDKCONFIG = (ROOT / "sdkconfig").read_text(encoding="utf-8") if (ROOT / "sdkconfig").exists() else ""
PROFILES = (ROOT / "components/inverter_manager/inverter_profiles.c").read_text(encoding="utf-8")

production = os.environ.get("PVDG_PRODUCTION_RELEASE", "0") == "1"


def macro_value(source: str, name: str):
    match = re.search(rf"^\s*#define\s+{re.escape(name)}\s+(\d+)\s*$", source, re.MULTILINE)
    return int(match.group(1)) if match else None


def kconfig_default_for(symbol: str):
    block = re.search(
        rf"^config\s+{re.escape(symbol)}\s*$([\s\S]*?)(?=^config\s+|\Z)",
        KCONFIG,
        re.MULTILINE,
    )
    if not block:
        return None
    match = re.search(r"^\s*default\s+([^\s#]+)", block.group(1), re.MULTILINE)
    return match.group(1) if match else None


blockers = []

if macro_value(AUTH, "AUTH_TEMPORARY_FIELD_BYPASS") != 0:
    blockers.append("temporary Engineering authentication bypass is enabled")

if kconfig_default_for("PVDG_APPLY_BUILD_WIFI_PROVISIONING") not in {"n", None}:
    blockers.append("build Wi-Fi provisioning defaults to enabled")

for symbol in (
    "CONFIG_PVDG_APPLY_BUILD_WIFI_PROVISIONING=y",
    "CONFIG_PVDG_PRIMARY_WIFI_PASSWORD=",
    "CONFIG_PVDG_DEFAULT_WIFI_PASSWORD=",
):
    if symbol.endswith("=y") and symbol in SDKCONFIG:
        blockers.append("sdkconfig enables build Wi-Fi provisioning")

# Pending/simulator-only catalogue entries are allowed to exist, but no such
# profile may satisfy the production write predicate.
if "!profile->simulator_only" not in PROFILES:
    blockers.append("simulator-only inverter profiles are not excluded from writes")
if "INVERTER_PROFILE_QUALIFICATION_PRODUCTION_APPROVED" not in PROFILES:
    blockers.append("production-approved inverter qualification gate is missing")

if production and blockers:
    raise SystemExit("PRODUCTION RELEASE BLOCKED:\n- " + "\n- ".join(blockers))

if production:
    print("production release compile-time safety gate passed")
else:
    print("development build: production release remains blocked by design")
    for blocker in blockers:
        print(f"- {blocker}")
