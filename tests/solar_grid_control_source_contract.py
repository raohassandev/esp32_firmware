from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
CONFIG_H = (ROOT / "components/solar_grid_config/include/solar_grid_config.h").read_text(encoding="utf-8")
CONFIG_C = (ROOT / "components/solar_grid_config/solar_grid_config.c").read_text(encoding="utf-8")
CONTROL = (ROOT / "components/control_engine/control_engine.c").read_text(encoding="utf-8")
CONTROL_H = (ROOT / "components/control_engine/include/control_engine.h").read_text(encoding="utf-8")
GATE = (ROOT / "components/control_engine/grid_control_gate.c").read_text(encoding="utf-8")
API = (ROOT / "components/web_server/solar_grid_api.c").read_text(encoding="utf-8")
STATUS_API = (ROOT / "components/web_server/solar_grid_status_api.c").read_text(encoding="utf-8")
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")
ASSETS = (ROOT / "components/web_server/web_assets.c").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
APP_CORE = (ROOT / "components/app_core/app_core.c").read_text(encoding="utf-8")
JS = (ROOT / "web/solar-grid.js").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


for token in [
    "SOLAR_GRID_POLICY_ZERO_EXPORT",
    "SOLAR_GRID_POLICY_LIMITED_EXPORT",
    "SOLAR_GRID_POLICY_MINIMUM_IMPORT",
    "SOLAR_GRID_IMPORT_POSITIVE",
    "SOLAR_GRID_EXPORT_POSITIVE",
    "grid_available",
    "grid_breaker_closed",
    "grid_loss_trip_ms",
    "grid_recovery_stable_ms",
]:
    require(token in CONFIG_H, f"persisted Solar-Grid model missing: {token}")

for token in [
    'SOLAR_GRID_NAMESPACE "pvdg_grid"',
    "nvs_set_blob",
    "nvs_commit",
    "nvs_get_blob",
    "memcmp(&verify, config",
    "solar_grid_config_evidence_complete",
    "grid_available.enabled != config->grid_breaker_closed.enabled",
]:
    require(token in CONFIG_C, f"Solar-Grid persistence safeguard missing: {token}")

require("solar_grid_config_init()" in APP_CORE,
        "Solar-Grid configuration must initialize before the control engine")
require(APP_CORE.index("solar_grid_config_init()") < APP_CORE.index("control_engine_init()"),
        "Solar-Grid persistence must load before live control starts")

for token in [
    "grid_evidence_task",
    "meter_manager_read_registers",
    "source_mode_evaluate(&source_evidence)",
    ".source_mode = source.mode",
    ".policy = (grid_policy_t)s_grid_config.policy",
    "oriented_grid_power",
    "SOLAR_GRID_EXPORT_POSITIVE",
    "gate.control_allowed",
    "grid_control_gate_step",
    "s_grid_config.evidence_stale_timeout_ms",
    "grid_control_gate_reset",
]:
    require(token in CONTROL or token in GATE,
            f"live explicit-grid evidence integration missing: {token}")

require(".source_mode = SOURCE_MODE_GRID_ONLY" not in CONTROL,
        "live control must not assume Grid Only from a fresh power meter")
require("Any uncertain, stale, open-breaker or contradictory evidence blocks PV" in GATE,
        "grid evidence gate must document immediate fail-closed behavior")
require("output.control_allowed = output.recovery_stable" in GATE,
        "grid recovery must remain blocked until continuous stabilization completes")
require("output.loss_confirmed" in GATE,
        "grid-loss confirmation timer contract is incomplete")

require("control_engine_force_disable" in CONTROL_H and
        "s_runtime_forced_disabled = true" in CONTROL,
        "running control disable latch is missing")
for token in [
    "s_safe_zero_pending = true",
    "inverter_manager_set_total_power_kw(0.0f)",
    "clear_safe_zero_pending();",
    "applied_kw = current_target_kw",
]:
    require(token in CONTROL,
            f"confirmed safe-zero transition after runtime disable missing: {token}")
require("control_engine_force_disable();" in API,
        "Solar-Grid writes must disable the already-running control task")
require("application->control.enabled = false" in API,
        "Solar-Grid writes must persist automatic control disabled")
require(API.index("application->control.enabled = false") < API.index("solar_grid_config_save(&next)"),
        "control must be disabled before the new source model is persisted")
require('"control_forced_disabled", true' in API,
        "Solar-Grid save response must disclose the enforced disable")
require("http_json_parse_bounded" in API and "SOLAR_GRID_JSON_MAX_DEPTH" in API,
        "Solar-Grid write API lacks bounded body/depth protection")
require("meter_manager_read_registers" not in API,
        "Solar-Grid configuration HTTP handler must not perform Modbus I/O")

for token in [
    '"/api/solar-grid/config"',
    '"/api/solar-grid/status"',
    '"modbus_io_in_http_handler", false',
    "grid_gate_state_name",
    "grid_evidence_last_error_name",
]:
    require(token in API or token in STATUS_API,
            f"Solar-Grid API/status contract missing: {token}")
require("meter_manager_read_registers" not in STATUS_API,
        "Solar-Grid status HTTP handler must return cache only")

for token in [
    "web_assets_solar_grid_js",
    "solar_grid_api_register(s_server)",
    "solar_grid_status_api_register(s_server)",
]:
    require(token in SERVER, f"Solar-Grid server integration missing: {token}")
handler_capacity = re.search(r"config\.max_uri_handlers\s*=\s*(\d+)", SERVER)
require(handler_capacity is not None and int(handler_capacity.group(1)) >= 40,
        "HTTP server must retain at least the Solar-Grid URI-handler capacity")
require("DECLARE_ASSET(solar_grid_js)" in ASSETS,
        "Solar-Grid browser asset is not exported")
for token in [
    'configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/solar-grid.js"',
    '"solar_grid_api.c"',
    '"solar_grid_status_api.c"',
    '"${CMAKE_CURRENT_BINARY_DIR}/solar-grid.js"',
    "solar_grid_config",
]:
    require(token in CMAKE, f"Solar-Grid build integration missing: {token}")

for token in [
    "Zero export",
    "Limited export",
    "Minimum grid import",
    "Positive = grid import",
    "Positive = grid export",
    "Grid available evidence",
    "Grid breaker closed evidence",
    "Evidence stale timeout",
    "Recovery stable time",
    "restart is required",
    "AbortController",
    "document.hidden",
    "beforeunload",
    "credentials: 'same-origin'",
]:
    require(token in JS, f"Solar-Grid commissioning browser contract missing: {token}")
require("/api/control" not in JS,
        "Solar-Grid commissioning page must not contain a control-enable call")
require("window.setInterval(" not in JS,
        "Solar-Grid runtime polling must use bounded route-aware timeouts")

print("Persisted Solar-Grid policy, evidence, runtime gate and UI source contract passed")
