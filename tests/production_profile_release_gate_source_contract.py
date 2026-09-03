#!/usr/bin/env python3
"""Production release must fail closed when no qualified inverter profile exists."""
import os
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GATE = (ROOT / "tests/production_release_gate.py").read_text(encoding="utf-8")
PROFILES = (ROOT / "components/inverter_manager/inverter_profiles.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


for token in (
    "def catalogue_profile_blocks():",
    "def production_ready_profile_ids():",
    ".qualification = INVERTER_PROFILE_QUALIFICATION_PRODUCTION_APPROVED",
    ".simulator_only = true",
    ".has_power_limit = true",
    ".has_power_limit_readback = true",
    "no non-simulator production-approved inverter profile with command readback is compiled",
):
    require(token in GATE, f"production profile release blocker lost: {token}")

# Dynamically establish whether the compiled catalogue currently has a complete
# production initializer. This keeps the test valid after a real profile is
# eventually qualified instead of hard-coding today's pending state forever.
marker = "static const inverter_profile_t PROFILES[] = {"
require(marker in PROFILES, "compiled inverter profile catalogue missing")
start = PROFILES.index(marker) + len(marker)
end = PROFILES.find("\n};", start)
require(end > start, "compiled inverter profile catalogue is not brace-terminated")
blocks = re.findall(r"\n    \{\n([\s\S]*?)\n    \},", PROFILES[start:end])
ready = [
    block for block in blocks
    if ".qualification = INVERTER_PROFILE_QUALIFICATION_PRODUCTION_APPROVED" in block
    and ".simulator_only = true" not in block
    and ".has_power_limit = true" in block
    and ".has_power_limit_readback = true" in block
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

# Production behavior must match the live catalogue. With no qualified profile
# it must fail and name this blocker explicitly. Once a real approved profile is
# compiled, this test stops requiring that specific failure and lets all other
# production invariants decide the final result.
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
if not ready:
    require(prod.returncode != 0, "production gate passed with zero approved inverter profiles")
    require(
        "no non-simulator production-approved inverter profile with command readback is compiled" in combined,
        "production failure did not identify the missing approved inverter profile",
    )
else:
    require(
        "no non-simulator production-approved inverter profile with command readback is compiled" not in combined,
        "production gate falsely reports zero approved profiles",
    )

print(f"production inverter-profile release gate contract passed (approved profiles: {len(ready)})")
