#!/usr/bin/env python3
"""Generator safety figures may be PROPOSED to an engineer, never imposed on one.

Two figures on the generator form are conventionally derived from the rating:
minimum loading (30 percent, the point below which a diesel set wet-stacks) and
the reverse-power margin (5 percent of rating, specified by the product owner).
Entering a rating fills them in.

Three properties make that safe rather than dangerous, and none is obvious from
reading the code that does it.

1. THE PROPOSAL LIVES IN THE FORM, NOT IN THE FIRMWARE. What is stored is what
   was on screen when Save was pressed. A default applied inside the firmware
   would give a plant a safety figure no engineer ever saw, and there would be
   nothing on the page to disagree with.

2. IT NEVER OVERWRITES A TYPED VALUE. An engineer who has entered a figure has
   made a decision about that specific machine -- possibly from its manual,
   possibly because it is not a diesel at all. Editing the rating later must not
   silently revert it.

3. THE MARGIN IS A FRACTION OF RATING, NOT A FIXED kW. The risk scales with the
   machine: 25 kW of cushion is generous on a 500 kW set and most of a 50 kW one.
   A constant would be right for exactly one plant size.

Asserted against comment-stripped source, so a promise made only in a comment
cannot satisfy any of it.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

failures = []


def require(condition, message):
    if not condition:
        failures.append(message)


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"^\s*//[^\n]*", " ", text, flags=re.MULTILINE)


ui = strip_comments((ROOT / "web" / "solar-grid.js").read_text(encoding="utf-8", errors="replace"))

# --- The figures exist, and are what was specified --------------------------

loading = re.search(r"const DEFAULT_MINIMUM_LOADING_PERCENT\s*=\s*([0-9.]+)\s*;", ui)
require(loading is not None, "DEFAULT_MINIMUM_LOADING_PERCENT is not declared")
if loading is not None:
    require(
        float(loading.group(1)) == 30,
        f"the proposed minimum loading is {loading.group(1)}, not the specified 30 percent",
    )

margin = re.search(r"const DEFAULT_REVERSE_POWER_MARGIN_FRACTION\s*=\s*([0-9.]+)\s*;", ui)
require(margin is not None, "DEFAULT_REVERSE_POWER_MARGIN_FRACTION is not declared")
if margin is not None:
    require(
        abs(float(margin.group(1)) - 0.05) < 1e-9,
        f"the proposed reverse-power margin is {margin.group(1)} of rating, not the specified 5 percent",
    )
    # A FRACTION, not a kW constant. If this ever became "const
    # DEFAULT_REVERSE_POWER_MARGIN_KW", it would be right for one plant size.
    require(
        "DEFAULT_REVERSE_POWER_MARGIN_KW" not in ui,
        "the reverse-power margin is proposed as a fixed kW figure; the risk scales "
        "with the machine, so it must be a fraction of rating",
    )

# --- It is derived from the rating -----------------------------------------

propose = re.search(r"const proposeFrom = \(\) => \{(.*?)\n        \};", ui, re.DOTALL)
require(propose is not None, "the proposal function could not be located")
if propose is not None:
    body = propose.group(1)
    require(
        "DEFAULT_REVERSE_POWER_MARGIN_FRACTION" in body and "rated *" in body,
        "the reverse-power margin is not computed from the entered rating",
    )
    # 2. Never overwrites. Both writes must be guarded on the field being empty
    # or zero.
    require(
        len(re.findall(r"!\(Number\([a-z]+\.value\) > 0\)", body)) >= 2,
        "the proposal is not guarded on the field being empty; editing the rating "
        "would silently revert a figure the engineer typed",
    )
    require(
        "rated <= 0" in body or "!(rated > 0)" in body,
        "a zero or missing rating still proposes figures, which would commission a "
        "minimum loading against no machine",
    )

# --- The engineer is told it happened --------------------------------------

require(
    re.search(r"engine\$\{slot\}Proposed", ui) is not None
    and "Filled from the rating" in ui,
    "nothing on screen says the figures were filled in, so an engineer cannot tell "
    "a proposal from something they entered",
)

# --- And the firmware still applies no default of its own -------------------

# The API stores what it is sent. A second, invisible default in the firmware
# would mean the stored value could differ from the form, and the form is what
# the engineer agreed to.
api = strip_comments(
    (ROOT / "components" / "web_server" / "solar_grid_api.c").read_text(
        encoding="utf-8", errors="replace"
    )
)
for pattern, what in (
    (r"minimum_loading_percent\s*=\s*30", "a 30 percent minimum loading"),
    (r"reverse_power_margin_kw\s*=\s*[^;]*0\.05", "a 5 percent reverse-power margin"),
):
    require(
        re.search(pattern, api) is None,
        f"solar_grid_api.c applies {what} of its own; the proposal belongs in the "
        f"form so that what is stored is what the engineer saw and agreed to",
    )

if failures:
    print("Generator commissioning defaults contract FAILED:")
    for failure in failures:
        print(f"  - {failure}")
    sys.exit(1)

print(
    "Generator commissioning defaults contract passed (30 percent and 5 percent of "
    "rating, proposed in the form, never over a typed value, no hidden firmware default)"
)
