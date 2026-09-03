#!/usr/bin/env python3
"""Prevent the historical Engineering error translator DOM feedback freeze."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "web/engineering-errors.js").read_text(encoding="utf-8")
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


# Translation must be idempotent: generated friendly descendants and an already
# translated owner are never translated again.
for token in (
    "node.dataset.errorFriendly === 'true'",
    "node.closest('.engineering-friendly-error')",
    "friendly.dataset.errorFriendly = 'true'",
    "title.dataset.errorFriendly = 'true'",
    "message.dataset.errorFriendly = 'true'",
    "detail.dataset.errorFriendly = 'true'",
):
    require(token in SOURCE, f"idempotent error translation safeguard missing: {token}")

# The observer may inspect newly inserted application nodes only. Re-observing
# text/attribute changes made by the translator itself recreated the historical
# recursive MutationObserver feedback loop.
require("mutation.addedNodes.forEach" in SOURCE,
        "observer must be driven only by newly added nodes")
observer_start = SOURCE.index("new MutationObserver")
observer_tail = SOURCE[observer_start:]
require("{ childList: true, subtree: true }" in observer_tail,
        "observer scope must remain child-list only")
for forbidden in ("characterData: true", "attributes: true"):
    require(forbidden not in observer_tail,
            f"translator must not observe its own {forbidden.split(':')[0]} mutations")
require("document.getElementById('mainContent') || document.body" in SOURCE,
        "observer must prefer the bounded product content root")
require("!item.closest('.engineering-friendly-error')" in SOURCE,
        "generated friendly descendants must be excluded before scanning")

# Retry links/buttons and diagnostic controls are functional UI, not disposable
# error text. The translator must preserve them and prepend friendly copy.
require("node.querySelector('button, a, input, select, textarea')" in SOURCE,
        "interactive diagnostic descendants are not detected")
interactive_start = SOURCE.index("const hasInteractiveContent")
interactive_end = SOURCE.index("const panel", interactive_start)
interactive_block = SOURCE[interactive_start:interactive_end]
require("if (hasInteractiveContent)" in interactive_block and
        "node.prepend(friendly)" in interactive_block,
        "interactive diagnostics must preserve controls while adding friendly text")
require("else" in interactive_block and "node.replaceChildren(friendly)" in interactive_block,
        "plain non-interactive error text may be replaced only in the non-interactive branch")

# Known raw controller errors remain mapped without hiding the diagnostic code.
for code in (
    "ESP_ERR_INVALID_RESPONSE",
    "ESP_ERR_TIMEOUT",
    "ESP_ERR_NOT_SUPPORTED",
    "ESP_ERR_INVALID_STATE",
    "ESP_FAIL",
):
    require(code in SOURCE, f"Engineering error mapping lost {code}")
require("node.dataset.diagnosticCode = translated.code" in SOURCE,
        "raw technical diagnostic identity must remain available to Engineering")

# The fixed module must actually ship in the served firmware bundle.
require("engineering-errors.js" in CMAKE,
        "Engineering error stability module is not embedded")
require("web_assets_engineering_errors_js" in SERVER,
        "Engineering error stability module is not served")

print("Engineering error DOM stability source contract passed")
