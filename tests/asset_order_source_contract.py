#!/usr/bin/env python3
"""The bundle order is coherent, and it is the ONLY copy of that order.

The browser receives ONE app.js and ONE app.css, concatenated at BUILD time from
web/app.js.order and web/app.css.order. That order is load-bearing twice: a
module that calls another while rendering must come after it, and the card layer
must come last in the CSS or the per-module panels win at equal specificity.

WHAT THIS USED TO GUARD, AND WHY IT CHANGED. The order lived in three places at
once -- the assets[] arrays in web_server.c, CMakeLists.txt in a different order,
and a JSON copy for the preview. This contract existed to keep them in step,
which is a thing you only need to do when you have decided to have three copies.
They are now one, so the contract asserts the harder property instead: that no
second copy has grown back.

Nothing here re-implements the checks in tools/check_asset_order.js. It runs
them, because a second implementation of a consistency rule is exactly the
duplication this file is about.
"""
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CHECKER = ROOT / "tools" / "check_asset_order.js"
WEB = ROOT / "web"
WEB_SERVER = ROOT / "components" / "web_server" / "web_server.c"
CMAKELISTS = ROOT / "components" / "web_server" / "CMakeLists.txt"


def strip_c_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def main():
    failures = []

    for kind in ("js", "css"):
        if not (WEB / f"app.{kind}.order").exists():
            failures.append(f"web/app.{kind}.order is missing; it is the source of truth "
                            f"for the {kind} bundle order")

    if CHECKER.exists():
        result = subprocess.run(["node", str(CHECKER)], capture_output=True,
                                text=True, cwd=str(ROOT))
        if result.returncode != 0:
            failures.append(result.stderr.strip() or "the bundle order is not coherent")
    else:
        failures.append("tools/check_asset_order.js is missing")

    # NO SECOND COPY. web_server.c serves one pre-built blob; if an assets[]
    # array of per-file getters reappears there, the order has been forked again
    # and the two will drift.
    server = strip_c_comments(WEB_SERVER.read_text(encoding="utf-8", errors="replace"))
    per_file_getters = re.findall(r"web_assets_[a-z0-9_]+_(?:js|css)", server)
    stray = [name for name in per_file_getters if not name.startswith("web_assets_bundle_")]
    if stray:
        failures.append(
            f"web_server.c names per-file assets again ({', '.join(sorted(set(stray))[:4])}"
            f"{'...' if len(set(stray)) > 4 else ''}). The bundle order lives in "
            f"web/app.*.order; a second list here is the fork this was consolidated "
            f"to remove.")

    # And the build must not embed the individual files alongside the bundle:
    # that is the same 1 MB stored twice, in a partition that cannot spare it.
    cmake = CMAKELISTS.read_text(encoding="utf-8", errors="replace")
    embedded = re.findall(r"\$\{CMAKE_CURRENT_BINARY_DIR\}/([A-Za-z0-9_.-]+\.(?:js|css))",
                          cmake)
    unexpected = [name for name in embedded if not name.startswith("app_bundle.")]
    if unexpected:
        failures.append(
            f"CMakeLists.txt still embeds per-file web assets ({', '.join(sorted(set(unexpected))[:4])}). "
            f"With the bundle these are the same bytes stored twice.")

    # The compressed form must be embedded, or the whole point is lost.
    if "app_bundle.js.gz" not in cmake or "app_bundle.css.gz" not in cmake:
        failures.append(
            "the pre-compressed bundles are not embedded. Uncompressed, the "
            "interface is about 1 MB over Wi-Fi from an ESP32 -- roughly 27 "
            "seconds before anything appears, which is long enough that people "
            "conclude the controller has crashed and reload.")

    # And the firmware must only send it to a client that asked for it.
    if "Accept-Encoding" not in server or "Content-Encoding" not in server:
        failures.append(
            "the server does not negotiate encoding. Sending gzip to a client "
            "that did not offer it hands a commissioning script a file of binary "
            "garbage.")

    if failures:
        print("Asset order contract FAILED:")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    print("Asset order contract passed (one order, coherent, pre-compressed, "
          "and negotiated)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
