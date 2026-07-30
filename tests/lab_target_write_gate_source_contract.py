#!/usr/bin/env python3
"""Lab-simulator write authority must never leak into production authority.

The site's inverters are thousands of miles away, so the control loop is
validated against a Modbus simulator. That required a second, narrower write
authority: an engineer declares a specific inverter endpoint to be a simulator,
and that declaration alone unlocks a command through a profile that has not
passed physical qualification.

The danger is obvious -- a path that commands unqualified equipment. This
contract pins the structural properties that keep the two authorities separate.
The behaviour itself is EXECUTED by tests/inverter_write_permission_test.c and
tests/commissioning_gate_test.c; this file guards the wiring those tests cannot
see.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

PROFILES_H = (ROOT / "components/inverter_manager/include/inverter_profiles.h").read_text(encoding="utf-8")
PROFILES_C = (ROOT / "components/inverter_manager/inverter_profiles.c").read_text(encoding="utf-8")
STORE_H = (ROOT / "components/inverter_manager/include/inverter_profile_store.h").read_text(encoding="utf-8")
STORE_C = (ROOT / "components/inverter_manager/inverter_profile_store.c").read_text(encoding="utf-8")
MANAGER = (ROOT / "components/inverter_manager/inverter_manager.c").read_text(encoding="utf-8")
GATE_H = (ROOT / "components/commissioning_gate/include/commissioning_gate.h").read_text(encoding="utf-8")
GATE_C = (ROOT / "components/commissioning_gate/commissioning_gate.c").read_text(encoding="utf-8")
STATUS_API = (ROOT / "components/web_server/solar_grid_status_api.c").read_text(encoding="utf-8")
GATE_API = (ROOT / "components/web_server/commissioning_gate_api.c").read_text(encoding="utf-8")
ASSIGN_API = (ROOT / "components/web_server/inverter_profile_api.c").read_text(encoding="utf-8")
CONTROL = (ROOT / "components/control_engine/control_engine.c").read_text(encoding="utf-8")

failures = []


def require(condition, message):
    if not condition:
        failures.append(message)


# ---------------------------------------------------------------------------
# The permission gate itself
# ---------------------------------------------------------------------------

# FORBIDDEN must be the zero value so uninitialised state denies a write.
require(re.search(r"INVERTER_WRITE_FORBIDDEN\s*=\s*0", PROFILES_H) is not None,
        "INVERTER_WRITE_FORBIDDEN must be explicitly 0 so zeroed state denies the write")

# The production predicate must remain the ONLY route to production authority,
# and must still demand production approval and a non-simulator profile.
production = re.search(r"bool inverter_profile_allows_write\(.*?\n\{(.*?)\n\}", PROFILES_C, re.DOTALL)
require(production is not None, "inverter_profile_allows_write must still exist")
if production:
    body = production.group(1)
    require("!profile->simulator_only" in body,
            "production write authority must still exclude simulator-only profiles")
    require("INVERTER_PROFILE_QUALIFICATION_PRODUCTION_APPROVED" in body,
            "production write authority must still require production approval")
    require("has_power_limit_readback" in body,
            "production write authority must still require a readback register")
    require("lab" not in body.lower(),
            "the production predicate must not consult any lab declaration")

permission = re.search(r"inverter_write_permission_t inverter_profile_write_permission\(.*?\n\{(.*?)\n\}",
                       PROFILES_C, re.DOTALL)
require(permission is not None, "inverter_profile_write_permission must exist")
if permission:
    body = permission.group(1)
    require("if (!profile) return INVERTER_WRITE_FORBIDDEN" in body,
            "a NULL profile must be forbidden")
    # Readback is mandatory in BOTH modes; the check must precede any grant.
    require("has_power_limit_readback" in body,
            "a readback register must be required before any write authority is granted")
    readback_at = body.find("has_power_limit_readback")
    lab_at = body.find("declared_lab_target")
    require(readback_at != -1 and lab_at != -1 and readback_at < lab_at,
            "the readback requirement must be checked BEFORE lab authority is granted, "
            "or a lab target with no readback would be commandable")
    require("inverter_profile_allows_write(profile)" in body,
            "production authority must be decided by the production predicate, not re-derived")


# ---------------------------------------------------------------------------
# The declaration is persisted safely
# ---------------------------------------------------------------------------

require("inverter_profile_store_lab_target_get" in STORE_H and
        "inverter_profile_store_lab_target_set" in STORE_H,
        "the lab-target declaration must be persisted through the profile store")

# Store version 2 must keep version 1 as a byte-exact prefix and migrate it, so a
# commissioned unit does not lose its profile assignments on upgrade.
require("PROFILE_STORE_VERSION 2" in STORE_C, "profile store must be at version 2")
require("legacy_profile_store_blob_v1_t" in STORE_C,
        "the version 1 layout must be frozen so a stored blob can still be identified")
require("_Static_assert" in STORE_C,
        "the prefix and size relationship between store versions must be asserted")
require(re.search(r"lab_target\[APP_MAX_INVERTERS\]", STORE_C) is not None,
        "the declaration must be stored per inverter, not globally")

# Migration must not silently declare an existing inverter a lab target.
require("no inverter is declared a lab target" in STORE_C,
        "migration must default every inverter to NOT a lab target and say so")

# Changing the declaration must disable automatic control, like a profile change.
require("commit_and_disable_control" in STORE_C,
        "declaring or revoking a lab target must disable automatic control")
require(STORE_C.count("commit_and_disable_control(&next)") >= 2,
        "both the profile setter and the lab-target setter must go through the "
        "control-disabling commit path")


# ---------------------------------------------------------------------------
# The runtime honours it, and fails closed
# ---------------------------------------------------------------------------

require("inverter_profile_write_permission" in MANAGER,
        "the runtime must decide write authority through the permission gate")
require(re.search(r"if \(inverter_profile_store_lab_target_get\([^)]*\) != ESP_OK\) lab_target = false",
                  MANAGER) is not None,
        "an unreadable lab declaration must fail closed to 'not a lab target'")
require("runtime->permission != INVERTER_WRITE_FORBIDDEN" in MANAGER,
        "write_allowed must be derived from the permission so they cannot disagree")
# Lab commanding must be loud in the log, every start.
require("is not production control" in MANAGER,
        "a lab-only command path must be logged as not being production control")


# ---------------------------------------------------------------------------
# Lab counts are never merged into qualified counts
# ---------------------------------------------------------------------------

require("lab_only_count" in MANAGER,
        "lab-only inverters must be counted separately from write-qualified ones")
require(re.search(r"write_qualified_count\s*\+\+[^;]*;\s*\n\s*break", MANAGER) is not None or
        "case INVERTER_WRITE_PRODUCTION:" in MANAGER,
        "only production permission may increment the write-qualified count")

# The gate combines them in exactly one place, and reports scope.
require("lab_only_inverter_count" in GATE_H, "the gate must receive the lab-only count")
require("COMMISSIONING_SCOPE_NONE = 0" in GATE_H,
        "COMMISSIONING_SCOPE_NONE must be 0 so a zeroed status authorises nothing")
require("commissioning_scope_t scope" in GATE_H,
        "the commissioning verdict must carry a scope")
require(re.search(r"lab_only_inverter_count\s*>\s*0U", GATE_C) is not None,
        "any lab-only inverter must force the whole verdict to LAB scope")
require(re.search(r"if \(!status\.commissioned\)\s*\{\s*status\.scope = COMMISSIONING_SCOPE_NONE",
                  GATE_C) is not None,
        "an uncommissioned gate must authorise nothing")


# ---------------------------------------------------------------------------
# Scope is visible wherever the verdict is
# ---------------------------------------------------------------------------

require("commissioning_scope" in CONTROL,
        "the control engine must publish the commissioning scope in its status")

for name, text, label in [
    ("solar_grid_status_api.c", STATUS_API, "commissioning_scope"),
    ("commissioning_gate_api.c", GATE_API, "scope"),
]:
    require(label in text and "lab_simulator_mode" in text,
            f"{name} must publish the commissioning scope and lab_simulator_mode "
            "alongside the commissioned verdict")

# A client must not be able to read "commissioned" without the scope being there
# too, so neither may be emitted conditionally on the other.
require(STATUS_API.index('"commissioned"') < STATUS_API.index('"commissioning_scope"'),
        "commissioning_scope must be published immediately after commissioned")
require(re.search(r'cJSON_AddStringToObject\(root, "commissioning_scope"', STATUS_API) is not None,
        "commissioning_scope must be published unconditionally, not inside a branch")

# The assignment endpoint must report the STORED declaration, not the request.
require("Report the stored declaration" in ASSIGN_API,
        "the assignment response must report the persisted declaration, not the requested one")
require('"lab_target"' in ASSIGN_API,
        "the assignment endpoint must report the lab-target declaration")


# ---------------------------------------------------------------------------
# Simulator profiles stay unpromotable
# ---------------------------------------------------------------------------

for match in re.finditer(r'\.id = "(soltrix\.sim\.[^"]+)"(.*?)(?=\n    \{|\n\};)',
                         PROFILES_C, re.DOTALL):
    profile_id, body = match.group(1), match.group(2)
    require(".simulator_only = true" in body,
            f"{profile_id} must remain simulator_only so it can never reach production")
    require("PRODUCTION_APPROVED" not in body,
            f"{profile_id} must never be production-approved")

# The measured lab profile must say it is measured against a simulator and not a
# manufacturer manual, so it is never cited as manufacturer evidence.
v3 = re.search(r'\.id = "soltrix\.sim\.huawei\.v3"(.*?)(?=\n    \{)', PROFILES_C, re.DOTALL)
require(v3 is not None, "the measured lab profile soltrix.sim.huawei.v3 must exist")
if v3:
    require("not a manufacturer manual" in v3.group(1),
            "the lab profile must state that it is not manufacturer evidence")
    # Device settle time must be declared, because the default would misjudge it.
    require("power_limit_settle_ms" in v3.group(1),
            "the lab profile must declare its own settle window: the simulator defers "
            "an applied setpoint and the firmware default would report a false mismatch")


# ---------------------------------------------------------------------------
# Device settle window cannot strand an unconfirmed setpoint
# ---------------------------------------------------------------------------

require("power_limit_settle_ms" in PROFILES_H,
        "the profile must be able to declare a device settle window")
require(re.search(r"settle_ms >= \(uint32_t\)INVERTER_CONFIRMATION_DEADLINE_MS", MANAGER) is not None,
        "a profile settle window must be clamped below the confirmation deadline, or a "
        "disagreement would stay pending forever and an unconfirmed setpoint would stand")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    sys.exit(1)

print("lab-simulator write gate source contract passed "
      "(production authority unreachable by declaration, readback mandatory in both modes, "
      "lab counts never merged, scope published with every verdict)")
