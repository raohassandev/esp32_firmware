#!/usr/bin/env python3
"""Production release must fail closed when no fully-qualified inverter profile exists."""
import os
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GATE = (ROOT / "tests/production_release_gate.py").read_text(encoding="utf-8")
PROFILES = (ROOT / "components/inverter_manager/inverter_profiles.c").read_text(encoding="utf-8")
PROFILE_GATE = (ROOT / "components/inverter_manager/inverter_profile_write_gate.c").read_text(encoding="utf-8")
AUTHORITY = (ROOT / "components/inverter_manager/inverter_write_authority_guard.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


for token in (
    "def catalogue_profile_blocks():",
    "def production_ready_profile_ids():",
    ".qualification = INVERTER_PROFILE_QUALIFICATION_PRODUCTION_APPROVED",
    ".simulator_only = true",
    ".has_identity_probe = true",
    ".has_active_power = true",
    ".has_power_limit = true",
    ".has_power_limit_readback = true",
    ".status_register = {",
    ".configured = true",
    "no non-simulator production-approved inverter profile with identity, telemetry, status, command and readback is compiled",
):
    require(token in GATE, f"production profile release blocker lost: {token}")

for token in (
    "profile->has_identity_probe",
    "profile->has_active_power",
    "inverter_profile_has_status_register(profile)",
):
    require(token in PROFILE_GATE, f"runtime production profile gate lost: {token}")

require("target_kw > 0.0f && !inverter_manager_fleet_synchronised()" in AUTHORITY,
        "positive fleet command is not gated by fresh ON_GRID status")
require("return inverter_manager_set_total_power_kw_core(target_kw);" in AUTHORITY,
        "status authority guard is not forwarding accepted commands to the transactional core")

# Dynamically establish whether the compiled catalogue currently has a complete
# production initializer. This remains valid after a real profile is qualified.
marker = "static const inverter_profile_t PROFILES[] = {"
require(marker in PROFILES, "compiled inverter profile catalogue missing")
start = PROFILES.index(marker) + len(marker)
end = PROFILES.find("\n};", start)
require(end > start, "compiled inverter profile catalogue is not brace-terminated")
blocks = re.findall(r"\n    \{\n([\s\S]*?)\n    \},", PROFILES[start:end])
required = (
    ".qualification = INVERTER_PROFILE_QUALIFICATION_PRODUCTION_APPROVED",
    ".has_identity_probe = true",
    ".has_active_power = true",
    ".has_power_limit = true",
    ".has_power_limit_readback = true",
    ".status_register = {",
    ".configured = true",
)
ready = [
    block for block in blocks
    if all(token in block for token in required)
    and ".simulator_only = true" not in block
]

# Development builds must remain runnable while blockers are reported.
dev = subprocess.run(
    ["python3", str(ROOT / "tests/production_release_gate.py")],
    cwd=ROOT,
    capture_output=True,
    text=True,
    check=False,
)
require(dev.returncode == 0, f"development build gate unexpectedly failed: {dev.stderr}")

# Production behavior must match the live catalogue. With no fully-qualified
# profile it must fail and name the exact evidence deficit.
environment = os.environ.copy()
environment["PVDG_PRODUCTION_RELEASE"] = "1"
prod = subprocess.run(
    ["python3", str(ROOT / "tests/production_release_gate.py")],
    cwd=ROOT,
    env=environment,
    capture_output=True,
    text=True,
    check=False,
)
combined = prod.stdout + prod.stderr
blocker = "no non-simulator production-approved inverter profile with identity, telemetry, status, command and readback is compiled"
if not ready:
    require(prod.returncode != 0, "production gate passed with zero fully-qualified inverter profiles")
    require(blocker in combined,
            "production failure did not identify the missing complete inverter evidence")
else:
    require(blocker not in combined,
            "production gate falsely reports zero fully-qualified profiles")

print(f"production inverter-profile release gate contract passed (fully qualified profiles: {len(ready)})")
