#!/usr/bin/env python3
"""Release gate for native Waveshare operational backend parity.

The LCD and HTTP surfaces must consume one Core-owned operational payload
implementation. This gate intentionally checks architecture as well as the
former zero-capacity symptom so a later cleanup cannot silently reintroduce a
second alarm/event state machine, same-device HTTP, or large internal-DRAM HMI
buffers that compete with control/network resources.
"""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
PROVIDER = ROOT / "boards/waveshare_esp32_s3_touch_lcd_5/screen/product_800x480/main/local_backend_provider.c"
OP_C = ROOT / "components/web_server/operational_api.c"
OP_H = ROOT / "components/web_server/include/operational_api.h"
DOC = ROOT / "docs/waveshare_backend_parity.md"

provider = PROVIDER.read_text(encoding="utf-8")
op_c = OP_C.read_text(encoding="utf-8")
op_h = OP_H.read_text(encoding="utf-8")
doc = DOC.read_text(encoding="utf-8")

assert "{SCREEN_API_EVENTS_PATH,        0U" not in provider
assert "{SCREEN_API_ALARMS_PATH,        0U" not in provider
assert "Alarms/events remain conservative-unavailable" not in provider
assert "socket/TCP self-transport removed" in provider

# Operational JSON can be substantially larger than the LCD's 16-row display
# projection. Keep explicit bounded provider slots large enough for the complete
# Core payload; truncation belongs in screen_api.c after parsing, never in the
# authoritative state transfer.
events_slot = re.search(r"\{SCREEN_API_EVENTS_PATH,\s*(\d+)U", provider)
alarms_slot = re.search(r"\{SCREEN_API_ALARMS_PATH,\s*(\d+)U", provider)
assert events_slot and int(events_slot.group(1)) >= 49152
assert alarms_slot and int(alarms_slot.group(1)) >= 32768

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

# The shared builders own the same semantic machinery the HTTP API had before
# this refactor. The native provider is only a bounded transport/projection seam.
assert "cJSON *operational_api_build_events_json(void)" in op_c
assert "event_text(event, &title, &detail, &action);" in op_c
assert "cJSON *operational_api_build_alarms_json(void)" in op_c
assert "alarm_cause_of((uint8_t)code, snapshot)" in op_c
assert "alarm_priority((uint8_t)code)" in op_c
assert "service_design_suppression_locked(current)" in op_c
assert "service_shelf_locked" in op_c
assert "return send_json(request, root);" in op_c, (
    "HTTP transport must remain a thin wrapper over the shared cJSON builders"
)

assert "static bool build_operational" in provider
assert "event_text(" not in provider, "native provider must not duplicate event wording/lifecycle"
assert "alarm_priority(" not in provider, "native provider must not duplicate alarm priority logic"
assert "alarm_cause_of(" not in provider, "native provider must not duplicate alarm causality logic"
assert "service_design_suppression_locked" not in provider
assert "service_shelf_locked" not in provider
assert "http://127.0.0.1" not in provider and "esp_http_client" not in provider

# Large HMI serialization slots are presentation transport. If PSRAM cannot
# supply them, the LCD may fail unavailable; it must not consume scarce internal
# DRAM and reduce headroom for Wi-Fi/httpd/control tasks.
init_start = provider.index("bool local_backend_provider_init(")
fetch_start = provider.index("bool local_backend_provider_fetch(", init_start)
init_body = provider[init_start:fetch_start]
assert "heap_caps_malloc(slot->capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)" in init_body
assert "slot->json = malloc(slot->capacity)" not in init_body
assert "Unable to allocate %u PSRAM bytes" in init_body

# The temporary cJSON allocation cost is deliberately not hand-waved away. It
# must be measured on exact-board soak while Alarms is held open; if it proves
# expensive the accepted next seam is a bounded Core snapshot, not duplication.
assert "temporary cJSON tree" in doc
assert "minimum-heap collapse or fragmentation" in doc
assert "bounded Core snapshot/projection seam" in doc

print("Waveshare native backend parity gate: PASS")
