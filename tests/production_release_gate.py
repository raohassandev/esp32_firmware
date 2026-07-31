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
CONFIG_MANAGER = (ROOT / "components/config_manager/config_manager.c").read_text(encoding="utf-8")

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

# The recovery access point is permanently on air, so its passphrase is the
# controller's outer perimeter rather than a last-resort convenience. A bench
# build may carry a predictable passphrase; a production release may not,
# because this source is public and the value would be identical on every unit.
#
# The intended production setting is an EMPTY default, which makes
# ensure_recovery_ap_secret() draw a per-device passphrase from the hardware
# RNG on first boot. Anything else compiled in is a blocker.
WEAK_AP_PASSPHRASES = {
    "12345678",
    "123456789",
    "1234567890",
    "password",
    "Password",
    "password1",
    "admin",
    "administrator",
    "automatrix",
    "automatrix123",
    "abcdefgh",
    "qwertyui",
    "00000000",
    "11111111",
}


def kconfig_default_string_for(symbol: str):
    """Kconfig string default, unquoted. None when the symbol has no default."""
    raw = kconfig_default_for(symbol)
    if raw is None:
        return None
    match = re.match(r'^"(.*)"$', raw)
    return match.group(1) if match else raw


recovery_default = kconfig_default_string_for("PVDG_RECOVERY_AP_PASSWORD")
if recovery_default is None:
    blockers.append(
        "PVDG_RECOVERY_AP_PASSWORD is missing, so the recovery AP passphrase is undefined"
    )
elif recovery_default == "":
    # An empty default is the CORRECT production setting, but only because
    # config_manager generates a per-device passphrase from the hardware RNG when
    # it finds none stored. Without that path, "empty" means an always-on access
    # point with no passphrase at all, which is the worst outcome of the set -- so
    # empty is only allowed while the generator is actually present and wired in.
    if not (
        "ensure_recovery_ap_secret" in CONFIG_MANAGER
        and "esp_fill_random" in CONFIG_MANAGER
        and "ensure_recovery_ap_secret(loaded)" in CONFIG_MANAGER
    ):
        blockers.append(
            "recovery AP passphrase default is empty but no per-device generator is "
            "applied at start-up, so the always-on access point would have no passphrase"
        )
else:
    if len(recovery_default) < 8:
        # WPA2's own minimum. Shorter than this cannot secure the AP at all.
        blockers.append(
            "recovery AP passphrase default is shorter than the WPA2 minimum of 8 characters"
        )
    elif recovery_default in WEAK_AP_PASSPHRASES or recovery_default.lower() in WEAK_AP_PASSPHRASES:
        blockers.append(
            "recovery AP passphrase default is a well known weak passphrase"
        )
    else:
        blockers.append(
            "recovery AP passphrase is compiled in, so every unit built from this "
            "public source shares one passphrase; leave it empty for per-device generation"
        )

# The same value must not be pinned in a checked-in sdkconfig either.
sdkconfig_recovery = re.search(
    r'^CONFIG_PVDG_RECOVERY_AP_PASSWORD="(.*)"\s*$', SDKCONFIG, re.MULTILINE
)
if sdkconfig_recovery and sdkconfig_recovery.group(1):
    blockers.append("sdkconfig pins a compiled-in recovery AP passphrase")

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
