from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "components/config_manager/config_manager.c").read_text(encoding="utf-8")
HTTP_JSON = (ROOT / "components/web_server/http_json.c").read_text(encoding="utf-8")
HTTP_JSON_HEADER = (ROOT / "components/web_server/include/http_json.h").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
CONTROL_ENGINE = (ROOT / "components/control_engine/control_engine.c").read_text(encoding="utf-8")

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
assert '"auth_hmac_compat.c"' not in CMAKE, \
    "the ESP-IDF HMAC symbol must not be multiply defined by a compatibility object"

write_api_paths = [
    "components/web_server/web_api.c",
    "components/web_server/meter_config_api.c",
    "components/web_server/inverter_config_api.c",
    "components/web_server/inverter_profile_api.c",
    "components/web_server/em500_settings_plan_api.c",
    "components/web_server/solar_grid_api.c",
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

for token in [
    '#include "power_control_policy.h"',
    "power_control_step(&input)",
    ".policy = (grid_policy_t)s_grid_config.policy",
    ".source_mode = source.mode",
    "source_mode_evaluate(&source_evidence)",
    "grid_control_gate_step",
    "gate.control_allowed",
    "meter_sample_fresh",
    "oriented_grid_power",
    "safety_manager_limit_target_kw",
    "inverter_manager_set_total_power_kw",
    ".grid_power_kw = measured_grid_kw",
    "control_engine_force_disable",
]:
    assert token in CONTROL_ENGINE, f"live grid control policy wiring missing: {token}"
assert ".source_mode = SOURCE_MODE_GRID_ONLY" not in CONTROL_ENGINE, \
    "live Grid Only mode must be proven by explicit source evidence, not assumed from meter freshness"
# This previously asserted SOURCE_MODE_GENERATOR_ONLY was absent entirely, because at
# the time nothing could establish generator operation. That is no longer true, and the
# scope was changed deliberately by the system owner on 2026-07-29, and the decision is
# restated here because the brief that carried it has since been deleted as spent:
# no genset controller is integrated, the source is
# established by a wired 220 VAC input verified on site plus meter power, and the
# controller only ever reduces PV.
#
# The guarantee being protected is unchanged, so it is asserted directly instead of by
# the absence of a symbol: generator operation must never be inferred from a power
# reading alone, and breaker or synchronisation state must never be fabricated.
assert ".generator_breaker_closed = false," in CONTROL_ENGINE, \
    "generator breaker state must never be fabricated from a measurement"
assert ".grid_generator_synchronized = false," in CONTROL_ENGINE, \
    "synchronisation state must never be fabricated from a measurement"
assert "source_mode_from_measured_source" in CONTROL_ENGINE, \
    "generator operation must come from the measured-source mapping, which fails closed"
assert "transition_pending" in CONTROL_ENGINE, \
    "a source transition still inside its debounce must not count as a settled generator"
assert ".source_mode = SOURCE_MODE_GENERATOR_ONLY" not in CONTROL_ENGINE, \
    "generator mode must be produced by the source mapping, never hardcoded into the policy input"

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

print("configuration migration, bounded HTTP JSON, truthful status, and persisted grid-control policy tests passed")
