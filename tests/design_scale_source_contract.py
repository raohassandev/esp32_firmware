#!/usr/bin/env python3
"""The interface must converge on one scale, and can never re-fragment.

An inventory of this stylesheet set found 313 separate font-size declarations,
217 paddings, 287 gaps and 156 border-radii spread across fifteen files, with six
:root blocks in three of them -- and seventeen JavaScript modules writing page
DOM. No individual value was wrong. What was wrong is that there was no shared
answer, so every module invented its own and no two screens agreed on how large a
heading is or how far a panel sits from its neighbour.

That is the whole of "the pages are not managed, the sizes and spacing are bad",
and it is why adjusting individual numbers never held: each fix was one more
private opinion added to a thousand.

So this contract does two things a style guide cannot.

RATCHET. It records how many RAW values exist today and refuses an increase. New
work must use the tokens; existing files are converted as they are touched. The
number only ever goes down, and every reduction is committed as a lower ceiling,
so progress is a fact in the repository rather than a claim in a report.

ONE SOURCE. The scale lives in exactly one :root, in web/app.css. A second
declaration of --fs-* or --sp-* anywhere is how a shared answer quietly becomes
two answers again.

A raw value is a declaration whose value contains no var(). Media queries and
one-off geometry are counted too, deliberately: an exception that is invisible to
the count is an exception nobody ever removes.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
WEB = ROOT / "web"

# The ceiling. LOWER THIS when values are converted; never raise it. Raising it
# is the one edit that defeats the whole mechanism, so it should look wrong in a
# diff -- which is why it is a single obvious number with this comment attached.
RAW_VALUE_CEILING = 1100

TRACKED = (
    "font-size", "padding", "padding-top", "padding-bottom",
    "padding-left", "padding-right", "margin", "gap", "border-radius",
)

failures = []


def require(condition, message):
    if not condition:
        failures.append(message)


stylesheets = sorted(WEB.glob("*.css"))
require(len(stylesheets) >= 5, "the stylesheet set could not be found")

combined = "".join(sheet.read_text(encoding="utf-8", errors="replace") for sheet in stylesheets)

# --- The ratchet -----------------------------------------------------------

per_property = {}
for prop in TRACKED:
    values = re.findall(prop + r"\s*:\s*([^;{}]+)", combined)
    per_property[prop] = len([value for value in values if "var(" not in value])

total = sum(per_property.values())
require(
    total <= RAW_VALUE_CEILING,
    f"raw style values rose to {total}, above the ceiling of {RAW_VALUE_CEILING}. "
    f"New rules must use the scale in web/app.css (--fs-*, --sp-*, --r-*). "
    f"Breakdown: {per_property}",
)

# --- One source of truth ---------------------------------------------------

# The scale is declared once. A second declaration is how one shared answer
# becomes two, which is the state this contract exists to leave behind.
for token in ("--fs-3", "--sp-4", "--r-2", "--tap"):
    declarations = [
        sheet.name for sheet in stylesheets
        if re.search(re.escape(token) + r"\s*:", sheet.read_text(encoding="utf-8", errors="replace"))
    ]
    require(
        declarations == ["app.css"],
        f"{token} is declared in {declarations or 'no file'}; it must be declared "
        f"exactly once, in app.css",
    )

app_css = (WEB / "app.css").read_text(encoding="utf-8", errors="replace")
for token in ("--fs-1", "--fs-2", "--fs-3", "--fs-4", "--fs-5", "--fs-6",
              "--sp-1", "--sp-2", "--sp-3", "--sp-4", "--sp-5", "--sp-6", "--sp-7",
              "--r-1", "--r-2", "--r-3", "--tap"):
    require(f"{token}:" in app_css, f"the scale is missing {token}")

# --- Two properties of the scale itself ------------------------------------

# 16px is the floor for anything focusable. iOS zooms the whole page when a
# focused input is smaller, which on a cabinet-mounted tablet reads as the
# interface breaking.
body = re.search(r"--fs-3:\s*([^;]+);", app_css)
require(body is not None and body.group(1).strip() == "1rem",
        "--fs-3 must be 1rem: it is the body size and the minimum for inputs, "
        "below which iOS zooms the page on focus")

# A control screen operated with gloves on a mounted tablet.
tap = re.search(r"--tap:\s*(\d+)px;", app_css)
require(tap is not None and int(tap.group(1)) >= 44,
        "--tap must be at least 44px; this interface is operated with gloves on a "
        "tablet in a cabinet, so the touch target floor is not a suggestion")

if failures:
    print("Design scale contract FAILED:")
    for failure in failures:
        print(f"  - {failure}")
    sys.exit(1)

print(f"Design scale contract passed ({total} raw values, ceiling {RAW_VALUE_CEILING}; "
      f"scale declared once in app.css)")
