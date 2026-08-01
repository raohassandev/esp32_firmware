#!/usr/bin/env python3
"""The preview must serve the assets in the order the firmware does.

The browser gets ONE app.js and ONE app.css, concatenated by the firmware in the
order of the asset arrays in components/web_server/web_server.c. That order is
load-bearing twice: a module that calls another while rendering must come after
it, and the card layer must come last in the CSS or the per-module panels win.

CMakeLists.txt lists the same files in a DIFFERENT order. A preview built from
that one renders a cascade the product does not have -- so it shows a layout
nobody will ever see and hides the one they will, which is worse than having no
preview, because it is a preview that lies. The list was kept in step by hand,
which lasted exactly as long as somebody remembered.

Three things are pinned here:

  1. tools/ui_preview_order.json is what the firmware actually serves.
  2. Every .js and .css in web/ is served by something. A file that is not is
     either dead weight or a module somebody forgot to register, and the second
     is invisible until a page quietly stops working.
  3. The card layer is last in the CSS. It is the shared answer the per-module
     stylesheets are being converted to, and while both exist it has to win.
"""
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXTRACTOR = ROOT / "tools" / "extract_asset_order.js"
ORDER = ROOT / "tools" / "ui_preview_order.json"
WEB = ROOT / "web"


def main():
    failures = []

    if not EXTRACTOR.exists():
        print("Asset order contract FAILED: tools/extract_asset_order.js is missing")
        return 1

    # The extractor reads web_server.c and compares. Running it rather than
    # reimplementing it is the point: a second copy of the parsing rule would be
    # one more thing to drift.
    result = subprocess.run(
        ["node", str(EXTRACTOR), "--check"],
        capture_output=True, text=True, cwd=str(ROOT))
    if result.returncode != 0:
        failures.append(
            (result.stderr.strip() or result.stdout.strip()
             or "the preview asset order does not match the firmware"))

    # Nothing in web/ may be orphaned. The extractor reports this on stderr even
    # when it otherwise succeeds, so it is checked explicitly.
    if "never served" in (result.stderr or ""):
        failures.append(result.stderr.strip())

    if ORDER.exists():
        import json
        order = json.loads(ORDER.read_text(encoding="utf-8"))

        css = order.get("css", [])
        if css and "cards.css" in css:
            # Everything after cards.css must be a deliberate later layer. The
            # rule is not "cards.css is last" -- files that extend the card
            # vocabulary legitimately follow it -- but nothing that predates it
            # may, because those are exactly the panels it is replacing.
            tail = css[css.index("cards.css") + 1:]
            legacy = [name for name in tail
                      if name in {"app.css", "devices.css", "em500.css", "wifi.css",
                                  "theme.css", "product-mode.css"}]
            if legacy:
                failures.append(
                    f"{', '.join(legacy)} is served AFTER cards.css, so the "
                    f"per-module panels the card layer replaces would win the "
                    f"cascade")
        elif css:
            failures.append("cards.css is not served at all")

        # A module that another calls while rendering must be loaded first.
        # These are the pairs that exist today; each was a real ordering the
        # firmware arrays encode and the preview must reproduce.
        js = order.get("js", [])
        precedence = [
            ("icons.js", "operator-view.js"),
            ("operator-proof.js", "operator-view.js"),
            ("meter-detail.js", "devices.js"),
            ("inverter-detail.js", "devices.js"),
            ("alarm-journal.js", "operator-operations.js"),
        ]
        for earlier, later in precedence:
            if earlier in js and later in js and js.index(earlier) > js.index(later):
                failures.append(
                    f"{later} calls into {earlier} while rendering, but {earlier} "
                    f"is served after it, so the call would find nothing defined")

    if failures:
        print("Asset order contract FAILED:")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    print("Asset order contract passed (preview serves what the firmware serves, "
          "nothing in web/ is orphaned, and load order is respected)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
