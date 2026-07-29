from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ENGINE = (ROOT / "components/source_detection/source_detection_engine.c").read_text(encoding="utf-8")
ENGINE_H = (ROOT / "components/source_detection/include/source_detection_engine.h").read_text(encoding="utf-8")
CONFIG = (ROOT / "components/source_detection/source_detection_config.c").read_text(encoding="utf-8")
CONFIG_H = (ROOT / "components/source_detection/include/source_detection_config.h").read_text(encoding="utf-8")
RUNTIME = (ROOT / "components/source_detection/source_detection.c").read_text(encoding="utf-8")
API = (ROOT / "components/web_server/source_detection_api.c").read_text(encoding="utf-8")
WEB = (ROOT / "web/source-detection.js").read_text(encoding="utf-8")
SIM = (ROOT / "tools/soltrix_modbus_simulator.js").read_text(encoding="utf-8")
APP_CONFIG = (ROOT / "components/config_manager/include/config_types.h").read_text(encoding="utf-8")
APP_CORE = (ROOT / "components/app_core/app_core.c").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/esp-idf-build.yml").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


for token in [
    "SOURCE_DETECTION_MODE_SINGLE_INPUT",
    "SOURCE_DETECTION_MODE_DUAL_METER",
    "SOURCE_STATE_UNKNOWN",
    "SOURCE_TARIFF_1",
    "SOURCE_TARIFF_2",
    "SOURCE_REASON_CONFLICT",
    "SOURCE_REASON_NON_FINITE",
    "SOURCE_REASON_DEBOUNCE_PENDING",
]:
    require(token in ENGINE_H, f"source detection engine contract missing {token}")

require("observation.candidate_state == SOURCE_STATE_UNKNOWN" in ENGINE,
        "unknown evidence must take the immediate fail-closed path")
require("result.fail_closed = true" in ENGINE,
        "unknown evidence must be explicitly marked fail-closed")
require("SOURCE_STATE_GRID) return SOURCE_TARIFF_1" in ENGINE,
        "grid source must select tariff 1")
require("SOURCE_STATE_GENERATOR) return SOURCE_TARIFF_2" in ENGINE,
        "generator source must select tariff 2")
require("isfinite(evidence->grid_power_kw)" in ENGINE and
        "isfinite(evidence->generator_power_kw)" in ENGINE,
        "dual-meter evidence must reject non-finite values")
require("grid_active && generator_active" in ENGINE and "SOURCE_REASON_CONFLICT" in ENGINE,
        "both-above-threshold evidence must resolve to conflict")
require("!grid_active && !generator_active" in ENGINE and "SOURCE_REASON_NO_SOURCE" in ENGINE,
        "neither-above-threshold evidence must fail closed")

require("SOURCE_DETECTION_SINGLE_REGISTER_DEFAULT 0x2160u" in CONFIG_H,
        "clone-specific source input default must remain 0x2160")
require("SOURCE_DETECTION_POWER_THRESHOLD_DEFAULT_KW 1.0f" in CONFIG_H,
        "power threshold must default to 1.0 kW, not zero")
require("config->debounce_ms = 0U" in CONFIG and "config->stale_timeout_ms = 0U" in CONFIG,
        "site timing must remain unconfigured rather than invented")
require('SOURCE_DETECTION_NVS_NAMESPACE "src_detect"' in CONFIG,
        "source detection must use an additive NVS namespace")
require("pvdg/config blob" in CONFIG,
        "commissioned app configuration preservation must be documented in code")
require("nvs_flash_erase" not in CONFIG and "nvs_erase" not in CONFIG,
        "source detection must never erase commissioned NVS")
require("#define APP_CONFIG_VERSION 3u" in APP_CONFIG,
        "Phase 1 must not resize or reset the commissioned app/Wi-Fi schema")

for token in [
    "Automatic Solar-Grid control remains fail-closed",
    "source_detection_notify_config_changed",
    "meter_manager_read_registers",
    "meter_manager_get_data",
    "detection_only = true",
]:
    require(token in RUNTIME, f"runtime source detection contract missing {token}")
require("inverter_manager_set_total_power_kw" not in RUNTIME,
        "Phase 1 source detection must not issue inverter commands")
require("control_engine" not in RUNTIME,
        "Phase 1 must not enable or wire automatic control")

require('"/api/source-detection"' in API,
        "source detection configuration/status API is missing")
require('"reason", status->reason_text' in API,
        "firmware fail-closed reason must be surfaced verbatim")
require('"automatic_control_enabled_by_this_phase", false' in API,
        "status API must state that this phase does not enable control")
require("source_detection_config_save(&config)" in API,
        "configuration API must use verified NVS persistence")
require("meter_manager_read_registers" not in API,
        "HTTP handler must not perform blocking Modbus transactions")

require("status.reason || 'Source status unavailable.'" in WEB,
        "browser must display the firmware reason without rewriting it")
require("status.mode_a_limitation" in WEB,
        "Mode A no-redundancy limitation must be visible")
require("verified" not in WEB.lower(),
        "Mode A must not be described as verified")
require("Automatic control is not enabled by this phase" in WEB,
        "UI must state detection-only scope")

for scenario in [
    "em500-single-grid", "em500-single-generator", "em500-single-toggle",
    "em500-dual-grid", "em500-dual-generator", "em500-dual-conflict",
    "em500-dual-none", "em500-threshold-hover",
]:
    require(scenario in SIM, f"simulator scenario missing {scenario}")

require("source_detection_config_init()" in APP_CORE and
        "source_detection_init()" in APP_CORE,
        "source detection is not initialized by app_core")
require("source_detection_engine_test.c" in WORKFLOW and
        "em500_source_detection_source_contract.py" in WORKFLOW,
        "CI does not execute the Phase 1 host and contract tests")

print("EM500 source detection source contract passed")
