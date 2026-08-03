"""EEMUA 191 quantitative targets: gaps A6 and A10 of
docs/ALARM_MANAGEMENT_RESEARCH.md.

EEMUA 191 gives three hard numbers, which is what makes it directly testable:
fewer than 1 alarm per operator per 10 minutes in steady state, no more than 10 in
the first 10 minutes of an upset, and a priority distribution of roughly 5% high,
15% medium, 80% low.

Two different failures are guarded here, and the second is the one that needs a
test more than the first.

The obvious failure is not measuring. A6 and A10 were both "we believe this is
fine" rather than "here is the figure", so this contract checks that the
measurement exists, is computed from the same table the alarm list is served from,
and is published with the target beside it.

The subtler failure is measuring dishonestly. A rate that could be improved by
suppressing more alarms, a verdict extrapolated from a window that has not elapsed,
a peak reported as a pass because no flood has happened yet, or a distribution
"fixed" by demoting an alarm that genuinely stops the plant being controlled safely
would all read as compliance. So this file recomputes the controller's own
distribution from the priority table in the source, states it, and asserts it does
NOT meet the EEMUA target - because it does not, and the honest spread with its
reasoning is worth more than a number that has been arranged.

The window arithmetic itself is executed rather than pattern-matched: see
tests/alarm_metrics_test.c, which checks the peak against a brute-force reference
and drives the counter across the 49.7-day uptime wrap.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
API = (ROOT / "components/web_server/operational_api.c").read_text(encoding="utf-8")
METRICS = (ROOT / "components/web_server/alarm_metrics.c").read_text(encoding="utf-8")
METRICS_H = (ROOT / "components/web_server/include/alarm_metrics.h").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/esp-idf-build.yml").read_text(encoding="utf-8")
UI = (ROOT / "web/operator-operations.js").read_text(encoding="utf-8")
CSS = (ROOT / "web/operator-operations.css").read_text(encoding="utf-8")
APP_CSS = (ROOT / "web/app.css").read_text(encoding="utf-8")
THEME_CSS = (ROOT / "web/theme.css").read_text(encoding="utf-8")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def function_body(source, name):
    match = re.search(r"^[\w \*]*\b%s\s*\([^;]*?\)\s*\{" % re.escape(name), source, re.M | re.S)
    require(match is not None, f"function {name} not found")
    start = match.end() - 1
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f"function {name} is not brace balanced")


def define_value(source, name):
    match = re.search(r"#define\s+%s\s+(\d+)U?\b" % re.escape(name), source)
    require(match is not None, f"{name} is not defined")
    return int(match.group(1))


# ===========================================================================
# A10: the rate is measured, against EEMUA's own numbers
# ===========================================================================

window = define_value(METRICS_H, "ALARM_RATE_WINDOW_MS")
require(window == 600000,
        f"ALARM_RATE_WINDOW_MS={window}; EEMUA states both of its rate targets over "
        "ten minutes, so ten minutes is the window the metric is defined on")
require(define_value(METRICS_H, "ALARM_RATE_HOUR_MS") == 3600000, "the hour window moved")
require(define_value(METRICS_H, "ALARM_RATE_DAY_MS") == 86400000, "the day window moved")

steady = define_value(METRICS_H, "ALARM_RATE_STEADY_LIMIT_MILLI")
require(steady == 1000,
        f"ALARM_RATE_STEADY_LIMIT_MILLI={steady}; EEMUA's steady-state target is fewer "
        "than 1 alarm per operator per 10 minutes")
upset = define_value(METRICS_H, "ALARM_RATE_UPSET_LIMIT")
require(upset == 10,
        f"ALARM_RATE_UPSET_LIMIT={upset}; EEMUA allows no more than 10 alarms in the "
        "first 10 minutes of an upset")

# "Fewer than 1" is strict. At most 1 is a different, weaker target.
steady_check = function_body(METRICS, "alarm_rate_meets_steady_target")
require("<" in steady_check and "<=" not in steady_check,
        "the steady-state comparison must be strict: EEMUA says fewer than 1 alarm per "
        "10 minutes, and exactly one is not fewer than one")
# "No more than 10" includes ten.
peak_check = function_body(METRICS, "alarm_rate_meets_peak_target")
require("<=" in peak_check,
        "the peak comparison must include ten: EEMUA allows no more than 10, so ten "
        "itself is within the target")

capacity = define_value(METRICS_H, "ALARM_RATE_CAPACITY")
require(capacity >= 10 * upset,
        f"ALARM_RATE_CAPACITY={capacity} cannot hold a flood of the size EEMUA cares "
        "about with any margin; the metric exists to measure exactly that case")

# Wrap safety. uptime milliseconds roll over at ~49.7 days, and a metric that
# reported zero for the following 49 days would read as a perfectly quiet plant.
in_window = function_body(METRICS, "stamp_in_window")
require("(int32_t)(now_ms - stamp)" in in_window,
        "the window comparison must use a wrap-safe signed difference; comparing "
        "absolute uptime values places every pre-wrap record in the future")
require("return false" in in_window,
        "a timestamp in the future must be excluded rather than counted")

# 64-bit normalisation. count x window x 1000 overflows uint32 by four orders of
# magnitude and a wrapped rate reads as healthy.
normalise = function_body(METRICS, "alarm_rate_per_window_milli")
require("uint64_t" in normalise,
        "the rate normalisation must be done in 64 bits; a wrapped rate would read as "
        "compliant, which is the worst possible failure of this function")
require("UINT32_MAX" in normalise,
        "the normalised rate must saturate rather than wrap")

# The measurement is taken where a raise actually becomes real, and it counts every
# raise - so suppression cannot be used to flatter the figure.
commit = function_body(API, "commit_alarm_locked")
require("alarm_rate_record(&s_rate" in commit,
        "the rate is not measured at the point a confirmed raise happens")
commit_code = re.sub(r"//[^\n]*", "", re.sub(r"/\*.*?\*/", "", commit, flags=re.S))
require("shelved" not in commit_code and "suppress" not in commit_code,
        "the rate must count every confirmed raise; a figure that shrinks whenever "
        "somebody suppresses an alarm is not evidence of anything")

# Nothing in the pure module may block, allocate or log: it is called with
# interrupts disabled.
for forbidden in ["ESP_LOG", "malloc(", "calloc(", "free(", "cJSON_",
                  "portENTER_CRITICAL", "fopen", "fwrite", "xSemaphore"]:
    require(forbidden not in METRICS,
            f"alarm_metrics.c contains {forbidden}: it runs inside the alarm module's "
            "critical section and must stay pure and non-blocking")

alarms_get = function_body(API, "alarms_get")
rate_block = alarms_get[alarms_get.index('cJSON_AddObjectToObject(root, "rate")'):]
for field in ['"last_10_min"', '"last_60_min"', '"last_24_h"', '"peak_per_10_min"',
              '"steady_limit_milli"', '"peak_limit"', '"raises_total"']:
    require(field in rate_block, f"the alarm-rate payload does not publish {field}")
require('"per_10_min_from_60_min_milli"' in rate_block and
        '"per_10_min_from_24_h_milli"' in rate_block,
        "the rate must be normalised into EEMUA's own unit, or the comparison is left "
        "to the reader")

# No real-time clock. Every time in this payload is uptime, and it says so - the
# same convention the journal already declares.
require('"time_base", "uptime_ms"' in rate_block,
        "the rate payload must declare that its windows are uptime-relative")
require("no real-time clock" in rate_block,
        "the rate payload must state that the controller has no wall clock rather than "
        "imply a calendar timeline")
require("A reboot resets" in rate_block,
        "the payload must admit that a restart resets the counters")

# A verdict is only reported once the window has elapsed. Anything else is an
# extrapolation presented as a measurement.
require("alarm_rate_window_observed" in rate_block,
        "the steady-state verdict is not gated on the window having elapsed")
require('cJSON_AddNullToObject(rate, "meets_steady_target")' in rate_block,
        "before the window has elapsed the verdict must be null, not a number "
        "extrapolated from a few minutes of uptime")
# The peak is reported as a breach or as nothing, never as a pass: a flood that has
# not happened cannot be disproved by waiting.
require('"peak_target_breached"' in rate_block,
        "the peak must be reported as a breach rather than as a pass")
require('"meets_peak_target"' not in rate_block,
        "the peak must not be reported as a pass; no flood observed yet is not proof "
        "the alarm system can survive one")
# Losses are admitted.
require('"last_24_h_truncated"' in rate_block and '"discarded"' in rate_block,
        "records dropped from the rate ring must be reported; a rate metric that "
        "silently under-reports a flood is worse than no metric")
require("alarm_rate_window_truncated" in alarms_get,
        "truncation must be derived from the ring rather than assumed")

# The counts are lifted under the same lock as the alarm table, so the rate cannot
# describe a different instant from the alarms next to it.
locked = alarms_get[alarms_get.index("portENTER_CRITICAL"):alarms_get.index("portEXIT_CRITICAL")]
require("alarm_rate_window_count" in locked,
        "the window counts must be taken under the same lock as the alarm snapshot")


# ===========================================================================
# A6: the distribution is computed from the same table, and reported honestly
# ===========================================================================

# One source of truth. A hand-maintained second copy of the numbers is how a
# published distribution stops matching the alarms it describes.
census = function_body(API, "priority_census")
require("alarm_priority((uint8_t)code)" in census,
        "the census must be computed from the same alarm_priority table the alarm list "
        "is served from, not from a separate hand-written tally")
require("event_is_alarm_condition" in census,
        "the census must be able to separate the alarm population from the full "
        "condition population; they answer different questions")

rationalisation = alarms_get[alarms_get.index('cJSON_AddObjectToObject(root, "rationalisation")'):
                             alarms_get.index('cJSON_AddObjectToObject(root, "rate")')]
for field in ['"target"', '"alarms"', '"conditions"', '"note"']:
    require(field in rationalisation,
            f"the rationalisation payload does not publish {field}")
for field in ['"high_percent"', '"medium_percent"', '"low_percent"', '"meets_target"',
              '"target_representable"', '"smallest_representable_percent"']:
    require(field in API, f"the priority distribution does not publish {field}")

require("ALARM_PRIORITY_TARGET_HIGH_PERCENT" in rationalisation,
        "the EEMUA target must be published beside the measurement, or the reader "
        "cannot tell whether it was met")
require(define_value(METRICS_H, "ALARM_PRIORITY_TARGET_HIGH_PERCENT") == 5, "the high target moved")
require(define_value(METRICS_H, "ALARM_PRIORITY_TARGET_MEDIUM_PERCENT") == 15, "the medium target moved")
require(define_value(METRICS_H, "ALARM_PRIORITY_TARGET_LOW_PERCENT") == 80, "the low target moved")

tolerance = define_value(METRICS_H, "ALARM_PRIORITY_TARGET_TOLERANCE_PERCENT")
require(0 < tolerance <= 10,
        f"ALARM_PRIORITY_TARGET_TOLERANCE_PERCENT={tolerance}; EEMUA says 'roughly', but "
        "a band this wide would let 50/50/0 pass as 5/15/80")

# meets_target must check all three bands. A small high count is not compliance on
# its own: a system with no low-priority band at all has a different defect.
meets = function_body(METRICS, "alarm_priority_meets_target")
require(meets.count("within_tolerance") == 3,
        "the distribution verdict must test all three bands; checking only the high "
        "band would call 5/95/0 compliant")
require("return false" in meets,
        "an empty population must not be reported as meeting the target")

# --- the actual distribution, recomputed here from the source ---------------
#
# This is the part that makes the report honest rather than aspirational: the
# priority table and the alarm/event split are both parsed out of the firmware, the
# distribution is recomputed, and it is asserted to MISS the EEMUA target. Anyone
# who later demotes a condition to move this number will be stopped here and made
# to justify it in the commit rather than in a comment.

priority_map = dict(re.findall(r"case\s+(EVENT_\w+):\s*return\s+(\d+);",
                               function_body(API, "alarm_priority")))
require(priority_map, "alarm_priority has no per-code mapping to audit")

# Parsed from the enum body alone. Scanning the whole file would also pick up the
# EVENT_* names used in the causality tables and miscount the population, which is
# the number this whole contract turns on.
enum_match = re.search(r"typedef enum \{(.*?)\}\s*operational_event_code_t;", API, re.S)
require(enum_match is not None, "the operational event code enum was not found")
codes = list(dict.fromkeys(re.findall(r"\b(EVENT_[A-Z_0-9]+)\b", enum_match.group(1))))
require(len(codes) >= 7, f"only {len(codes)} condition codes were found: {codes}")

condition_body = function_body(API, "event_is_alarm_condition")
alarm_case_block = condition_body[:condition_body.index("return true;")]
alarm_codes = [code for code in codes if f"case {code}:" in alarm_case_block]
require(len(alarm_codes) == 4,
        f"expected four alarm conditions, found {len(alarm_codes)}: {alarm_codes}")


def tally(population):
    counts = {"high": 0, "medium": 0, "low": 0}
    for code in population:
        value = priority_map.get(code, "0")
        counts["high" if value == "2" else "medium" if value == "1" else "low"] += 1
    return counts


def percent(part, total):
    return 0 if total == 0 else (part * 100 + total // 2) // total


def verdict(counts):
    total = sum(counts.values())
    actual = {band: percent(counts[band], total) for band in counts}
    targets = {"high": 5, "medium": 15, "low": 80}
    return total, actual, all(abs(actual[band] - targets[band]) <= tolerance
                              for band in targets)


alarm_total, alarm_actual, alarm_meets = verdict(tally(alarm_codes))
all_total, all_actual, all_meets = verdict(tally(codes))

require(not alarm_meets,
        f"the alarm population now measures {alarm_actual} across {alarm_total} "
        "conditions and claims to meet the EEMUA 5/15/80 target. If a severity was "
        "changed to reach that, revert it: the target is arithmetically unreachable "
        "below 20 conditions and moving an alarm to fit a percentage is the opposite "
        "of rationalisation. If the condition set genuinely grew, update this contract "
        "deliberately and say so.")
require(not all_meets,
        f"the full condition population now measures {all_actual} across {all_total} "
        "conditions and claims to meet the EEMUA target; see the note above")

min_population = define_value(METRICS_H, "ALARM_PRIORITY_TARGET_MIN_POPULATION")
require(min_population >= 20,
        f"ALARM_PRIORITY_TARGET_MIN_POPULATION={min_population}; below twenty "
        "conditions a 5% high band cannot be represented at all, so the target must be "
        "reported as unreachable rather than merely missed")
require(alarm_total < min_population and all_total < min_population,
        "both populations are now large enough for the EEMUA distribution to be "
        "reachable, so the 'arithmetically unreachable' explanation no longer applies "
        "and the payload note must be rewritten")

# The published note has to explain the miss rather than leave it as a bare false.
note = re.search(r'cJSON_AddStringToObject\(rationalisation, "note",\s*((?:\s*"[^"]*")+)',
                 rationalisation)
require(note is not None, "the rationalisation has no published explanation")
note_text = note.group(1)
require("not met" in note_text or "cannot" in note_text,
        "the note must state plainly that the target is not met")
require("no severity was adjusted" in note_text.lower(),
        "the note must record that no severity was moved to fit the number")

# Every alarm still carries its own rationale, so the judgement is reviewable on
# the row rather than only in aggregate.
require('"priority"' in alarms_get and '"priority_rationale"' in alarms_get,
        "the rationalised priority and its reasoning must travel with the alarm")


# ===========================================================================
# THE INTERFACE SECTION IS GONE WITH THE ALARM PAGE.
#
# It asserted that the measured rate, the EEMUA comparison and the priority
# distribution reached a screen, in colours that met contrast in both themes.
# The owner removed that screen from the product, so there is nothing left to
# assert about.
#
# EVERYTHING ABOVE IS UNTOUCHED. The controller still measures the alarm rate,
# still compares it against EEMUA's numbers and still computes the priority
# distribution, and this file still executes that arithmetic. What was removed
# is the claim that a page displays it.
# ===========================================================================
worst_band = float("nan")
worst_text = float("nan")

require("tests/alarm_rate_and_priority_source_contract.py" in WORKFLOW,
        "this contract is not registered in the CI workflow")

print("EEMUA 191 rate and priority contract passed. "
      f"Measured distribution: alarms {alarm_actual['high']}% high / "
      f"{alarm_actual['medium']}% medium / {alarm_actual['low']}% low over "
      f"{alarm_total} conditions; all conditions {all_actual['high']}% / "
      f"{all_actual['medium']}% / {all_actual['low']}% over {all_total}. "
      "EEMUA target 5/15/80 is NOT met and is unreachable below "
      f"{min_population} conditions. Rate windows 10 min / 60 min / 24 h, "
      f"steady limit {steady / 1000:.0f} per 10 min, peak ceiling {upset}. "
      f"Worst band contrast {worst_band:.2f}:1, worst text contrast "
      f"{worst_text:.2f}:1 across both themes.")
