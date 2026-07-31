#!/usr/bin/env python3
"""The Solar inverters page must answer its own question, and must not be a
setup workbench.

Two findings from a screenshot of a live unit, both structural.

SETUP LIVED ON A MONITORING PAGE. /inverters asks "how much solar is available
and which equipment needs attention?" -- an operator's question. Unlocking an
engineering session stacked the profile catalogue and the endpoint editor
underneath that question: seven panels, of which the operator sees one. Which
family of inverter is installed, and at which host and unit id, is
commissioning data. It is decided once during commissioning and never during
monitoring. Both panels now mount in the engineering workspace.

THE PAGE NEVER ANSWERED ITS QUESTION. renderCurrent() and refreshAll() in
operator-view.js returned early during an engineering session, so signing in
REMOVED the live product view. The engineer saw setup forms and no production
figure anywhere on screen -- the one number the page is named after was visible
only to the reader who could not act on it. The endpoints involved are
read-only and operator-scoped, so there was nothing being withheld.

What must stay gated is hideLegacyOperatorContent(), which suppresses the
engineering panels for an operator. That is a real access distinction, and this
contract asserts it survives -- the easy wrong fix for the above is to delete
every isOperator() check in the file, which would expose engineering panels to
unauthenticated viewers.

Asserted against comment-stripped source.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
WEB = ROOT / "web"

failures = []


def require(condition, message):
    if not condition:
        failures.append(message)


def source(name):
    text = (WEB / name).read_text(encoding="utf-8", errors="replace")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"^\s*//[^\n]*", " ", text, flags=re.MULTILINE)


profiles = source("inverter-profiles.js")
config = source("inverter-config.js")
operator = source("operator-view.js")

# --- Setup panels are off the monitoring page -----------------------------

for name, text in (("inverter-profiles.js", profiles), ("inverter-config.js", config)):
    require(
        '[data-page="inverters"]' not in text,
        f"{name} still mounts on the inverters page; setup would reappear under "
        f"the operator's question the moment an engineering session is unlocked",
    )
    require(
        '[data-page="engineering"]' in text,
        f"{name} does not mount in the engineering workspace",
    )

# They must stay together. The profile panel derives its transport readout from
# the endpoint editor's channel and says so in the text the engineer reads;
# splitting them across pages would leave that sentence pointing at nothing.
require(
    "Inverter endpoints and ratings" in profiles,
    "the profile panel no longer references the endpoint editor it derives the "
    "transport from",
)

# --- The product view renders for everyone --------------------------------

render_fn = re.search(r"function renderCurrent\(\)\s*\{(.*?)\n\s{4}\}", operator, re.DOTALL)
require(render_fn is not None, "renderCurrent() could not be located")
if render_fn is not None:
    require(
        "isOperator()" not in render_fn.group(1),
        "renderCurrent() is still gated on operator access; signing in would "
        "again remove the live production figure from the page named after it",
    )

refresh_fn = re.search(r"async function refreshAll\(\)\s*\{(.*?)\n\s{4}\}", operator, re.DOTALL)
require(refresh_fn is not None, "refreshAll() could not be located")
if refresh_fn is not None:
    require(
        "isOperator()" not in refresh_fn.group(1),
        "refreshAll() is still gated on operator access, so the product view "
        "would render from a payload that is never fetched",
    )
    # Removing that guard un-cancels a five-second timer on every route,
    # including ones this module draws nothing on, competing for the four client
    # sockets the controller has.
    require(
        "PRODUCT_ROUTES.has(route())" in refresh_fn.group(1),
        "refreshAll() polls on every route; it must poll only where this module "
        "draws, or commissioning and network setup pay four requests every five "
        "seconds for nothing",
    )

# --- But the suppression survives -----------------------------------------

hide_fn = re.search(
    r"function hideLegacyOperatorContent\(\)\s*\{(.*?)\n\s{4}\}", operator, re.DOTALL
)
require(hide_fn is not None, "hideLegacyOperatorContent() could not be located")
if hide_fn is not None:
    require(
        "if (!isOperator()) return;" in hide_fn.group(1),
        "hideLegacyOperatorContent() no longer checks access; engineering panels "
        "would be left visible to an unauthenticated viewer",
    )
    require(
        "operator-legacy-hidden" in hide_fn.group(1),
        "the suppression class is gone from hideLegacyOperatorContent()",
    )

require(
    operator.count("function isOperator()") == 1,
    "isOperator() is not defined exactly once in operator-view.js",
)

# --- The monitoring page still signposts where setup went -----------------

html = (ROOT / "web" / "index.html").read_text(encoding="utf-8", errors="replace")
start = html.index('data-page="inverters"')
end = html.find('<section class="page"', start + 10)
section = html[start : end if end > 0 else len(html)]
require(
    'href="#/engineering"' in section,
    "the inverters page offers no route to the setup that was moved off it",
)

if failures:
    print("Inverter page scope contract FAILED:")
    for failure in failures:
        print(f"  - {failure}")
    sys.exit(1)

print(
    "Inverter page scope contract passed (setup moved to the engineering "
    "workspace, product view renders for both access levels, suppression intact)"
)
