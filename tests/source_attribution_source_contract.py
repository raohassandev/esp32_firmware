#!/usr/bin/env python3
"""No screen may name the live source without asking the controller which it is.

WHAT HAPPENED. The controller resolved GENERATOR from the EM-500 tariff input --
Tariff 2, evidence good, shown correctly on the source-detection page -- and the
home page drew the measured 347.3 kW under GRID, with the generator dimmed and
captioned "not running", while the plant ran on the generator.

The measurement was never wrong. Its NAME was. On a single-meter tariff plant one
meter measures whichever source is live: the number is identical and what it
means changes with the tariff input.

WHY IT WAS NOT ONE MISTAKE. Nine modules label that measurement and every one
called it "Grid" unconditionally, because /api/status did not publish the source
at all. There was nothing to consult. A tenth screen written next month would
have made the same assumption for the same reason.

So this contract does not check the nine. It checks that the FACT is published,
that there is one place the rule lives, and that the screens which draw the
source diagram consult it. Fixing nine call sites without that leaves the tenth
free to be wrong.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
WEB = ROOT / "web"
STATUS_API = ROOT / "components" / "web_server" / "web_api.c"
HELPER = WEB / "source-attribution.js"
FLOW = WEB / "operator-view.js"


def strip_c_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def strip_js_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"^\s*//[^\n]*", " ", text, flags=re.M)


def main():
    failures = []

    # 1. THE FACT IS PUBLISHED. Without this every reader is guessing.
    status = strip_c_comments(STATUS_API.read_text(encoding="utf-8", errors="replace"))
    if "source_detection_get_status" not in status:
        failures.append(
            "/api/status does not read the detected source. Every screen that "
            "names the live measurement is then guessing, and on a single-meter "
            "tariff plant it guesses wrong whenever the generator is running.")
    if '"attributed_to"' not in status:
        failures.append(
            "/api/status does not publish attributed_to. A screen needs one "
            "field saying which node the measurement belongs under, or each one "
            "derives the rule itself and they derive it differently.")

    # 2. AND IT ADMITS WHEN IT CANNOT SAY. "unknown" is a real answer: the meter
    #    may be answering perfectly while the SOURCE is unestablished, and a
    #    screen must not resolve that by picking the likelier one.
    if '"unknown"' not in status:
        failures.append(
            "/api/status never answers \"unknown\" for the source. Unconfigured, "
            "stale, conflicting and fail-closed must not be reported as grid.")
    # ON THE VERDICT EXPRESSION, not on the file.
    #
    # Checking that the word "evidence_fresh" appears somewhere passes even when
    # the guard has been removed from the decision, because the same word is also
    # a published field two lines above. The guards have to be IN the expression
    # that decides what may be claimed.
    verdict = re.search(r"const bool trustworthy\s*=(.*?);", status, re.S)
    if not verdict:
        failures.append(
            "the attribution has no single expression deciding whether the "
            "source may be named. Without one there is nothing to check and "
            "nothing for a reader to find.")
    else:
        decision = verdict.group(1)
        for guard in ("configured", "evidence_fresh", "conflict"):
            if guard not in decision:
                failures.append(
                    f"the source may be named without checking {guard}. A stale "
                    f"tariff read that last said generator is not a statement "
                    f"about the plant now, and a conflict is not a source.")

    # 3. ONE PLACE THE RULE LIVES.
    if not HELPER.exists():
        failures.append(
            "web/source-attribution.js is missing. Nine modules label this "
            "measurement; nine copies of the rule is nine chances to derive it "
            "differently, and a tenth screen has nothing to consult.")
    else:
        helper = strip_js_comments(HELPER.read_text(encoding="utf-8", errors="replace"))
        if "attributed_to" not in helper:
            failures.append(
                "the attribution helper does not read the firmware's verdict. It "
                "must not re-derive the rule: the firmware is what the control "
                "loop acts on, and a screen reaching a different conclusion from "
                "the controller regulating the plant would be the more "
                "convincing of the two.")
        if "generator" not in helper or "reverse power" not in helper:
            failures.append(
                "the helper does not distinguish the generator vocabulary. "
                "Negative power is ordinary export on a grid and REVERSE POWER "
                "on a generator -- a fault. The same sign must not read the same "
                "way on both.")

    # 4. THE DIAGRAM CONSULTS IT. This is the screen the defect appeared on.
    flow = strip_js_comments(FLOW.read_text(encoding="utf-8", errors="replace"))
    if "AutomatrixSource" not in flow:
        failures.append(
            "the power-flow diagram does not consult the attribution. It drew "
            "347.3 kW under GRID while the controller had resolved GENERATOR.")

    # The specific regression: the generator node asserted "not running" whenever
    # no generator measurement existed, which on a tariff plant is also what an
    # unestablished source looks like.
    if "not running" in flow and "attribution.node === 'grid'" not in flow:
        failures.append(
            "the diagram still asserts \"not running\" without checking that the "
            "controller actually knows the plant is on the grid. On a tariff "
            "plant an absent generator reading and an unestablished source look "
            "identical.")

    if failures:
        print("Source attribution contract FAILED:")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    print("Source attribution contract passed (the source is published with its "
          "confidence, one helper owns the rule, and the diagram asks)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
