#!/usr/bin/env python3
"""Production inverter writes require identity, telemetry and fresh ON_GRID status."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CMAKE = (ROOT / "components/inverter_manager/CMakeLists.txt").read_text(encoding="utf-8")
PROFILE_GATE = (ROOT / "components/inverter_manager/inverter_profile_write_gate.c").read_text(encoding="utf-8")
AUTHORITY = (ROOT / "components/inverter_manager/inverter_write_authority_guard.c").read_text(encoding="utf-8")
MANAGER = (ROOT / "components/inverter_manager/inverter_manager.c").read_text(encoding="utf-8")
PROFILES = (ROOT / "components/inverter_manager/inverter_profiles.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


for token in (
    '"inverter_write_authority_guard.c"',
    '"inverter_profile_write_gate.c"',
    'inverter_manager_set_total_power_kw=inverter_manager_set_total_power_kw_core',
    'inverter_profile_allows_write=inverter_profile_allows_write_core',
):
    require(token in CMAKE, f"production write wrapper integration missing: {token}")

for token in (
    "inverter_profile_allows_write_core(profile)",
    "profile->has_identity_probe",
    "profile->has_active_power",
    "inverter_profile_has_status_register(profile)",
):
    require(token in PROFILE_GATE, f"complete profile write evidence missing: {token}")

require("profile->has_identity_probe" in PROFILES,
        "base catalogue write predicate no longer requires identity")

for token in (
    "if (!isfinite(target_kw) || target_kw < 0.0f) return ESP_ERR_INVALID_ARG;",
    "target_kw > 0.0f && !inverter_manager_fleet_synchronised()",
    "return ESP_ERR_INVALID_STATE;",
    "return inverter_manager_set_total_power_kw_core(target_kw);",
):
    require(token in AUTHORITY, f"positive-command status authority missing: {token}")

# Safe zero must remain possible when status evidence disappears. The guard is
# intentionally positive-target-only; rollback and runtime-disable paths need a
# chance to command zero during faults/transitions.
status_check = AUTHORITY.index("target_kw > 0.0f && !inverter_manager_fleet_synchronised()")
core_call = AUTHORITY.index("inverter_manager_set_total_power_kw_core(target_kw)")
require(status_check < core_call, "status authority must run before positive core writes")
require("target_kw == 0" not in AUTHORITY and "target_kw <= 0" not in AUTHORITY,
        "safe zero must not be rejected merely because status is stale/unknown")

for token in (
    "runtime->write_allowed",
    "runtime->data.online",
    "runtime->data.telemetry_valid",
    "!runtime->data.telemetry_stale",
    "identity_is_current(runtime, timestamp)",
    "Build and validate the complete immutable fleet plan",
    "rollback_targets",
):
    require(token in MANAGER, f"transactional core safety contract missing: {token}")

print("complete production inverter write-authority contract passed")
