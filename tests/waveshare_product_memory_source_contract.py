#!/usr/bin/env python3
"""Guard the exact-board product memory policy after physical DMA exhaustion.

This is a source/config contract only. It does not claim that the chosen budget
passes hardware; the real board still has to prove steady-state headroom. It
prevents the exact configuration that physically collapsed internal DMA from
silently returning while keeping the separately-qualified RGB anti-tear path.
"""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SCREEN = ROOT / "boards/waveshare_esp32_s3_touch_lcd_5/screen"
BASE = SCREEN / "product_800x480"
SDK = (BASE / "sdkconfig.defaults").read_text(encoding="utf-8")
MAIN = (BASE / "main/main.c").read_text(encoding="utf-8")
APP = (SCREEN / "screen_app.c").read_text(encoding="utf-8")


def config_int(name: str) -> int:
    match = re.search(rf"^{re.escape(name)}=(\d+)$", SDK, re.M)
    assert match, f"missing numeric sdkconfig value: {name}"
    return int(match.group(1))


always_internal = config_int("CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL")
reserve_internal = config_int("CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL")

# Failed physical candidate used 4096/65536 and reached only 1695 bytes free
# internal DMA before safety/recovery task creation completed.
assert always_internal <= 512, (
    "ordinary <=4 KiB malloc traffic must not be biased back into scarce internal RAM"
)
assert reserve_internal >= 98304, (
    "explicit DMA/internal consumers need the enlarged protected pool used by the requalification lane"
)
assert "CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y" in SDK
assert "CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y" in SDK

# Memory rebalancing must not silently weaken the RGB stabilization mechanism.
assert "#define PRODUCT_RGB_BOUNCE_LINES 10U" in MAIN
assert "#define PRODUCT_TEAR_MODE ESP_LV_ADAPTER_TEAR_AVOID_MODE_DOUBLE_DIRECT" in MAIN
assert "allow_no_bounce_fallback = true" in MAIN

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
