from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "components/config_manager/config_manager.c").read_text(encoding="utf-8")
HTTP_JSON = (ROOT / "components/web_server/http_json.c").read_text(encoding="utf-8")
HTTP_JSON_HEADER = (ROOT / "components/web_server/include/http_json.h").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")

required = [
    "CONFIG_JSON_MAX_DEPTH 16U",
    "json_depth_valid",
    "!isfinite(x->valuedouble)",
    "loaded->wifi_provision_id = CONFIG_PVDG_WIFI_PROVISION_ID",
    "c->control.enabled = false;",
    "Generic import may tune values but cannot arm automatic control",
    "meter_valid",
    "inverter_valid",
    "control_valid",
    "endpoint_valid",
]

for token in required:
    assert token in SOURCE, f"configuration safety contract missing: {token}"

assert "loaded->wifi_provision_id = 0;" not in SOURCE, \
    "schema-2 migration must not reopen build provisioning over commissioned credentials"
assert "c->control.enabled = cJSON_IsTrue" not in SOURCE, \
    "generic configuration import must not enable automatic control"
assert "if (!text || !json_depth_valid(text))" in SOURCE, \
    "configuration JSON must be depth-checked before cJSON parsing"
assert "if (err == ESP_OK && !valid(c))" in SOURCE, \
    "imported configuration must pass full structural/numeric validation before persistence"

for token in [
    "http_body_read_bounded", "http_json_depth_valid", "http_json_parse_bounded",
    "HTTPD_SOCK_ERR_TIMEOUT", "ESP_ERR_TIMEOUT", "cJSON_ParseWithLength",
    "const uint64_t deadline", "now_ms() >= deadline",
]:
    assert token in HTTP_JSON or token in HTTP_JSON_HEADER, \
        f"shared HTTP JSON safety helper missing: {token}"
assert '"http_json.c"' in CMAKE

write_api_paths = [
    "components/web_server/web_api.c",
    "components/web_server/meter_config_api.c",
    "components/web_server/inverter_config_api.c",
    "components/web_server/inverter_profile_api.c",
    "components/web_server/em500_settings_plan_api.c",
]
for relative in write_api_paths:
    source = (ROOT / relative).read_text(encoding="utf-8")
    assert "http_json.h" in source, f"{relative} must use the shared bounded JSON reader"
    assert "cJSON_Parse(" not in source, f"{relative} must not parse unbounded raw request JSON"
    assert "httpd_req_recv(" not in source, f"{relative} must not implement an independent receive loop"
    assert "BODY_DEADLINE_MS" in source, f"{relative} must declare a cumulative body deadline"

web_api = (ROOT / "components/web_server/web_api.c").read_text(encoding="utf-8")
assert 'cJSON_AddNullToObject(root, "grid_power_kw")' in web_api
assert 'cJSON_AddNullToObject(root, "meter_age_ms")' in web_api
assert "meter.last_update_ms != 0 && isfinite(meter.active_power_kw)" in web_api

with tempfile.TemporaryDirectory() as directory:
    binary = Path(directory) / "power_control_policy_test"
    subprocess.run([
        "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I", str(ROOT / "components/control_engine/include"),
        str(ROOT / "tests/power_control_policy_test.c"),
        str(ROOT / "components/control_engine/power_control_policy.c"),
        "-lm", "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)

print("configuration migration, bounded HTTP JSON, truthful status, and power-control policy tests passed")
