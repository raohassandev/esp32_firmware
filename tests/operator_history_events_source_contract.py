from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
API = (ROOT / "components/web_server/operational_api.c").read_text(encoding="utf-8")
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
ASSETS = (ROOT / "components/web_server/web_assets.c").read_text(encoding="utf-8")
UI = (ROOT / "web/operator-operations.js").read_text(encoding="utf-8")
CSS = (ROOT / "web/operator-operations.css").read_text(encoding="utf-8")


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
for label in ["15 min", "1 hour", "24 hours", "Minimum", "Average", "Peak"]:
    require(label in UI, f"operator history UI missing {label}")
require("op-event-row" in CSS and "op-range-selector" in CSS,
        "operator history/event styling is incomplete")

print("Operator history and event center source contract passed")
