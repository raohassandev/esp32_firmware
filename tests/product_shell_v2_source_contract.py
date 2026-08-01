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
assert "groupNavigation" in js and "dataset.shellGrouped" in js, "navigation hierarchy must be deterministic and idempotent"
assert "removeDuplicateIntros" in js, "duplicate page introductions must be suppressed"

# The shell is the single owner of the navigation list: both the item order and
# the section labels. product-experience-v2.js used to insert the labels into a
# list this module was reordering underneath them, so the outcome depended on
# which module ran last. Both label strings must therefore live here.
assert "experience-nav-label" in js, "the shell owns the navigation section labels"
assert "'Operate'" in js and "'Commission & service'" in js, "navigation sections are not labelled"

# Grouping must be repeatable, not latched: nav entries are added by later
# modules (the commissioning entry among them) after this module has started,
# and a one-shot pass leaves them unplaced. Repeatable means it must also be
# idempotent -- it compares before it writes.
assert "if (tail.length === ordered.length" in js, "navigation grouping must not mutate when already ordered"

# Visibility of a navigation entry follows Engineering authorisation and belongs
# to product-mode.js. The shell orders the list; it must never hide an entry.
grouping = js[js.index("function groupNavigation"):js.index("function removeDuplicateIntros")]
assert "hidden" not in grouping, "the shell must not decide navigation visibility"
assert "dataset.access" not in grouping and "authenticated" not in grouping, \
    "navigation order must not depend on authorisation state"

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
