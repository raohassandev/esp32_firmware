#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OVERVIEW = ROOT / "boards/waveshare_esp32_s3_touch_lcd_5/screen/pages/overview_screen.c"
PRODUCT = ROOT / "boards/waveshare_esp32_s3_touch_lcd_5/screen/product_800x480/main/main.c"


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


if __name__ == "__main__":
    test_overview_rssi_uses_static_fixed_width_status_label()
    test_fix_does_not_hide_bug_by_changing_status_cadence()
    print("waveshare status scan source contract: PASS")
