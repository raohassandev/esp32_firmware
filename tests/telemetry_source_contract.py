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
assert "float commanded_kw = targets[i].rated_kw * percent / 100.0f;" in INVERTER_MANAGER, \
    "commanded kW must be derived from the validated percentage and snapshot rating"
assert "runtime->data.commanded_power_kw = commanded_kw;" in INVERTER_MANAGER, \
    "runtime diagnostics must store the command actually sent"
assert "runtime->data.commanded_power_kw = share_kw;" not in INVERTER_MANAGER, \
    "pre-validation requested share must not be reported as the sent command"
assert "if (command_err == ESP_OK)" in INVERTER_MANAGER, \
    "command diagnostics must only be committed after a successful write"

required_ui_fragments = [
    "Measured production",
    "Command results must not be treated as measured power",
    "measured inverter production, generator power and facility-load telemetry are not configured",
]
for fragment in required_ui_fragments:
    assert fragment.lower() in UI.lower(), f"missing truthful UI wording: {fragment}"

assert "method: 'POST'" not in UI and 'method: "POST"' not in UI, \
    "device diagnostics UI must not issue POST requests"

print("telemetry source contract passed")
