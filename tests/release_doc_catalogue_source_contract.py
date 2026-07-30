#!/usr/bin/env python3
"""The release document's profile table must match the compiled catalogue.

This exists because the drift already happened: the table listed 7 profiles when
the catalogue held 9, silently omitting two -- including one added in the very
commit the document was assessing. A release decision made against an incomplete
list of what the controller can command is exactly the failure this document is
supposed to prevent.

The table is the human-readable summary of a safety property (which profiles can
be commanded, and why the rest cannot). Anything that can silently disagree with
the code is worse than no summary at all, so this test makes disagreement loud.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
PROFILES = (ROOT / "components/inverter_manager/inverter_profiles.c").read_text(encoding="utf-8")
DOC = (ROOT / "docs/RELEASE_READINESS.md").read_text(encoding="utf-8")

failures = []


def require(condition, message):
    if not condition:
        failures.append(message)


# ---------------------------------------------------------------------------
# The catalogue, read from the source of truth
# ---------------------------------------------------------------------------

SAFE_DEFAULT_ID = "custom.modbus-percent-v1"

catalogue = {}
for match in re.finditer(r"\.id = (SAFE_DEFAULT_PROFILE_ID|\"([^\"]+)\")", PROFILES):
    profile_id = SAFE_DEFAULT_ID if match.group(1) == "SAFE_DEFAULT_PROFILE_ID" else match.group(2)
    end = PROFILES.find("\n    },", match.start())
    block = PROFILES[match.start():end if end != -1 else len(PROFILES)]
    catalogue[profile_id] = {
        "simulator_only": ".simulator_only = true" in block,
        "prerequisite": ".requires_prerequisite_enable = true" in block,
        "command": ".has_power_limit = true" in block,
        "readback": ".has_power_limit_readback = true" in block,
        "production": "QUALIFICATION_PRODUCTION_APPROVED" in block,
        "flash_backed": ".command_register_is_flash_backed = true" in block,
        "has_command_rate": ".min_command_interval_ms =" in block,
    }

require(len(catalogue) >= 10, f"only {len(catalogue)} profiles parsed; the parser is probably wrong")

# ---------------------------------------------------------------------------
# The document's table
# ---------------------------------------------------------------------------

# Rows look like: | Manufacturer | `profile.id` | Qualification | authority | why |
documented = {}
for row in re.finditer(r"^\|[^|\n]*\|\s*`([^`]+)`\s*\|([^|\n]*)\|([^|\n]*)\|", DOC, re.MULTILINE):
    documented[row.group(1)] = {
        "qualification": row.group(2).strip().lower(),
        "authority": row.group(3).strip().lower(),
    }

missing = sorted(set(catalogue) - set(documented))
require(not missing,
        f"profiles exist in the catalogue but not in the release table: {missing}. "
        "A release decision must not be made against an incomplete list.")

extra = sorted(set(documented) - set(catalogue))
require(not extra,
        f"the release table lists profiles that no longer exist: {extra}")

# ---------------------------------------------------------------------------
# The stated authority must agree with what the code would decide
# ---------------------------------------------------------------------------

for profile_id, facts in catalogue.items():
    row = documented.get(profile_id)
    if not row:
        continue  # already reported above

    # Mirrors inverter_profile_write_permission() with a lab target declared:
    # a command and a readback register are both required, a prerequisite enable is
    # refused outright, and a flash-backed command register is refused unless the
    # manufacturer stated a permitted write rate. Nothing here is
    # production-approved.
    #
    # This mirror must be kept in step with the C rule. When it drifted, this test
    # is what reported it -- which is the point, but it also means a change to the
    # gate belongs here in the same commit.
    flash_hazard = facts["flash_backed"] and not facts["has_command_rate"]
    commandable_in_lab = (facts["command"] and facts["readback"]
                          and not facts["prerequisite"]
                          and not flash_hazard)
    expected = "lab only" if commandable_in_lab else "forbidden"
    require(expected in row["authority"],
            f"{profile_id}: the release table says lab authority "
            f"'{row['authority']}' but the catalogue implies '{expected}'")

    # No profile may be described as production-qualified while none is.
    require("production" not in row["authority"],
            f"{profile_id}: the release table claims production authority")
    require(not facts["production"],
            f"{profile_id} is marked production-approved in the catalogue; the "
            "release document's central claim that no profile is production "
            "qualified would be false")

# The document's headline count must be honest.
require(re.search(r"production-approved profiles:\s*\*\*0\*\*", DOC) is not None,
        "the release document must state that zero profiles are production-approved")

# Every refused profile must have a stated reason, so 'forbidden' is never
# unexplained in a document someone releases against.
for profile_id, facts in catalogue.items():
    row = documented.get(profile_id)
    if not row or "forbidden" not in row["authority"]:
        continue
    line = next((l for l in DOC.splitlines() if f"`{profile_id}`" in l), "")
    reason = line.rsplit("|", 2)[-2].strip() if line.count("|") >= 3 else ""
    require(len(reason) > 3,
            f"{profile_id} is refused but the release table gives no reason")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    sys.exit(1)

print(f"release document catalogue contract passed "
      f"({len(catalogue)} profiles, all listed with a matching authority and a reason)")
