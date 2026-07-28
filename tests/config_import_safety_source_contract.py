from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "components/config_manager/config_manager.c").read_text(encoding="utf-8")

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

print("configuration migration, import safety, and power-control policy tests passed")
