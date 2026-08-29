#!/usr/bin/env python3
"""Release gate for the native Waveshare backend lane.

This is intentionally a test-first gate.  It must remain red while the local
LCD provider hard-disables authoritative alarms/events.  The implementation is
free to expose Core snapshots directly or project them through a bounded adapter,
but it may not silently report the operational surfaces as unavailable forever
and it may not reintroduce same-device HTTP as the transport.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROVIDER = ROOT / "boards/waveshare_esp32_s3_touch_lcd_5/screen/product_800x480/main/local_backend_provider.c"

text = PROVIDER.read_text(encoding="utf-8")

assert "{SCREEN_API_EVENTS_PATH,        0U" not in text, (
    "native backend parity blocked: /api/operator/events is hard-disabled"
)
assert "{SCREEN_API_ALARMS_PATH,        0U" not in text, (
    "native backend parity blocked: /api/operator/alarms is hard-disabled"
)
assert "Alarms/events remain conservative-unavailable" not in text, (
    "native backend still contains the deliberate operations-unavailable boundary"
)
assert "socket/TCP self-transport removed" in text, (
    "do not solve native parity by restoring unreliable same-device HTTP"
)

print("Waveshare native backend parity gate: PASS")
