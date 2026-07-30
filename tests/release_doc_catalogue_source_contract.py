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
import shutil
import subprocess
import sys
import tempfile

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

# The authority for each profile is obtained by COMPILING AND RUNNING the real
# decision function over the real catalogue -- not by reimplementing the rule here.
#
# An earlier version of this test did reimplement it, and that was a mistake with
# teeth. When prerequisite-enable sequencing landed, the Python copy and the
# release table both still encoded the old rule, so they agreed with each other,
# disagreed with the firmware, and this test PASSED while the document was wrong
# about which inverters the controller would command. A mirror of a safety rule is
# another thing that can drift; asking the rule cannot.
PROBE = ROOT / "tests/support/profile_authority_probe.c"


def compiled_catalogue():
    """Compiles and runs the real decision function over the real catalogue."""
    gcc = shutil.which("gcc")
    if not gcc:
        raise AssertionError("gcc is required: this contract executes the real "
                             "write-permission rule rather than reimplementing it")
    with tempfile.TemporaryDirectory() as tmp:
        binary = pathlib.Path(tmp) / "probe"
        subprocess.run(
            [gcc, "-std=c11", "-Wall", "-Wextra", "-Werror",
             "-I", str(ROOT / "tests/support"),
             "-I", str(ROOT / "components/inverter_manager/include"),
             str(PROBE),
             str(ROOT / "components/inverter_manager/inverter_profiles.c"),
             str(ROOT / "components/inverter_manager/inverter_status.c"),
             "-lm", "-o", str(binary)],
            check=True, capture_output=True)
        out = subprocess.run([str(binary)], check=True, capture_output=True,
                             text=True).stdout
    result = {}
    for line in out.splitlines():
        if not line.strip():
            continue
        pid, qualification, authority, production = line.split("	")
        result[pid] = {
            "qualification": qualification.strip().lower(),
            "authority": authority.strip(),
            "production": production.strip() == "1",
        }
    return result


catalogue = compiled_catalogue()

# Human-readable authority, matching the wording used in the release table.
AUTHORITY_TEXT = {
    "forbidden": "forbidden",
    "lab_simulator_only": "lab only",
    "production": "production",
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

    # What the FIRMWARE actually decided, executed above, not inferred here.
    expected = AUTHORITY_TEXT.get(facts["authority"])
    require(expected is not None,
            f"{profile_id}: unrecognised authority '{facts['authority']}' from the "
            "firmware; this table needs a wording for it")
    if expected is not None:
        require(expected in row["authority"],
                f"{profile_id}: the release table says lab authority "
                f"'{row['authority']}' but the firmware decides '{expected}'")

    # Qualification must match too, so a profile cannot be quietly promoted in code
    # while the document still describes it at the old level.
    require(facts["qualification"] in row["qualification"],
            f"{profile_id}: the release table says qualification "
            f"'{row['qualification']}' but the catalogue says '{facts['qualification']}'")

    # No profile may be described as production-qualified while none is.
    require("production" not in row["authority"],
            f"{profile_id}: the release table claims production authority")
    require(not facts["production"],
            f"{profile_id} passes the production write gate; the release document's "
            "central claim that no profile is production qualified would be false")

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
