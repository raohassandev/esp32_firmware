from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
API = (ROOT / "components/web_server/operational_api.c").read_text(encoding="utf-8")
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
ASSETS = (ROOT / "components/web_server/web_assets.c").read_text(encoding="utf-8")
UI = (ROOT / "web/operator-operations.js").read_text(encoding="utf-8")
CSS = (ROOT / "web/operator-operations.css").read_text(encoding="utf-8")
# The range selector and the range statistics moved out of this screen and
# into the one shared chart component when the two competing chart
# implementations were consolidated. They are still operator-facing UI and
# still asserted; they are simply asserted where they now live.
CHART = (ROOT / "web/pvdg-chart.js").read_text(encoding="utf-8")
CHART_CSS = (ROOT / "web/pvdg-chart.css").read_text(encoding="utf-8")
OPERATOR_VIEW = (ROOT / "web/operator-view.js").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


for endpoint in ["/api/operator/history", "/api/operator/events"]:
    require(endpoint in API, f"missing sanitized operator endpoint {endpoint}")
    require(endpoint in UI, f"operator UI does not consume {endpoint}")

require("FAST_SAMPLE_COUNT 180" in API, "15-minute high-resolution history ring missing")
require("MINUTE_SAMPLE_COUNT 1440" in API, "24-hour minute history ring missing")
require("EVENT_COUNT 96" in API, "bounded operator event ring missing")
require("SAMPLE_INTERVAL_MS 5000U" in API and "MINUTE_INTERVAL_MS 60000U" in API,
        "history sampling cadence is incomplete")
require('strcmp(range, "1h")' in API and 'strcmp(range, "24h")' in API,
        "history range selection is incomplete")
require('"controller_resident", true' in API, "history must declare controller residency")
require('"recommended_action"' in API and '"severity"' in API,
        "operator events need severity and recommended action")
require("nvs_" not in API and "esp_partition" not in API,
        "high-frequency history must not wear flash")
require("modbus_tcp_write" not in API and "inverter_manager_set_total_power_kw" not in API,
        "operator history/event collection must issue no device commands")
for forbidden_field in [
    '"register_address"', '"pdu_address"', '"scale_factor"',
    '"function_code"', '"raw_registers"', '"endpoint_host"'
]:
    require(forbidden_field not in API,
            f"operator payload exposes engineering field {forbidden_field}")

history = API[API.index("static esp_err_t history_get"):API.index("static const char *severity_label")]
events = API[API.index("static esp_err_t events_get"):API.index("esp_err_t operational_api_register")]
for name, body in [("history", history), ("events", events)]:
    lock_start = body.find("portENTER_CRITICAL")
    lock_end = body.find("portEXIT_CRITICAL")
    require(lock_start >= 0 and lock_end > lock_start, f"{name} snapshot lock missing")
    locked = body[lock_start:lock_end]
    require("cJSON_" not in locked and "malloc(" not in locked and "calloc(" not in locked,
            f"{name} allocates or serializes while interrupts are disabled")
require("snapshot[i] = ring[index]" in history,
        "history ring must be copied before JSON serialization")
require("snapshot[i] = s_events[index]" in events,
        "event ring must be copied before JSON serialization")

require("operational_api_register(s_server)" in SERVER,
        "operator history/event API is not registered")
require('"operational_api.c"' in CMAKE,
        "operator history/event API is not compiled")
require("operator-operations.js" in CMAKE and "operator-operations.css" in CMAKE,
        "operator history/event assets are not embedded")
require("web_assets_operator_operations_js" in SERVER and
        "web_assets_operator_operations_css" in SERVER,
        "operator history/event assets are not served")
require("operator_operations_js_start" in ASSETS and "operator_operations_css_start" in ASSETS,
        "operator history/event embedded symbols are missing")
require("Alarm and event center" in UI and "Active conditions" in UI,
        "dedicated operator alarm center is missing")
require("requestAnimationFrame" in UI and "subtree: true" not in UI,
        "operator alarm enhancement must be deduplicated and must not observe its own subtree output")
require("AbortController" in UI and "Controller request timed out" in UI,
        "operator history/event fetches need bounded request timeouts")
for label in ["15 min", "1 hour", "24 hours", "Minimum", "Average", "Peak", "Current"]:
    require(label in CHART, f"operator history chart missing {label}")
require("op-event-row" in CSS and "op-range-selector" in CSS,
        "operator history/event styling is incomplete")

# ------------------------------------------------------- one chart, and only one
#
# The product used to carry two chart implementations that both drew this
# data and both appeared on the dashboard at the same time: a browser-session
# sparkline in operator-view.js and a controller-history sparkline in
# operator-operations.js. Neither had a time axis and both compacted the
# samples with .filter(Number.isFinite) before drawing, which deleted the
# missing readings rather than showing them.
#
# On a reverse-power controller that is safety-relevant: a straight line
# across an unmeasured interval, or an unmeasured sample drawn at 0 kW, reads
# as "no power" when the truth is "no measurement". These assertions exist so
# that behaviour cannot come back.
for source, name in [(UI, "operator-operations.js"), (OPERATOR_VIEW, "operator-view.js")]:
    require("function sparkline" not in source,
            f"{name} must not carry its own chart implementation")
    require("op-spark" not in source,
            f"{name} must not render the retired sparkline markup")
require("PvdgChart" in UI and "mountChart" in UI,
        "the operator screens must draw the shared chart component")
require("root.PvdgChart = api" in CHART,
        "the chart component must publish itself as the shared PvdgChart module")
require("pvdg-chart.js" in CMAKE and "pvdg-chart.css" in CMAKE,
        "the chart component is not embedded")
require("web_assets_pvdg_chart_js" in SERVER and "web_assets_pvdg_chart_css" in SERVER,
        "the chart component is not served")
require("pvdg_chart_js_start" in ASSETS and "pvdg_chart_css_start" in ASSETS,
        "the chart component embedded symbols are missing")

# A missing sample stays missing: it is never dropped, interpolated across,
# or coerced to zero.
require(".filter(Number.isFinite)" not in CHART and ".filter(Number.isFinite)" not in UI,
        "missing samples must not be compacted away before drawing")
require("function segments" in CHART and "function gaps" in CHART,
        "the chart must split its line at a gap rather than bridge one")
segment_body = CHART[CHART.index("function segments"):CHART.index("function gaps")]
require("point.v === null" in segment_body,
        "a null value must end a drawn run")
stats_body = CHART[CHART.index("function stats"):CHART.index("function niceStep")]
require("if (value === null) continue;" in stats_body,
        "the range statistics must be computed from measured samples only")
require("currentMissing" in CHART,
        "a stale reading must not be presented as the current value")

# X is a real timestamp. The retired charts used the array index.
require("base - age" in CHART, "sample timestamps must be reconstructed from age_ms")
require("points.sort((a, b) => a.t - b.t)" in CHART,
        "samples must be ordered by time, not by array position")

# The controller substitutes 15m for any range it does not know, so no other
# value may ever be requested.
require("function normalizeRange" in CHART, "the range value must be constrained before it is sent")
require("state.range = window.PvdgChart.normalizeRange(value)" in UI,
        "the requested range must pass through the constrained set")

# Series identity may not rest on colour alone (WCAG 2.2 1.4.1), and the
# chart must have a text alternative that states its coverage.
require("glyph:" in CHART and "dash:" in CHART,
        "series must be distinguishable without colour")
require("function summaryText" in CHART and "aria-live" in CHART,
        "the chart needs a text summary and keyboard-accessible point details")
require("ArrowRight" in CHART and "ArrowLeft" in CHART,
        "chart points must be reachable from the keyboard")

# The plot must dominate its card. The retired trend card was 220 px tall
# with a 92 px drawing area.
require("--pvc-plot-height" in CHART_CSS and "height: var(--pvc-plot-height, 500px)" in CHART_CSS,
        "the chart plot area is not sized by the component")

# The chart's pure logic - the scale, the statistics, the bucketing and above all
# the gap handling - is unit tested, and that test runs in CI.
WORKFLOW = (ROOT / ".github/workflows/esp-idf-build.yml").read_text(encoding="utf-8")
require((ROOT / "web/tests/chart-utils.test.js").exists(),
        "the chart logic has no unit test")
require("node web/tests/chart-utils.test.js" in WORKFLOW,
        "the chart unit test is not registered in the build workflow")
require("node --check web/pvdg-chart.js" in WORKFLOW,
        "the chart component is not syntax checked in the build workflow")

print("Operator history and event center source contract passed")
