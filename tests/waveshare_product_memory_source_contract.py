#!/usr/bin/env python3
"""Guard the exact-board product memory/RGB policy after physical failures.

This is a source/config contract only. It does not claim hardware acceptance.
It prevents the exact configurations that exhausted internal DMA or diverged
from the pinned Waveshare RGB transport from silently returning.
"""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SCREEN = ROOT / "boards/waveshare_esp32_s3_touch_lcd_5/screen"
BASE = SCREEN / "product_800x480"
SDK = (BASE / "sdkconfig.defaults").read_text(encoding="utf-8")
MAIN = (BASE / "main/main.c").read_text(encoding="utf-8")
APP = (SCREEN / "screen_app.c").read_text(encoding="utf-8")
DISPLAY_PORT = (SCREEN / "drivers/waveshare_display_port.c").read_text(encoding="utf-8")


def config_int(name: str) -> int:
    match = re.search(rf"^{re.escape(name)}=(\d+)$", SDK, re.M)
    assert match, f"missing numeric sdkconfig value: {name}"
    return int(match.group(1))


always_internal = config_int("CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL")
reserve_internal = config_int("CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL")
static_rx = config_int("CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM")
static_tx = config_int("CONFIG_ESP_WIFI_STATIC_TX_BUFFER_NUM")
rx_ba_win = config_int("CONFIG_ESP_WIFI_RX_BA_WIN")

# Failed physical candidate used 4096/65536 and collapsed scarce internal DMA.
assert always_internal <= 512, (
    "ordinary <=4 KiB malloc traffic must not be biased back into scarce internal RAM"
)
assert reserve_internal >= 98304, (
    "explicit DMA/internal consumers need the protected pool used by the requalification lane"
)
assert "CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y" in SDK
assert "CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y" in SDK

# Exact-board diagnostic evidence established RX4/TX4 with BA8 as the first
# configuration that removed the critical Core NO_MEM/degraded-startup failure.
assert static_rx == 4
assert "CONFIG_ESP_WIFI_STATIC_TX_BUFFER=y" in SDK
assert static_tx == 4
assert rx_ba_win == 8

# The pinned Waveshare LVGL9 reference keeps executable/cache traffic available
# from PSRAM while the RGB driver refills bounce buffers. Keep that transport
# policy explicit instead of relying on changing ESP-IDF defaults.
assert "CONFIG_SPIRAM_XIP_FROM_PSRAM=y" in SDK
assert "CONFIG_ESP32S3_INSTRUCTION_CACHE_32KB=y" in SDK
assert "CONFIG_ESP32S3_DATA_CACHE_64KB=y" in SDK
assert "CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y" in SDK
assert ".sram_trans_align = 4" in DISPLAY_PORT
assert ".psram_trans_align = 64" in DISPLAY_PORT

# Product-only DMA rebalance: two RGB565 bounce buffers at 800 px use
# 2 * width * 2 bytes * lines. Moving 10 -> 6 lines releases exactly 12.8 KiB
# while retaining DOUBLE_DIRECT and a nonzero bounce path. Standalone HIL keeps
# its separate vendor-baseline qualification.
assert "#define PRODUCT_RGB_BOUNCE_LINES 6U" in MAIN
assert 2 * 800 * 2 * (10 - 6) == 12800
assert "#define PRODUCT_TEAR_MODE ESP_LV_ADAPTER_TEAR_AVOID_MODE_DOUBLE_DIRECT" in MAIN
assert "allow_no_bounce_fallback = true" in MAIN
assert "2 PSRAM framebuffers, 6-line bounce" in MAIN

# Boot must no longer allocate every hidden page before the first visible frame.
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

# A wedged LVGL worker must be observable rather than holding main/refresh forever.
assert "#define SCREEN_LVGL_LOCK_MS 2000U" in MAIN
assert "esp_lv_adapter_lock(-1)" not in MAIN
assert "esp_lv_adapter_lock(SCREEN_LVGL_LOCK_MS)" in MAIN
assert "LVGL activation lock timed out/failed" in MAIN
assert "LVGL activation stage 5/6" in MAIN

# Hardware evidence remains mandatory for the next candidate.
assert 'log_dma_headroom("Before LCD DMA reservation")' in MAIN
assert 'log_dma_headroom("After LCD DMA reservation")' in MAIN
assert 'log_dma_headroom("Before Product Core init")' in MAIN
assert 'log_dma_headroom("After Product Core init")' in MAIN
assert 'log_dma_headroom("After LVGL/UI activation")' in MAIN
assert 'log_runtime_headroom("Screen soak")' in MAIN

print("Waveshare product memory source contract: PASS")
