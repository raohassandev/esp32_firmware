#!/usr/bin/env python3
"""The commissioning wizard must not render a control that does nothing, and
must not call evidence valid while the controller is running other settings.

Two defects motivate this, both found on a working build.

DEAD CONTROL. "Finish commissioning" was rendered as an enabled primary button
with no data-action attribute, and bind() wired no handler for it. Pressing it
issued no request, stored nothing and printed no message. It sat at the end of a
seven-step workflow looking like the thing that completes it. A button that
looks like the end of the job and silently does nothing is worse than no button:
the engineer walks away believing something was recorded.

The general form is what is checked here -- every data-action the wizard renders
must have a handler bound to it -- because the specific instance was invisible
for as long as it existed and would be again.

STALE EVIDENCE. Both configuration endpoints answer restart_required:true and
the wizard discarded it. Saved settings are stored but not in force until the
controller restarts, so the repeated reads taken straight after a save describe
the PREVIOUS configuration. Reporting that as a pass certifies settings nobody
tested. It therefore blocks the verdict rather than warning, and clearing it
takes a restart and a re-run.

The flag also has to be able to clear. The endpoints answer restart_required on
every accepted save whether or not anything changed, so honouring that literally
would never settle: restart, re-qualify, and the save that re-qualification
performs raises it again. It is raised only when the values sent differ from the
values read back moments earlier.

Asserted against comment-stripped source.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
WIZARD = ROOT / "web" / "commissioning-release-v3.js"

failures = []


def require(condition, message):
    if not condition:
        failures.append(message)


text = WIZARD.read_text(encoding="utf-8", errors="replace")
text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)

# --- No rendered control is inert -----------------------------------------

# The lookbehind matters. querySelector('[data-action="finish"]') contains the
# same substring as the markup, so a plain search finds an action in the BIND
# site and reports it rendered. Removing the attribute from the button then
# leaves both sets unchanged and the check passes over a control that renders
# inert -- which is the exact defect. Only an occurrence not preceded by '['
# is markup.
rendered = set(re.findall(r'(?<!\[)data-action="([a-z-]+)"', text))
bound = set(re.findall(r"querySelector\('\[data-action=\"([a-z-]+)\"\]'\)", text))
require(
    len(rendered) >= 8,
    f"only {len(rendered)} actions were found; the extraction pattern has "
    f"probably stopped matching and this check is vacuous",
)
dead = sorted(rendered - bound)
require(
    not dead,
    f"these controls are rendered but have no handler bound: {dead}. A control "
    f"that looks like it acts and does nothing is how 'Finish commissioning' "
    f"stayed inert through a full release",
)

require(
    "finish" in rendered and "function finish()" in text,
    "the finish action is missing from the rendered markup",
)

# It must not claim something the controller has not said. There is no
# commissioning-complete endpoint; inventing one, or POSTing to a route that
# does not exist, would be worse than the inert button it replaces.
require(
    "/api/commissioning/complete" not in text and "/api/commissioning/finish" not in text,
    "the wizard posts to a commissioning-completion endpoint that does not exist "
    "in the firmware",
)
for phrase in ("Automatic control", "field acceptance"):
    require(
        phrase.lower() in text.lower(),
        f"finishing does not state what remains outstanding ({phrase}); a bare "
        f"'complete' would read as a release",
    )

# --- The restart notice ---------------------------------------------------

require(
    "Restart required — restart now?" in text,
    "the restart prompt does not state itself in the words the engineer was "
    "promised",
)
# Set membership, not a substring search: data-action="restart-wizard" contains
# data-action="restart" and would satisfy a regex while the restart button
# itself had been removed.
require(
    "restart" in rendered and "'/api/system/restart'" in text,
    "the restart prompt offers no way to perform the restart",
)
# Offered, never automatic: restarting a controller is a decision, not a side
# effect of pressing Save.
require(
    re.search(r"function restartController\(\)\{if\(!confirm\(", text) is not None,
    "the restart is not confirmed before it is issued",
)
require(
    re.search(r"restartBanner\(\)", text) is not None
    and re.search(r"root\.innerHTML=header\(\)\+restartBanner\(\)", text) is not None,
    "the restart notice is not rendered on every step; a save made on one step "
    "would be forgotten by navigating to another",
)

# "Later" must not clear the flag -- the controller is still running the old
# settings and the next render has to say so again.
require(
    "state.restart_required=false" not in re.sub(
        r"async function restartController\(\)\{.*?\n\}", " ", text, flags=re.DOTALL
    ),
    "something other than an accepted restart clears restart_required; the "
    "notice would disappear while the controller still runs the old settings",
)

# --- Stale evidence blocks the verdict ------------------------------------

verdict_fn = re.search(r"function verdict\(\)\{(.*?)return\{blockers,", text, re.DOTALL)
require(verdict_fn is not None, "verdict() could not be located")
if verdict_fn is not None:
    require(
        "state.restart_required" in verdict_fn.group(1)
        and "blockers.push" in verdict_fn.group(1),
        "a pending restart does not block the commissioning verdict; the report "
        "would certify qualification evidence taken against settings that were "
        "not in force",
    )

# --- The flag can actually clear ------------------------------------------

require(
    re.search(r"function differs\(stored,next\)", text) is not None,
    "there is no comparison against the stored configuration, so restart_required "
    "would be raised on every save and could never settle",
)
require(
    len(re.findall(r"saved\.restart_required&&changed", text)) == 2,
    "restart_required is not gated on an actual change at both save sites",
)

if failures:
    print("Commissioning finish/restart contract FAILED:")
    for failure in failures:
        print(f"  - {failure}")
    sys.exit(1)

print(
    f"Commissioning finish/restart contract passed ({len(rendered)} rendered "
    "actions, all bound; restart blocks the verdict and can clear)"
)
