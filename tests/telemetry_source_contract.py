from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
API = (ROOT / "components/web_server/device_api.c").read_text(encoding="utf-8")
UI = (ROOT / "web/devices.js").read_text(encoding="utf-8")
INVERTER_MANAGER = (ROOT / "components/inverter_manager/inverter_manager.c").read_text(encoding="utf-8")
METER_MANAGER = (ROOT / "components/meter_manager/meter_manager.c").read_text(encoding="utf-8")

required_api_fragments = [
    '{.uri = "/api/meters", .method = HTTP_GET',
    '{.uri = "/api/inverters", .method = HTTP_GET',
    '{.uri = "/api/telemetry", .method = HTTP_GET',
    'cJSON_AddNullToObject(runtime, "active_power_kw")',
    'cJSON_AddNullToObject(item, "measured_power_kw")',
    'cJSON_AddNullToObject(inverters, "measured_power_kw")',
    'cJSON_AddNullToObject(availability, "generator_power_kw")',
    'cJSON_AddNullToObject(availability, "facility_load_kw")',
]

for fragment in required_api_fragments:
    assert fragment in API, f"missing telemetry safety contract: {fragment}"

assert "runtime->data.active_power_kw = NAN;" in METER_MANAGER, \
    "meter power must begin unavailable so generic status JSON emits null instead of 0.00 kW"
assert "No sample has been acquired yet" in METER_MANAGER

assert "HTTP_POST" not in API, "device telemetry API must remain read-only"
assert "esp_wifi_" not in API, "device telemetry API must not manipulate the radio"
assert "inverter_manager_set_total_power_kw" not in API, "telemetry API must not command inverters"
assert "config_manager_save" not in API, "telemetry API must not persist configuration"
assert "config_manager_import_json" not in API, "telemetry API must not import configuration"

assert "command_target_t targets[APP_MAX_INVERTERS]" in INVERTER_MANAGER, \
    "fleet command must use one immutable eligible-target snapshot"
assert "float commanded_kw = target->rated_kw * percent / 100.0f;" in INVERTER_MANAGER, \
    "commanded kW must be derived from the validated percentage and snapshot rating"
assert "runtime->data.commanded_power_kw = share_kw;" not in INVERTER_MANAGER, \
    "pre-validation requested share must not be reported as the sent command"

# Write confirmation is deferred to the background acquisition task (P0-9), so
# the committed command telemetry must be written by the confirmation evaluator
# and nowhere else. An issued write may only record requested_percent.
assert "inverter_write_confirmation_evaluate(&evidence)" in INVERTER_MANAGER
assert "verdict.state == INVERTER_WRITE_CONFIRMED" in INVERTER_MANAGER
assert INVERTER_MANAGER.count("data.commanded_power_kw =") == 1, \
    "committed command telemetry must have exactly one writer, the confirmation evaluator"
assert INVERTER_MANAGER.index("inverter_write_confirmation_evaluate(&evidence)") < \
    INVERTER_MANAGER.index("data.commanded_power_kw ="), \
    "command telemetry must be committed only after readback confirmation"
assert "note_write_issued" in INVERTER_MANAGER, \
    "an issued but unconfirmed write must be recorded separately from a commanded value"

required_ui_fragments = [
    "Measured production",
    "Command results must not be treated as measured power",
    "measured inverter production, generator power and facility-load telemetry are not configured",
]
for fragment in required_ui_fragments:
    assert fragment.lower() in UI.lower(), f"missing truthful UI wording: {fragment}"

assert "method: 'POST'" not in UI and 'method: "POST"' not in UI, \
    "device diagnostics UI must not issue POST requests"

print("truthful meter telemetry and confirmed inverter-command telemetry contract passed")
