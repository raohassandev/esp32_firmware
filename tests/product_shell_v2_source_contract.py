#!/usr/bin/env python3
import re
from pathlib import Path
import sys as _sys, pathlib as _pathlib
_sys.path.insert(0, str(_pathlib.Path(__file__).resolve().parent))
import bundle_membership as bundle


def code(text: str) -> str:
    """Executable source only; ownership comments name identifiers on purpose."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"(?m)^\s*//.*$", "", text)


ROOT = Path(__file__).resolve().parents[1]
js = (ROOT / "web/product-shell-v2.js").read_text(encoding="utf-8")
css = (ROOT / "web/product-shell-v2.css").read_text(encoding="utf-8")
cmake = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
assets_h = (ROOT / "components/web_server/include/web_assets.h").read_text(encoding="utf-8")
assets_c = (ROOT / "components/web_server/web_assets.c").read_text(encoding="utf-8")
server = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")

for token in (
    "shell-health-button", "shell-overflow-button", "shell-page-context",
    "System health", "Controller menu", "Refresh data", "Engineering workspace",
):
    assert token in js or token in css, f"missing product shell behavior: {token}"

assert ".status-strip { display: none; }" in css, "legacy global status strip must be removed from normal page flow"
assert "#controllerPill { display: none; }" in css, "legacy controller pill must not compete with health control"
assert ".product-tool-button" in css and "display: none" in css, "secondary display tools must leave the permanent header"
assert "max-width: 650px" in css and "place-items: end stretch" in css, "mobile overflow sheet is required"
assert "removeDuplicateIntros" in js, "duplicate page introductions must be suppressed"

# THE SHELL MUST NOT ARRANGE THE NAVIGATION LIST.
#
# It used to, and so did app.js, to different schemes: this module carried its
# own OPERATE/SERVICE route lists and its own two section labels and appended
# them to the same .nav-list that app.js orders from the ROUTES table. The result
# depended on which ran last, and the second copy went stale exactly as a second
# copy does -- still naming 'control', 'alarms' and 'readiness' after those pages
# were deleted, and never hearing about 'generator' or 'network'.
#
# What that produced on a real controller: Plant overview, Grid power and Solar
# inverters pushed below the ACCESS heading, with COMMISSION and MAINTAIN
# standing empty above them. The shell's own labels had been hidden with a CSS
# rule instead of removed, which left two empty spans and the conflict intact.
#
# app.js owns it now, alone: it holds the route table, each route's group and
# each route's durable name, and tests/ia_taxonomy_source_contract.py pins that
# ownership from the other side. This clause is the matching half -- if the shell
# ever grows a second arranger again, the two contracts disagree and one of them
# fails, which is the point.
assert "groupNavigation" not in js,     "the shell must not arrange the navigation list; app.js owns it"
assert "OPERATE_ROUTES" not in js and "SERVICE_ROUTES" not in js,     "a second route list in the shell is the copy that goes stale"
assert "experience-nav-label" not in js,     "the shell must not create navigation section labels; the ROUTES table names the groups"


# Visibility of a navigation entry follows Engineering authorisation and belongs
# to product-mode.js. The clause that inspected the shell's arranger for hidden
# handling is gone with the arranger itself: a module that no longer touches the
# list cannot hide anything in it. The ownership assertions above keep it so.

# One MutationObserver watches #mainContent for the whole application and it
# lives in product-mode.js. This module subscribes; it does not add its own.
assert "new MutationObserver" not in code(js), "the shell must not install a MutationObserver of its own"
assert "onContentChange(" in js, "the shell must subscribe to the shared content notifier"

# Health state is data published by app.js. Deriving it by reading the rendered
# status text back out of the DOM and regex-matching English words meant a copy
# edit changed the header indicator, and required a characterData observer over
# a strip that updates every two seconds.
assert "amx-controller-health" in js, "shell health must come from the published status event"
assert "statusController" not in code(js) and "status-strip" not in code(js), \
    "shell must not scrape rendered status text"
assert "characterData" not in code(js), "shell must not observe rendered text"

bundle.require_delivered("product-shell-v2.css", "product-shell-v2.js")
assert bundle.delivered("product-shell-v2.js")

for forbidden in ("/api/control", "/api/inverter-command", "method: 'POST'", 'method: "POST"'):
    assert forbidden not in js, f"product shell must remain navigation/presentation only: {forbidden}"

print("product shell V2 source contract: PASS")
