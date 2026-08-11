#!/usr/bin/env python3
"""The measured production a screen shows must come from the fast endpoint.

The command path was already fast: a written limit moves the machines within a
second or two. What lagged was the reporting of it. The product view polls every
fifteen seconds and the inverter telemetry endpoint every ten, so production
appeared ten to fifteen seconds after it changed -- long enough for an operator
to decide the command had not landed and send it again.

This contract holds the two halves of the repair together. The firmware must
publish per-machine kW on /api/live, and the renderer must write that over the
telemetry it holds instead of waiting for its own timer. Either half alone
restores the lag silently, which is precisely how the bug survived once already.
"""

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def read(relative):
    return (ROOT / relative).read_text(encoding="utf-8", errors="ignore")


def require(condition, message):
    if not condition:
        print(f"live production freshness contract FAILED: {message}")
        sys.exit(1)


def main():
    live = read("components/web_server/live_api.c")

    require('cJSON_AddArrayToObject(root, "inverters")' in live,
            "/api/live must carry a per-inverter array, not only the fleet total: "
            "the fleet card and the per-machine table are different figures and "
            "the table is where an operator looks to see which machine moved")
    require('cJSON_AddNumberToObject(item, "index", i)' in live,
            "each live inverter entry must state its index, or the renderer "
            "cannot tell which row a measurement belongs to")
    require('add_kw(item, "kw"' in live,
            "each live inverter entry must carry its measured kW")
    require("data.telemetry_valid && !data.telemetry_stale" in live,
            "a stale or invalid reading must be published as unknown, never as a "
            "number: an inverter that stopped answering is not producing zero")

    view = read("web/operator-view.js")

    require("function overlayLive(" in view,
            "the renderer must have a named step that writes live measurements "
            "over the slow telemetry")
    require("window.AutomatrixLive" in view,
            "the overlay must read the frame the fast poller publishes")
    require(view.count("overlayLive(state.lastPayload)") >= 2,
            "the overlay must run both on the live tick and after the slow poll "
            "rebuilds the payload -- otherwise the slow poll reinstates a stale "
            "production figure the moment it lands")

    start = view.index("function overlayLive(")
    end = view.index("function inverterTable(", start)
    body = view[start:end]

    require("row.telemetry_valid = false" in body,
            "when the live frame reports no measurement for a machine the row "
            "must be marked unmeasured, not left holding the last number")
    require("measured_total_kw = finite(live.solar_kw) ? Number(live.solar_kw) : null" in body,
            "the fleet total must follow the same rule: unknown when nothing is "
            "measuring, rather than a stale total presented as current")
    require("if (!match) return;" in body,
            "an inverter the live frame does not mention must be left untouched")

    app = read("web/app.js")
    require("window.AutomatrixLive = { at: Date.now(), payload: live };" in app,
            "the fast poller must publish the whole frame; status has no field "
            "for solar and inventing one would put an inverter measurement "
            "inside the controller's own report")

    print("Live production freshness contract passed (per-machine kW is published "
          "on the fast endpoint, written over the slow telemetry on every tick and "
          "after every slow poll, and an unmeasured machine is never drawn as "
          "producing)")


if __name__ == "__main__":
    main()
