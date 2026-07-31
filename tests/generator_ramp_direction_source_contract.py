#!/usr/bin/env python3
"""The urgency ramp boost must accelerate PV DOWNWARD and nowhere else.

Below its minimum loading a diesel generator wet-stacks and heads toward reverse
power. The only lever this controller has to raise generator load is to LOWER
PV, so the boost belongs on the PV ramp-DOWN rate.

Put it on ramp-UP and the controller raises PV faster while the machine is
already starved -- it accelerates the plant INTO the fault it exists to prevent,
and it does so silently, because a faster ramp looks like responsiveness. There
is no runtime symptom that distinguishes "correctly hurrying down" from
"dangerously hurrying up" other than which way the numbers move.

tests/generator_urgent_ramp_test.c executes the threshold rule itself. What that
test cannot see is which of the two ramp rates the multiplier is applied to,
because that is wiring in the control loop rather than behaviour of the pure
function. This contract asserts the wiring, against comment-stripped source.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ENGINE = ROOT / "components" / "control_engine" / "control_engine.c"
LIMIT_H = ROOT / "components" / "control_engine" / "include" / "generator_fleet_limit.h"

failures = []


def require(condition, message):
    if not condition:
        failures.append(message)


def strip_comments(text):
    return re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)


engine = strip_comments(ENGINE.read_text(encoding="utf-8", errors="replace"))
header = LIMIT_H.read_text(encoding="utf-8", errors="replace")

# The multiplier is used exactly once. Two call sites would mean it reached both
# directions, which is the defect this file exists to prevent.
uses = len(re.findall(r"generator_urgent_ramp_multiplier\s*\(", engine))
require(
    uses == 1,
    f"generator_urgent_ramp_multiplier is applied {uses} times in the control loop; "
    f"exactly one application is correct, and more than one means it has reached "
    f"the ramp-up rate as well",
)

# Isolate each ramp field's initialiser and check which one carries it.
def initialiser(field):
    match = re.search(
        r"\." + field + r"\s*=\s*(.*?)(?=,\s*\n\s*\.[a-z_]+\s*=)", engine, re.DOTALL
    )
    return match.group(1) if match else None


down = initialiser("ramp_down_kw_per_second")
up = initialiser("ramp_up_kw_per_second")

require(down is not None, "the ramp_down_kw_per_second initialiser could not be located")
require(up is not None, "the ramp_up_kw_per_second initialiser could not be located")

if down is not None:
    require(
        "generator_urgent_ramp_multiplier" in down,
        "the urgency boost is not applied to the PV ramp-DOWN rate, so an "
        "underloaded generator recovers no faster than normal",
    )
if up is not None:
    require(
        "generator_urgent_ramp_multiplier" not in up,
        "THE URGENCY BOOST IS APPLIED TO THE PV RAMP-UP RATE. That accelerates PV "
        "into a starving generator: the opposite of the intent, and dangerous.",
    )

# The threshold must stay below the minimum-loading target it is hurrying
# towards. If it were raised to or above the target the boost would never switch
# off, and the plant would ramp at double rate permanently.
fraction = re.search(r"#define GENERATOR_URGENT_LOADING_FRACTION\s+([0-9.]+)f", header)
require(fraction is not None, "GENERATOR_URGENT_LOADING_FRACTION is not defined")
if fraction is not None:
    value = float(fraction.group(1))
    require(
        0.0 < value < 0.30,
        f"the urgency threshold is {value:.2f} of rating, which is not below the "
        f"0.30 default minimum loading; at or above the target the boost would "
        f"never switch off and the plant would ramp at double rate permanently",
    )

multiplier = re.search(r"#define GENERATOR_URGENT_RAMP_MULTIPLIER\s+([0-9.]+)f", header)
require(multiplier is not None, "GENERATOR_URGENT_RAMP_MULTIPLIER is not defined")
if multiplier is not None:
    require(
        float(multiplier.group(1)) > 1.0,
        "the urgency multiplier does not accelerate anything",
    )

# The load it is judged against must be the generator's own output, and the
# rating must be the ONLINE rating. Judging against the commissioned rating of
# machines that are not on the bus would use a denominator the evidence does not
# support -- the same error the fleet module refuses everywhere else.
#
# Read out of the ramp-down initialiser rather than by matching the call's own
# parentheses. An argument list containing fabsf(...) has a ')' followed by ','
# inside it, so a non-greedy match to the first such pair stops early and reads
# only part of the arguments -- which is how the first draft of this contract
# failed against correct code.
args = down if down is not None else ""
require(args != "", "the multiplier call site could not be read")
if args:
    require(
        "fleet_limit.online_rated_kw" in args,
        "the urgency threshold is not measured against the ONLINE generator rating",
    )
    require(
        "generator_carrying" in args and "fleet_limit.known" in args,
        "the urgency boost is not conditioned on a generator carrying the plant "
        "with a known running set",
    )

if failures:
    print("Generator ramp direction contract FAILED:")
    for failure in failures:
        print(f"  - {failure}")
    sys.exit(1)

print(
    "Generator ramp direction contract passed (boost on ramp-down only, one call "
    "site, threshold below the minimum-loading target, judged on the online rating)"
)
