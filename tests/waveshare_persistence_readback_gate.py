#!/usr/bin/env python3
"""Test-first gate for Waveshare Engineering persistence reliability.

A successful NVS/flash call is necessary but not sufficient evidence for the
native HMI to tell the engineer that a configuration was saved.  The lane must
read the authoritative persisted model back after the write and only report
success when the stored state matches the intended safe state.

This gate is intentionally RED on the field baseline until readback verification
is implemented for generic app-config saves, the two-model plant save and the
persistent ARM/DISARM path.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "boards/waveshare_esp32_s3_touch_lcd_5/screen/product_800x480/main/local_commissioning_backend.c"
text = SOURCE.read_text(encoding="utf-8")


def body(name: str, next_name: str) -> str:
    start = text.index(f"static bool {name}(")
    end = text.index(f"static bool {next_name}(", start)
    return text[start:end]


def require_readback_after_write(section: str, write_token: str, read_token: str, label: str) -> None:
    write_at = section.index(write_token)
    read_at = section.find(read_token, write_at + len(write_token))
    assert read_at >= 0, f"{label}: no authoritative readback after persistent write"


save_app = body("save_app_config", "local_save_site")
require_readback_after_write(
    save_app,
    "config_manager_save(next)",
    "config_manager_get_snapshot(",
    "generic app-config save",
)

save_plant = body("local_save_plant", "local_set_control_enabled")
require_readback_after_write(
    save_plant,
    "solar_grid_config_save(&solar)",
    "solar_grid_config_get_snapshot(",
    "plant/source save",
)
require_readback_after_write(
    save_plant,
    "solar_grid_config_save(&solar)",
    "config_manager_get_snapshot(",
    "plant/control save",
)

set_control = body("local_set_control_enabled", "local_restart_controller")
require_readback_after_write(
    set_control,
    "config_manager_save(&app)",
    "config_manager_get_snapshot(",
    "persistent ARM/DISARM",
)
assert "if (enabled) control_engine_force_disable();" in set_control, (
    "persistent ARM failure must force the running control loop disabled"
)

print("Waveshare commissioning persistence readback gate: PASS")
