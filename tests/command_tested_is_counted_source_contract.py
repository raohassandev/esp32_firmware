#!/usr/bin/env python3
"""The operator projection must count confirmations, not assert zero.

The operator view of /api/inverters printed `command_tested: 0` and
`last_write_ok: 0` as literals. On a plant whose setpoint the machine confirmed
on every pass -- verdict CONFIRMED on setpoint readback, production tracking the
limit -- the one screen an owner looks at still said no command had ever been
proven. A hardcoded zero is a claim about the plant, and this one was false.

The gate withholds how the firmware TALKS to a machine: register addresses,
function codes, raw words. Whether a setpoint was confirmed is a plant fact and
belongs to the operator, the same argument the file already makes for the
commanded percentage.

The second half is the diagnosis path. When these figures ARE zero the firmware
must say why, so an unreadable setpoint register and a write that never left the
controller are distinguishable from outside.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def read(relative):
    return (ROOT / relative).read_text(encoding="utf-8", errors="ignore")


def require(condition, message):
    if not condition:
        print(f"command tested counting contract FAILED: {message}")
        sys.exit(1)


def main():
    guard = read("components/web_server/engineering_guard.c")

    for field in ("command_tested", "last_write_ok"):
        require(not re.search(rf'AddNumberToObject\(summary, "{field}", 0\)', guard),
                f"the operator summary must not report {field} as a literal zero: "
                "that is an assertion about the plant, not a measurement of it")
        require(re.search(rf'AddNumberToObject\(summary, "{field}", {field}\)', guard),
                f"the operator summary must report the counted {field}")

    require("if (have && data.has_command) {" in guard,
            "the count must come from the same confirmation flag the engineering "
            "view uses, or the two views will disagree about the same plant")

    manager = read("components/inverter_manager/inverter_manager.c")

    require("power limit readback register" in manager,
            "an unreadable setpoint readback must be reported: it is the failure "
            "that drives command_tested to zero while the write itself lands, and "
            "it produced no message at all")
    require("runtime->readback_last_error != err" in manager,
            "the readback report must be rate limited to a change of outcome, or "
            "a permanently unreadable register costs one line per poll")

    require("write verdict:" in manager,
            "the confirmation verdict must be reported, so a zero count has a "
            "stated cause rather than being a silent number")
    require("!runtime->confirmation_logged" in manager,
            "routine re-confirmation after a periodic rewrite must stay silent -- "
            "measured at fifteen lines every thirty seconds on a steady plant, "
            "which would bury the failures this log exists to show")
    require("verdict.state != INVERTER_WRITE_CONFIRMED ||" in manager,
            "a verdict that is NOT a confirmation must always be reported, "
            "however often it repeats")

    print("Command tested counting contract passed (the operator summary counts "
          "confirmations instead of asserting zero, and a zero now has a stated "
          "cause without flooding the log)")


if __name__ == "__main__":
    main()
