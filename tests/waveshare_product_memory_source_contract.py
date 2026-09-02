#!/usr/bin/env python3
"""Guard Waveshare product memory and RGB transport configuration."""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SCREEN = ROOT / "boards/waveshare_esp32_s3_touch_lcd_5/screen"
BASE = SCREEN / "product_800x480"
SDK = (BASE / "sdkconfig.defaults").read_text(encoding="utf-8")
MAIN = (BASE / "main/main.c").read_text(encoding="utf-8")
APP = (SCREEN / "screen_app.c").read_text(encoding="utf-8")
PORTAL = (ROOT / "components/network_manager/captive_portal.c").read_text(encoding="utf-8")


def config_int(name: str) -> int:
    match = re.search(rf"^{re.escape(name)}=(\d+)$", SDK, re.M)
    assert match, f"missing numeric sdkconfig value: {name}"
    return int(match.group(1))


assert config_int("CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL") <= 512
assert config_int("CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL") >= 98304
assert "CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y" in SDK
assert "CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y" in SDK

assert config_int("CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM") == 4
assert "CONFIG_ESP_WIFI_STATIC_TX_BUFFER=y" in SDK
assert config_int("CONFIG_ESP_WIFI_STATIC_TX_BUFFER_NUM") == 4
assert config_int("CONFIG_ESP_WIFI_RX_BA_WIN") == 8

assert "CONFIG_SPIRAM_XIP_FROM_PSRAM=y" in SDK
assert "CONFIG_ESP32S3_INSTRUCTION_CACHE_16KB=y" in SDK
assert "CONFIG_ESP32S3_INSTRUCTION_CACHE_32KB=y" not in SDK
assert "CONFIG_ESP32S3_DATA_CACHE_32KB=y" in SDK
assert "CONFIG_ESP32S3_DATA_CACHE_64KB=y" not in SDK
assert "CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y" in SDK

# Any task created with xTaskCreateWithCaps() must use the matching deletion API.
# The recovery captive-DNS task is self-deleting when AP mode stops, so this is
# a real runtime path rather than a diagnostic-only contract.
assert "xTaskCreateWithCaps(portal_task" in PORTAL
assert "vTaskDeleteWithCaps(NULL)" in PORTAL
assert re.search(r"\bvTaskDelete\s*\(\s*NULL\s*\)", PORTAL) is None

# Product-only RGB bandwidth/memory balance. HIL profile remains at 16 MHz/10 lines.
assert "#define PRODUCT_RGB_BOUNCE_LINES 6U" in MAIN
assert "#define PRODUCT_RGB_PCLK_HZ 12000000U" in MAIN
assert "s_product_profile = *vendor_profile;" in MAIN
assert "s_product_profile.pixel_clock_hz = PRODUCT_RGB_PCLK_HZ;" in MAIN
assert 2 * 800 * 2 * (10 - 6) == 12800
assert "#define PRODUCT_TEAR_MODE ESP_LV_ADAPTER_TEAR_AVOID_MODE_DOUBLE_DIRECT" in MAIN
assert "allow_no_bounce_fallback = true" in MAIN
assert "2 PSRAM framebuffers, 6-line bounce" in MAIN

create_match = re.search(
    r"lv_obj_t \*screen_app_create\([^)]*\)\s*\{(.*?)\n\}\n\nvoid screen_app_show_page",
    APP,
    re.S,
)
assert create_match, "unable to locate screen_app_create()"
create_body = create_match.group(1)
assert "ensure_page(SCREEN_PAGE_OVERVIEW)" in create_body
assert "grid_screen_create" not in create_body
assert "solar_screen_create" not in create_body
assert "alarms_screen_create" not in create_body
assert "readiness_screen_create" not in create_body
assert "commissioning_screen_create" not in create_body
assert "source_commissioning_screen_create" not in create_body
assert "static lv_obj_t *ensure_page(screen_page_t page)" in APP
assert "if (!ensure_page(page)) return;" in APP
assert "s_commissioning_backend = *backend" in APP
assert "commissioning_screen_set_backend(&s_commissioning_backend)" in APP
assert "s_source_backend = *backend" in APP
assert "source_commissioning_screen_set_backend(&s_source_backend)" in APP

assert "#define SCREEN_LVGL_LOCK_MS 2000U" in MAIN
assert "esp_lv_adapter_lock(-1)" not in MAIN
assert "esp_lv_adapter_lock(SCREEN_LVGL_LOCK_MS)" in MAIN
assert "LVGL activation lock timed out/failed" in MAIN
assert "LVGL activation stage 5/6" in MAIN

assert 'log_dma_headroom("Before LCD DMA reservation")' in MAIN
assert 'log_dma_headroom("After LCD DMA reservation")' in MAIN
assert 'log_dma_headroom("Before Product Core init")' in MAIN
assert 'log_dma_headroom("After Product Core init")' in MAIN
assert 'log_dma_headroom("After LVGL/UI activation")' in MAIN
assert 'log_runtime_headroom("Screen soak")' in MAIN

print("Waveshare product memory source contract: PASS")
