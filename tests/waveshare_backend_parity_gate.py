#!/usr/bin/env python3
"""Release gate for native Waveshare operational backend parity."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROVIDER = ROOT / "boards/waveshare_esp32_s3_touch_lcd_5/screen/product_800x480/main/local_backend_provider.c"
OP_C = ROOT / "components/web_server/operational_api.c"
OP_H = ROOT / "components/web_server/include/operational_api.h"

provider = PROVIDER.read_text(encoding="utf-8")
op_c = OP_C.read_text(encoding="utf-8")
op_h = OP_H.read_text(encoding="utf-8")

assert "{SCREEN_API_EVENTS_PATH,        0U" not in provider
assert "{SCREEN_API_ALARMS_PATH,        0U" not in provider
assert "Alarms/events remain conservative-unavailable" not in provider
assert "socket/TCP self-transport removed" in provider

for name in ("events", "alarms"):
    builder = f"operational_api_build_{name}_json"
    assert f"cJSON *{builder}(void);" in op_h
    assert f"cJSON *{builder}(void)" in op_c
    assert f"cJSON *root = {builder}();" in op_c, (
        f"HTTP {name} handler must call the shared authoritative builder"
    )
    assert builder in provider, (
        f"native provider must call the same authoritative {name} builder"
    )

assert "static bool build_operational" in provider
assert "event_text(" not in provider, "native provider must not duplicate event wording/lifecycle"
assert "alarm_priority(" not in provider, "native provider must not duplicate alarm priority logic"
assert "alarm_cause_of(" not in provider, "native provider must not duplicate alarm causality logic"
assert "http://127.0.0.1" not in provider and "esp_http_client" not in provider

print("Waveshare native backend parity gate: PASS")
