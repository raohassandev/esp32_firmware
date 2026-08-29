#!/usr/bin/env python3
"""Source-level guard for the Waveshare live-render stabilization lane.

The physical 800x480 RGB panel must not destroy and rebuild whole live lists on
periodic backend refresh. The product build must also use an RGB tear-avoid mode
intended for dynamic widget deltas rather than scan and repaint the same single
framebuffer. The exact-board image must publish low-cadence resource telemetry so
the physical soak can prove the display fix did not consume unsafe headroom.

This test does not claim hardware acceptance; it only prevents known unsafe or
unmeasurable stabilization patterns from returning.
"""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "boards/waveshare_esp32_s3_touch_lcd_5/screen"
GRID = BASE / "pages/grid_screen.c"
SOLAR = BASE / "pages/solar_screen.c"
ALARMS = BASE / "pages/alarms_screen.c"
COMMISSIONING = BASE / "pages/commissioning_screen.c"
WIDGETS = BASE / "components/screen_widgets.c"
PRODUCT_MAIN = BASE / "product_800x480/main/main.c"


def function_body(text: str, name: str, next_name: str) -> str:
    match = re.search(
        rf"void\s+{re.escape(name)}\s*\([^)]*\)\s*\{{(.*?)\n\}}\n\nvoid\s+{re.escape(next_name)}",
        text,
        re.S,
    )
    assert match, f"unable to locate {name}() source"
    return match.group(1)


grid = GRID.read_text(encoding="utf-8")
solar = SOLAR.read_text(encoding="utf-8")
alarms = ALARMS.read_text(encoding="utf-8")
commissioning = COMMISSIONING.read_text(encoding="utf-8")
widgets = WIDGETS.read_text(encoding="utf-8")
product = PRODUCT_MAIN.read_text(encoding="utf-8")

grid_apply = function_body(grid, "grid_screen_apply", "grid_screen_show_unavailable")
solar_apply = function_body(solar, "solar_screen_apply", "solar_screen_show_unavailable")
alarm_apply = function_body(alarms, "alarms_screen_apply_alarms", "alarms_screen_apply_events")
event_apply = function_body(alarms, "alarms_screen_apply_events", "alarms_screen_show_unavailable")
commissioning_apply = function_body(commissioning, "commissioning_screen_apply_gate", "commissioning_screen_apply_status")

assert "lv_obj_clean" not in grid_apply, "Grid live refresh must retain its LVGL row tree"
assert "lv_obj_clean" not in solar_apply, "Solar live refresh must retain its LVGL row tree"
assert "lv_obj_clean" not in alarm_apply, "Alarm refresh must retain its LVGL row tree"
assert "lv_obj_clean" not in event_apply, "Event refresh must retain its LVGL row tree"
assert "lv_obj_clean" not in alarms, "Alarms page must not tear down lists on unavailable transitions either"

# Commissioning may rebuild on explicit navigation/config edits, but an unchanged
# periodic gate refresh must not destroy/recreate the Review page widget tree.
assert "gate_snapshot_equal" in commissioning
assert "const bool changed = !gate_snapshot_equal" in commissioning_apply
assert "if (changed && s_ui.root && s_ui.config.unlocked && s_ui.step == 7U) render();" in commissioning_apply
assert commissioning_apply.count("render();") == 1, (
    "commissioning gate refresh must have exactly one guarded rebuild path"
)

assert "grid_row_ui_t rows[SCREEN_API_MAX_METERS]" in grid
assert "solar_row_ui_t rows[SCREEN_API_MAX_INVERTERS]" in solar
assert "alarm_row_ui_t alarms[SCREEN_API_MAX_ALARMS]" in alarms
assert "event_row_ui_t events[SCREEN_API_MAX_EVENTS]" in alarms
assert "screen_ui_set_text_if_changed" in widgets
assert "strcmp(current, text) == 0" in widgets

assert "#define PRODUCT_TEAR_MODE ESP_LV_ADAPTER_TEAR_AVOID_MODE_DOUBLE_DIRECT" in product, (
    "dynamic 800x480 RGB product UI must use the adapter's DOUBLE_DIRECT tear-avoid mode"
)
assert "ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE" not in product, (
    "single-framebuffer NONE mode must not silently return to the dynamic product UI"
)

# Hardware acceptance needs objective evidence, not only visual judgement. Keep
# the logging slow enough that instrumentation cannot become the flicker source.
assert "#define SCREEN_RESOURCE_LOG_MS 60000U" in product
assert "heap_caps_get_free_size(psram_caps)" in product
assert "heap_caps_get_largest_free_block(psram_caps)" in product
assert "heap_caps_get_free_size(dma_caps)" in product
assert "heap_caps_get_largest_free_block(dma_caps)" in product
assert "esp_get_minimum_free_heap_size()" in product
assert "uxTaskGetStackHighWaterMark(NULL)" in product
assert 'log_runtime_headroom("Screen soak")' in product

print("Waveshare flicker source contract: PASS")
