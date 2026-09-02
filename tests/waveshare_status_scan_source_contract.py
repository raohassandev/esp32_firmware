#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / "boards/waveshare_esp32_s3_touch_lcd_5/screen"
OVERVIEW = BOARD / "pages/overview_screen.c"
PRODUCT = BOARD / "product_800x480/main/main.c"
RUNTIME_C = BOARD / "screen_runtime.c"
RUNTIME_H = BOARD / "screen_runtime.h"


def test_overview_rssi_uses_static_fixed_width_status_label():
    text = OVERVIEW.read_text(encoding="utf-8")

    assert "#define OVERVIEW_STATUS_DRAW_WIDTH 300" in text
    assert "char network_text[OVERVIEW_STATUS_TEXT_MAX];" in text
    assert "lv_obj_set_width(value, OVERVIEW_STATUS_DRAW_WIDTH);" in text
    assert "lv_label_set_long_mode(value, LV_LABEL_LONG_CLIP);" in text
    assert "lv_label_set_text_static(value, value_buffer);" in text
    assert "set_static_value_fmt(s_ui.network_state," in text
    assert "set_text_fmt_if_changed(s_ui.network_state" not in text


def test_fix_does_not_hide_bug_by_changing_status_cadence():
    text = PRODUCT.read_text(encoding="utf-8")
    assert "#define SCREEN_STATUS_MS 5000U" in text


def test_overview_status_tick_does_not_build_unused_telemetry():
    product = PRODUCT.read_text(encoding="utf-8")
    runtime_c = RUNTIME_C.read_text(encoding="utf-8")
    runtime_h = RUNTIME_H.read_text(encoding="utf-8")

    start = product.index("static void refresh_overview_status(void)")
    end = product.index("static void refresh_status(void)", start)
    overview_refresh = product[start:end]

    assert "local_backend_provider_fetch(SCREEN_API_STATUS_PATH)" in overview_refresh
    assert "screen_runtime_refresh_status_only()" in overview_refresh
    assert "SCREEN_API_TELEMETRY_PATH" not in overview_refresh
    assert "local_backend_provider_read_commissioning" not in overview_refresh
    assert "refresh_overview_status();" in product

    assert "bool screen_runtime_refresh_status_only(void);" in runtime_h
    assert "bool screen_runtime_refresh_status_only(void)" in runtime_c
    assert "static bool refresh_telemetry_only(void)" in runtime_c
    assert "const bool status_ok = screen_runtime_refresh_status_only();" in runtime_c
    assert "const bool telemetry_ok = refresh_telemetry_only();" in runtime_c


if __name__ == "__main__":
    test_overview_rssi_uses_static_fixed_width_status_label()
    test_fix_does_not_hide_bug_by_changing_status_cadence()
    test_overview_status_tick_does_not_build_unused_telemetry()
    print("waveshare status scan source contract: PASS")