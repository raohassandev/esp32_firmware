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
PRODUCT_MODE = (ROOT / "web/product-mode.js").read_text(encoding="utf-8")

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


def catalogue_profile_blocks():
    marker = "static const inverter_profile_t PROFILES[] = {"
    if marker not in PROFILES:
        return []
    start = PROFILES.index(marker) + len(marker)
    end = PROFILES.find("\n};", start)
    if end < 0:
        return []
    return re.findall(r"\n    \{\n([\s\S]*?)\n    \},", PROFILES[start:end])


def production_ready_profile_ids():
    approved = []
    for block in catalogue_profile_blocks():
        if ".qualification = INVERTER_PROFILE_QUALIFICATION_PRODUCTION_APPROVED" not in block:
            continue
        if ".simulator_only = true" in block:
            continue
        if ".has_power_limit = true" not in block or ".has_power_limit_readback = true" not in block:
            continue
        match = re.search(r'\.id\s*=\s*"([^"]+)"', block)
        approved.append(match.group(1) if match else "<non-literal-profile-id>")
    return approved


blockers = []

# A missing macro means the bypass was removed outright, which is the safe state.
if macro_value(AUTH, "AUTH_TEMPORARY_FIELD_BYPASS") not in {0, None}:
    blockers.append("temporary Engineering authentication bypass is enabled")

dev_password = re.search(
    r"^\s*const\s+DEV_DEFAULT_ENGINEERING_PASSWORD\s*=\s*'([^']*)'", PRODUCT_MODE, re.MULTILINE
)
if dev_password and dev_password.group(1):
    blockers.append("web UI pre-fills a development Engineering password")

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

# A production image with the gate mechanism present but zero profiles that can
# actually pass it is not a completed PV-DG release. This check deliberately
# looks only inside the compiled catalogue initializers, so the enum/comparison
# used by inverter_profile_allows_write() cannot be mistaken for an approval.
production_profiles = production_ready_profile_ids()
if not production_profiles:
    blockers.append(
        "no non-simulator production-approved inverter profile with command readback is compiled"
    )

if production and blockers:
    raise SystemExit("PRODUCTION RELEASE BLOCKED:\n- " + "\n- ".join(blockers))

if production:
    print("production release compile-time safety gate passed")
    print("production-approved inverter profiles: " + ", ".join(production_profiles))
else:
    print("development build: production release remains blocked by design")
    for blocker in blockers:
        print(f"- {blocker}")
