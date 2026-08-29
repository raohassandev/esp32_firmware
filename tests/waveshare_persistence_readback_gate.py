#!/usr/bin/env python3
"""Source contract for Waveshare Engineering persistence reliability.

The native HMI must use the Product Core persistence services rather than
implementing a second storage/readback path.  Both Core services are required to
commit and verify their NVS blobs before updating the active snapshot.  The HMI
layer is responsible for fail-closed sequencing around those verified writes.

Physical save/reboot/read evidence remains a separate HIL gate; this contract
only proves that the software path reaches the canonical verified storage APIs.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HMI = ROOT / "boards/waveshare_esp32_s3_touch_lcd_5/screen/product_800x480/main/local_commissioning_backend.c"
APP_STORE = ROOT / "components/config_manager/config_manager.c"
SOLAR_STORE = ROOT / "components/solar_grid_config/solar_grid_config.c"

hmi = HMI.read_text(encoding="utf-8")
app_store = APP_STORE.read_text(encoding="utf-8")
solar_store = SOLAR_STORE.read_text(encoding="utf-8")


def function_body(text: str, signature: str, next_signature: str) -> str:
    start = text.index(signature)
    end = text.index(next_signature, start)
    return text[start:end]


def ordered(section: str, *tokens: str) -> None:
    pos = -1
    for token in tokens:
        nxt = section.find(token, pos + 1)
        assert nxt >= 0, f"missing required persistence token: {token}"
        assert nxt > pos
        pos = nxt


# Canonical app configuration persistence: write -> commit -> reopen read-only ->
# read back -> compare -> only then publish the active snapshot.
app_save = function_body(
    app_store,
    "esp_err_t config_manager_save(const app_config_t *c)",
    "static void endpoint_to_json",
)
ordered(
    app_save,
    "nvs_set_blob(h, KEY, c, sizeof(*c))",
    "nvs_commit(h)",
    "nvs_open(NS, NVS_READONLY, &h)",
    "nvs_get_blob(h, KEY, verify, &verify_size)",
    "memcmp(verify, c, sizeof(*c)) == 0",
    "set_active(c)",
)

# Canonical Solar/Grid persistence has the same verified-storage property.
solar_save = function_body(
    solar_store,
    "esp_err_t solar_grid_config_save(const solar_grid_config_t *config)",
    "esp_err_t solar_grid_config_init(void)",
)
ordered(
    solar_save,
    "nvs_set_blob(handle, SOLAR_GRID_KEY, config, sizeof(*config))",
    "nvs_commit(handle)",
    "nvs_open(SOLAR_GRID_NAMESPACE, NVS_READONLY, &handle)",
    "nvs_get_blob(handle, SOLAR_GRID_KEY, &verify, &size)",
    "memcmp(&verify, config, sizeof(verify)) != 0",
    "set_active(config)",
)

# The HMI must fail closed before generic configuration persistence.
save_app = function_body(hmi, "static bool save_app_config(", "static bool local_save_site(")
ordered(save_app, "control_engine_force_disable();", "next->control.enabled = false;", "config_manager_save(next)")

# Plant saves span two verified Core models. A partial second-model failure must
# remain an explicit failure with control already forced off and restart required.
save_plant = function_body(hmi, "static bool local_save_plant(", "static bool local_set_control_enabled(")
ordered(
    save_plant,
    "control_engine_force_disable();",
    "app.control.enabled = false;",
    "config_manager_save(&app)",
    "solar_grid_config_save(&solar)",
)
assert "Control remains disabled; review before restart." in save_plant
assert "s_restart_required = true;" in save_plant

# If a persistent ARM write fails after the running loop was enabled, the HMI
# must immediately force the running control path back to disabled.
set_control = function_body(hmi, "static bool local_set_control_enabled(", "static bool local_restart_controller(")
ordered(set_control, "control_engine_set_enabled(enabled)", "config_manager_save(&app)")
assert "if (enabled) control_engine_force_disable();" in set_control
assert "Persistent arm failed; running control was forced disabled." in set_control

print("Waveshare commissioning persistence Core-contract gate: PASS")
