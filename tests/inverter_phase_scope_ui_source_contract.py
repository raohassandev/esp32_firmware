"""The phase scope must be visible to the engineer choosing a profile.

This release phase is scoped to the EM500 meter and the Huawei SUN2000 inverter.
Twelve inverter profiles are parked (docs/RELEASE_READINESS.md section 4b.1) and
inverter_profile_write_permission() refuses every one of them, first and in both
modes. That refusal is already proven by tests/inverter_write_permission_test.c
and tests/phase_scope_source_contract.py, and nothing here weakens or restates it.

What this contract adds is the other half: an engineer must not be led into a
dead end. Selecting a parked brand in the profile picker used to look exactly
like a normal commissioning choice - the catalogue API published no scope field
at all, so a parked profile and a profile with no register map both reported
write_allowed:false and nothing else. The reasonable conclusion was that the
product does not support the brand.

So three properties, asserted over the source rather than over any one string:

  1. every parked profile carries a reason AND an unpark criterion, and no
     in-scope profile carries one (a reason on an in-scope profile is a lie);
  2. the controller publishes both, so the interface never restates product
     scope from memory;
  3. the picker keeps parked profiles OUT of the default list, keeps them IN the
     catalogue behind an explicit disclosure, marks them where they are listed,
     and refuses to assign one.

Every assertion runs against comment-stripped source: a promise in a comment is
not an implementation.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

FAILURES = []


def require(condition, message):
    if not condition:
        FAILURES.append(message)


def strip_c_comments(text):
    """Remove /* */ and // comments while preserving string literals."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
            continue
        if ch == "/" and i + 1 < n and text[i + 1] == "*":
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
            out.append(" ")
            continue
        if ch == "/" and i + 1 < n and text[i + 1] == "/":
            end = text.find("\n", i)
            i = n if end < 0 else end
            out.append(" ")
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def strip_js_comments(text):
    """Same, for JavaScript, preserving ' " and ` literals."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch in "'\"`":
            quote = ch
            j = i + 1
            while j < n and text[j] != quote:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
            continue
        if ch == "/" and i + 1 < n and text[i + 1] == "*":
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
            out.append(" ")
            continue
        if ch == "/" and i + 1 < n and text[i + 1] == "/":
            end = text.find("\n", i)
            i = n if end < 0 else end
            out.append(" ")
            continue
        out.append(ch)
        i += 1
    return "".join(out)


# The strippers are load-bearing: every assertion below is only as good as its
# refusal to read a comment. Prove they work before trusting them.
_C_PROBE = 'int keep = 1; /* 4242 */ const char *s = "/* 8181 */"; // 9393\n'
_C_DONE = strip_c_comments(_C_PROBE)
assert "4242" not in _C_DONE and "9393" not in _C_DONE, "C comment stripper is broken"
assert "8181" in _C_DONE and "int keep = 1;" in _C_DONE, "C stripper ate a string literal"
_JS_PROBE = "let keep = 1; /* 4242 */ const s = '/* 8181 */'; // 9393\n"
_JS_DONE = strip_js_comments(_JS_PROBE)
assert "4242" not in _JS_DONE and "9393" not in _JS_DONE, "JS comment stripper is broken"
assert "8181" in _JS_DONE and "let keep = 1;" in _JS_DONE, "JS stripper ate a string literal"


PROFILES_H = strip_c_comments((ROOT / "components/inverter_manager/include/inverter_profiles.h").read_text(encoding="utf-8"))
PROFILES_C = strip_c_comments((ROOT / "components/inverter_manager/inverter_profiles.c").read_text(encoding="utf-8"))
PROFILE_API = strip_c_comments((ROOT / "components/web_server/inverter_profile_api.c").read_text(encoding="utf-8"))
PICKER = strip_js_comments((ROOT / "web/inverter-profiles.js").read_text(encoding="utf-8"))
READINESS = (ROOT / "docs/RELEASE_READINESS.md").read_text(encoding="utf-8")


# ---------------------------------------------------------------- 1. the data

require("const char *deferred_reason;" in PROFILES_H,
        "inverter_profile_t carries no deferred_reason, so the catalogue cannot "
        "say why a profile is parked")

# Split the catalogue the same way tests/phase_scope_source_contract.py does, so
# the two contracts cannot disagree about what an entry is.
entries = re.split(r"\n        \.id = ", "\n" + PROFILES_C)[1:]
# Twelve, not sixteen: the four SolTrix simulator profiles were removed when the
# controller went to site. They were lab rigs, not manufacturer profiles, and the
# rule below is about never deleting a PARKED profile -- the record of why a real
# brand is not commandable. That record is intact.
require(len(entries) >= 12,
        f"the catalogue lost entries: {len(entries)} found, at least 12 expected. "
        "A parked profile is never deleted; deleting it destroys the record of why "
        "it is not commandable")

parked = []
in_scope = []
for entry in entries:
    identifier = entry.split(",", 1)[0].strip()
    (parked if ".deferred_this_phase = true" in entry else in_scope).append((identifier, entry))

require(len(parked) > 0, "no profile is parked, so this contract is testing nothing")
require(len(in_scope) > 0, "every profile is parked, so nothing is commissionable")

# A parked profile must say why, and must say what would change it. "Deferred"
# on its own sends an engineer looking for a document they do not know exists.
CRITERION_MARKERS = ("unparking needs", "scope widens", "phase scope to widen")
for identifier, entry in parked:
    body = entry.split(".deferred_reason", 1)
    require(len(body) == 2,
            f"{identifier} is parked but carries no deferred_reason")
    if len(body) != 2:
        continue
    reason = " ".join(re.findall(r'"((?:[^"\\]|\\.)*)"', body[1].split(",\n        .", 1)[0]))
    require(len(reason.split()) >= 12,
            f"{identifier} carries a deferred_reason of {len(reason.split())} words, "
            "which is too short to state both a reason and an unpark criterion")
    require(any(marker in reason.lower() for marker in CRITERION_MARKERS),
            f"{identifier} states no unpark criterion; the reason must say what "
            f"would make it available, not only that it is unavailable")

# And an in-scope profile must NOT carry one. A reason attached to a profile
# that is not parked is a statement the firmware does not act on.
for identifier, entry in in_scope:
    require(".deferred_reason" not in entry,
            f"{identifier} is in scope but carries a deferred_reason; a profile "
            "that is not parked must not claim to be")

# The reasons here and the table in the release doc are the same product
# decision, so every parked id must still appear in the doc.
for identifier, _ in parked:
    slug = identifier.strip('"')
    if slug.startswith("SAFE_DEFAULT"):
        continue
    require(slug in READINESS,
            f"{slug} is parked in firmware but is not recorded in "
            "docs/RELEASE_READINESS.md")


# ------------------------------------------------------------ 2. the API says so

require('cJSON_AddBoolToObject(item, "deferred_this_phase", profile->deferred_this_phase);' in PROFILE_API,
        "GET /api/inverter-profiles does not publish deferred_this_phase, so no "
        "interface can distinguish a parked profile from an unqualified one")
require('cJSON_AddStringToObject(item, "deferred_reason"' in PROFILE_API,
        "GET /api/inverter-profiles does not publish deferred_reason")

# Published, never enforced here. The refusal is the write gate's and must not be
# duplicated into the presentation layer, where it could drift.
assignment = PROFILE_API.split("profile_assignment_post", 1)
require(len(assignment) > 1, "profile_assignment_post is gone")
if len(assignment) > 1:
    require("deferred_this_phase" not in assignment[1],
            "the assignment handler now tests deferred_this_phase itself; the "
            "phase-scope refusal belongs to inverter_profile_write_permission() "
            "alone, so that unparking stays a single edit")


# ------------------------------------------------------------- 3. the picker

require("deferred_this_phase" in PICKER,
        "the profile picker never reads deferred_this_phase, so it cannot tell "
        "an engineer that a brand is out of scope for this phase")
require("deferred_reason" in PICKER,
        "the picker does not show the reason a profile is parked")

# Out of the default list...
require(re.search(r"filter\(\s*\(\s*profile\s*\)\s*=>\s*!isDeferred\(profile\)\s*\)", PICKER)
        or re.search(r"filter\([^)]*!\s*isDeferred", PICKER),
        "parked profiles are not filtered out of the default catalogue list")
# ...but reachable, behind a control the engineer operates deliberately.
require("inverterShowDeferred" in PICKER,
        "there is no disclosure for the deferred profiles, so they are silently "
        "dropped from the catalogue and an engineer cannot find out why their "
        "brand is missing")
require("showingDeferred" in PICKER and "visibleProfiles" in PICKER,
        "the deferred disclosure is not wired to what the picker lists")

# Marked where they are listed.
require("deferred this phase" in PICKER,
        "a parked profile is listed without being marked as parked")

# Non-applicable: the action that would assign one is refused.
require(re.search(r"apply\.disabled\s*=\s*[^;]*deferred", PICKER),
        "the Apply button is not disabled for a parked profile")
require(re.search(r"if\s*\(\s*isDeferred\(profile\)\s*\)\s*\{", PICKER),
        "applyProfile does not refuse a parked profile, so a stale button state "
        "or a keyboard activation could still start the assignment")

# The refusal must not be softened into advice.
for phrase in ("no action required", "safe to ignore", "will clear itself",
               "harmless", "should still work", "try it anyway"):
    require(phrase not in PICKER.lower(),
            f"the picker softens a phase-scope refusal with {phrase!r}")


if FAILURES:
    for failure in FAILURES:
        print(f"FAIL: {failure}")
    raise SystemExit(f"{len(FAILURES)} phase-scope UI contract failure(s)")

print(f"inverter phase scope UI contract passed "
      f"({len(parked)} parked, {len(in_scope)} in scope)")
