"""The Inverters page states the finding by default and keeps the reasoning.

The page was roughly ten screens of permanently visible prose. An engineer
standing at a plant needs two things by default: what the verdict is, and, if it
is not qualified, what evidence is missing. Everything explaining WHY belongs one
level down.

The danger in "cut the text" is that the text gets deleted. It must not: the
qualification and write-permission explanations are why this product refuses to
command equipment on insufficient evidence, and an engineer must be able to reach
the full reasoning. So this contract asserts BOTH directions at once -

    the reasoning still exists, verbatim, and it is not in the default view.

Assertions run against comment-stripped source; a promise in a comment is not an
implementation.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

FAILURES = []


def require(condition, message):
    if not condition:
        FAILURES.append(message)


def strip_js_comments(text):
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


_PROBE = "let keep = 1; /* 4242 */ const s = '/* 8181 */'; // 9393\n"
_DONE = strip_js_comments(_PROBE)
assert "4242" not in _DONE and "9393" not in _DONE, "JS comment stripper is broken"
assert "8181" in _DONE and "let keep = 1;" in _DONE, "JS stripper ate a string literal"


INDEX = (ROOT / "web/index.html").read_text(encoding="utf-8")
APP_RAW = (ROOT / "web/app.js").read_text(encoding="utf-8")
APP = strip_js_comments(APP_RAW)
TELEMETRY = strip_js_comments((ROOT / "web/inverter-telemetry.js").read_text(encoding="utf-8"))


def inverters_section(html):
    start = html.index('<section class="page" data-page="inverters"')
    depth = 0
    for match in re.finditer(r"</?section\b", html[start:]):
        depth += -1 if html[start + match.start() + 1] == "/" else 1
        if depth == 0:
            return html[start:start + match.end()]
    raise AssertionError("the inverters section is not closed")


SECTION = inverters_section(INDEX)


def details_blocks(fragment):
    """Every <details>...</details> subtree, brace-matched on the tag."""
    blocks = []
    for match in re.finditer(r"<details\b", fragment):
        depth = 0
        for tag in re.finditer(r"</?details\b", fragment[match.start():]):
            depth += -1 if fragment[match.start() + tag.start() + 1] == "/" else 1
            if depth == 0:
                blocks.append(fragment[match.start():match.start() + tag.end() + len("></details>")])
                break
    return blocks


BLOCKS = details_blocks(SECTION)
DRAWER_TEXT = "\n".join(BLOCKS)


# ------------------------------------------------- 1. the substance is still here

# The exact sentences that carry the product's refusal to overclaim. Deleting any
# of them is the failure mode this contract exists to prevent; they are matched
# verbatim, wherever they now live.
SUBSTANCE = [
    "Confirmed does not mean the limit was demonstrated.",
    "echo of a stored command",
    "proves acceptance and nothing more",
    "adjustment coefficient",
    "shown with the evidence it rests on",
    "A setpoint can read back perfectly and still be ignored.",
    "still echoes it back",
    "full output",
]
for sentence in SUBSTANCE:
    require(sentence in SECTION,
            f"the explanation {sentence!r} has been deleted from the Inverters page. "
            "This reasoning may be moved one level down; it may not be removed")


# --------------------------------------------- 2. and it is not in the default view

require(len(BLOCKS) >= 2,
        f"only {len(BLOCKS)} disclosure(s) on the Inverters page; the setpoint "
        "confirmation explanations are still permanently visible")

# The long explanations sit inside a drawer...
for sentence in ("echo of a stored command", "proves acceptance and nothing more",
                 "adjustment coefficient", "still echoes it back"):
    require(sentence in DRAWER_TEXT,
            f"{sentence!r} is still in the default view; it is reasoning, not a finding")

# ...while the warning itself stays on screen as the summary, so closing the
# drawer removes the explanation and never the caveat.
summaries = " ".join(re.findall(r"<summary\b[^>]*>(.*?)</summary>", DRAWER_TEXT, re.S))
for headline in ("Confirmed does not mean the limit was demonstrated.",
                 "A setpoint can read back perfectly and still be ignored."):
    require(headline in summaries,
            f"{headline!r} is not the summary of its drawer; a closed drawer would "
            "hide the warning and not only the reasoning behind it")

# The live verdicts must NOT be inside a drawer. They are the reason to look.
for element_id in ('id="confirmProvenance"', 'id="prereqFleetState"',
                   'id="confirmFleetDetail"', 'id="confirmFleetBadge"'):
    require(element_id in SECTION, f"{element_id} is gone from the Inverters page")
    require(element_id not in DRAWER_TEXT,
            f"{element_id} is the live verdict and must not be behind a disclosure")


# ------------------------------------- 3. the injected prose is drawer-only too

# Most of the page's words were never in index.html: three frozen vocabularies in
# app.js supply a paragraph per state, rendered into the legends and into every
# inverter row. Assert structurally that those paragraphs can only reach a
# <details>: each is read exclusively inside a function whose every call site is
# an argument to disclosure().

def function_bodies(source):
    bodies = {}
    for match in re.finditer(r"function\s+([A-Za-z_$][\w$]*)\s*\(", source):
        brace = source.find("{", match.end())
        if brace < 0:
            continue
        depth = 0
        for i in range(brace, len(source)):
            if source[i] == "{":
                depth += 1
            elif source[i] == "}":
                depth -= 1
                if depth == 0:
                    bodies[match.group(1)] = (match.start(), i + 1)
                    break
    return bodies


def call_ranges(source, name):
    """Argument-list spans of every `name(` call, paren-matched."""
    spans = []
    for match in re.finditer(rf"\b{re.escape(name)}\s*\(", source):
        if source[max(0, match.start() - 12):match.start()].rstrip().endswith("function"):
            continue
        depth = 1
        i = match.end()
        while i < len(source) and depth:
            if source[i] == "(":
                depth += 1
            elif source[i] == ")":
                depth -= 1
            i += 1
        spans.append((match.start(), i))
    return spans


BODIES = function_bodies(APP)
require("disclosure" in BODIES, "app.js has no disclosure() helper")

DISCLOSURE_SPANS = [span for span in call_ranges(APP, "disclosure")
                    if not (BODIES["disclosure"][0] <= span[0] < BODIES["disclosure"][1])]
require(len(DISCLOSURE_SPANS) >= 2,
        "disclosure() is defined but barely used; the reasoning is still in the "
        "default view")


def inside(spans, position):
    return any(start <= position < end for start, end in spans)


# Fixpoint: a function renders only into a drawer when every call site of it is
# inside a disclosure() argument list, or inside another such function.
drawer_only = set()
for _ in range(8):
    grew = False
    for name, (start, end) in BODIES.items():
        if name in drawer_only:
            continue
        sites = [span[0] for span in call_ranges(APP, name)
                 if not (start <= span[0] < end)]
        if not sites:
            continue
        bound = [BODIES[other] for other in drawer_only]
        if all(inside(DISCLOSURE_SPANS, at) or inside(bound, at) for at in sites):
            drawer_only.add(name)
            grew = True
    if not grew:
        break

meaning_reads = [match.start() for match in re.finditer(r"\.meaning\b", APP)]
require(len(meaning_reads) >= 3,
        "the state vocabularies are no longer rendered; their explanations have "
        "been dropped rather than moved")
bound_bodies = [BODIES[name] for name in drawer_only]
stray = [at for at in meaning_reads if not inside(bound_bodies, at)
         and not inside(DISCLOSURE_SPANS, at)]
require(not stray,
        f"{len(stray)} of {len(meaning_reads)} reads of a state vocabulary's "
        "explanation render outside a disclosure, so those paragraphs are still "
        "permanently visible")

# The per-row explanation blocks belong to the drawer as well.
for name in ("provenanceBlock", "prerequisiteBlock", "rowMeaningElement"):
    require(name in drawer_only,
            f"{name}() renders outside a disclosure; its paragraphs are back in "
            "the default view")


# -------------------------------------- 4. the finding replaces them by default

require("function rowGapText" in APP,
        "no per-inverter statement of what evidence is missing")
gap_start, gap_end = BODIES.get("rowGapText", (0, 0))
require(gap_end > gap_start, "rowGapText has no body")
gap_body = APP[gap_start:gap_end]
require(gap_body.count("Missing:") >= 5,
        "rowGapText names too few missing-evidence cases to cover the states an "
        "unqualified row can be in")
require("confirm-row-gap" in APP,
        "the missing-evidence line is not rendered into the row")
require("rowGapElement" not in drawer_only,
        "the missing-evidence line has been put behind a disclosure; it is the "
        "finding, not the reasoning")


# ---------------------------------------------- 5. tables, not stacked cards

require("device-table" in TELEMETRY and "<thead" not in TELEMETRY,
        "the inverter telemetry panel does not render a table")
require("createElement('tr')" in TELEMETRY or "node('tr'" in TELEMETRY,
        "the inverter telemetry panel still stacks one card per channel")
require("device-runtime-card" not in TELEMETRY,
        "the inverter telemetry panel still builds runtime cards")
require("table-scroll" in TELEMETRY,
        "the telemetry table is not in its own horizontal scroll container, so a "
        "narrow viewport would scroll the whole page sideways")


if FAILURES:
    for failure in FAILURES:
        print(f"FAIL: {failure}")
    raise SystemExit(f"{len(FAILURES)} inverters-page density contract failure(s)")

print(f"inverters page density contract passed "
      f"({len(BLOCKS)} disclosures, {len(drawer_only)} drawer-only renderers)")
