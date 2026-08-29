#!/usr/bin/env python3
"""Source-level guard for the Waveshare live-render stabilization lane.

The physical 800x480 RGB panel must not destroy and rebuild whole device lists
on each periodic backend refresh.  This test does not claim hardware acceptance;
it only prevents the known high-churn rendering pattern from returning.
"""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
GRID = ROOT / "boards/waveshare_esp32_s3_touch_lcd_5/screen/pages/grid_screen.c"
SOLAR = ROOT / "boards/waveshare_esp32_s3_touch_lcd_5/screen/pages/solar_screen.c"
WIDGETS = ROOT / "boards/waveshare_esp32_s3_touch_lcd_5/screen/components/screen_widgets.c"


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
widgets = WIDGETS.read_text(encoding="utf-8")

grid_apply = function_body(grid, "grid_screen_apply", "grid_screen_show_unavailable")
solar_apply = function_body(solar, "solar_screen_apply", "solar_screen_show_unavailable")

assert "lv_obj_clean" not in grid_apply, "Grid live refresh must retain its LVGL row tree"
assert "lv_obj_clean" not in solar_apply, "Solar live refresh must retain its LVGL row tree"
assert "grid_row_ui_t rows[SCREEN_API_MAX_METERS]" in grid
assert "solar_row_ui_t rows[SCREEN_API_MAX_INVERTERS]" in solar
assert "screen_ui_set_text_if_changed" in widgets
assert "strcmp(current, text) == 0" in widgets

print("Waveshare flicker source contract: PASS")
