#!/usr/bin/env python3
"""One-shot refactor for the Waveshare backend parity lane.

Turns the existing authoritative operational HTTP payload construction into
shared in-process cJSON builders, then binds the native LCD provider to those
same builders.  The web handlers remain thin wrappers around the same builders,
so alarm lifecycle / acknowledgement / suppression / priority / causality logic
has exactly one implementation.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OP_C = ROOT / "components/web_server/operational_api.c"
OP_H = ROOT / "components/web_server/include/operational_api.h"
PROVIDER = ROOT / "boards/waveshare_esp32_s3_touch_lcd_5/screen/product_800x480/main/local_backend_provider.c"
TEST = ROOT / "tests/waveshare_backend_parity_gate.py"


def function_span(text: str, signature: str) -> tuple[int, int, str]:
    start = text.index(signature)
    brace = text.index("{", start)
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return start, i + 1, text[start : i + 1]
    raise AssertionError(f"unclosed function: {signature}")


def replace_function(text: str, signature: str, replacement: str) -> str:
    start, end, _ = function_span(text, signature)
    return text[:start] + replacement + text[end:]


# -------------------------------------------------------------------------
# Public builder seam.  Forward-declare cJSON so ordinary users of the HTTP
# registration API do not inherit the whole cJSON header unnecessarily.
# -------------------------------------------------------------------------
h = OP_H.read_text(encoding="utf-8")
assert "operational_api_build_events_json" not in h
h = h.replace(
    '#include "esp_http_server.h"\n',
    '#include "esp_http_server.h"\n\ntypedef struct cJSON cJSON;\n',
    1,
)
h = h.replace(
    "esp_err_t operational_api_register(httpd_handle_t server);",
    "/* Authoritative operational payload builders shared by the HTTP API and\n"
    " * the same-MCU native HMI. Caller owns the returned cJSON object and must\n"
    " * release it with cJSON_Delete(). NULL means the snapshot could not be\n"
    " * constructed. Keeping the builder here prevents the LCD from re-deriving\n"
    " * alarm lifecycle, suppression, causality, priority or event wording. */\n"
    "cJSON *operational_api_build_events_json(void);\n"
    "cJSON *operational_api_build_alarms_json(void);\n\n"
    "esp_err_t operational_api_register(httpd_handle_t server);",
    1,
)
OP_H.write_text(h, encoding="utf-8")


# -------------------------------------------------------------------------
# Refactor the two read-only handlers into shared builders plus thin HTTP
# wrappers.  The payload-building body itself is moved, not copied.
# -------------------------------------------------------------------------
c = OP_C.read_text(encoding="utf-8")

EVENT_SIG = "static esp_err_t events_get(httpd_req_t *request)"
_, _, event_func = function_span(c, EVENT_SIG)
event_builder = event_func.replace(
    EVENT_SIG, "cJSON *operational_api_build_events_json(void)", 1
)
event_builder = event_builder.replace(
    "if (!snapshot) return httpd_resp_send_500(request);",
    "if (!snapshot) return NULL;",
    1,
)
event_builder = event_builder.replace(
    "        return httpd_resp_send_500(request);",
    "        return NULL;",
    1,
)
event_builder = event_builder.replace(
    "    return send_json(request, root);",
    "    return root;",
    1,
)
assert "request" not in event_builder, "events builder still depends on HTTP request"
event_wrapper = """

static esp_err_t events_get(httpd_req_t *request)
{
    cJSON *root = operational_api_build_events_json();
    if (!root) return httpd_resp_send_500(request);
    return send_json(request, root);
}
"""
c = replace_function(c, EVENT_SIG, event_builder + event_wrapper.rstrip())

ALARM_SIG = "static esp_err_t alarms_get(httpd_req_t *request)"
_, _, alarm_func = function_span(c, ALARM_SIG)
alarm_builder = alarm_func.replace(
    ALARM_SIG, "cJSON *operational_api_build_alarms_json(void)", 1
)
alarm_builder = alarm_builder.replace(
    "if (!root) return httpd_resp_send_500(request);",
    "if (!root) return NULL;",
    1,
)
alarm_builder = alarm_builder.replace(
    "    return send_json(request, root);",
    "    return root;",
    1,
)
assert "request" not in alarm_builder, "alarms builder still depends on HTTP request"
alarm_wrapper = """

static esp_err_t alarms_get(httpd_req_t *request)
{
    cJSON *root = operational_api_build_alarms_json();
    if (!root) return httpd_resp_send_500(request);
    return send_json(request, root);
}
"""
c = replace_function(c, ALARM_SIG, alarm_builder + alarm_wrapper.rstrip())
OP_C.write_text(c, encoding="utf-8")


# -------------------------------------------------------------------------
# Native provider: allocate bounded PSRAM slots and print the exact same Core
# cJSON tree into them. No self-HTTP, no alarm/event reimplementation.
# -------------------------------------------------------------------------
p = PROVIDER.read_text(encoding="utf-8")
assert "operational_api_build_events_json" not in p
p = p.replace(
    '#include "network_manager.h"\n',
    '#include "network_manager.h"\n#include "operational_api.h"\n',
    1,
)
p = p.replace(
    "    /* The current alarm/event lifecycle is private to operational_api.c.  Do\n"
    "     * not fabricate an empty alarm system. These two stay unavailable until the\n"
    "     * Core exposes an authoritative in-process snapshot/builder. */\n"
    "    {SCREEN_API_EVENTS_PATH,        0U,  NULL, false, 0U},\n"
    "    {SCREEN_API_ALARMS_PATH,        0U,  NULL, false, 0U},",
    "    /* Exact operational payloads come from the same Core-owned builders as\n"
    "     * the HTTP API. Events can contain the full 96-entry ring, so keep this\n"
    "     * slot larger than the LCD's bounded 16-row projection. */\n"
    "    {SCREEN_API_EVENTS_PATH,    49152U,  NULL, false, 0U},\n"
    "    {SCREEN_API_ALARMS_PATH,    32768U,  NULL, false, 0U},",
    1,
)
p = p.replace("static bool s_logged_operations_boundary;\n", "", 1)
p = p.replace("    s_logged_operations_boundary = false;\n", "", 1)
p = p.replace("    s_logged_operations_boundary = false;\n", "", 1)

insert_before = "static bool build_telemetry(local_api_slot_t *slot)\n"
assert insert_before in p
shared_builder = """
static bool build_operational(local_api_slot_t *slot, bool alarms)
{
    cJSON *root = alarms ? operational_api_build_alarms_json()
                         : operational_api_build_events_json();
    if (!root) {
        note_failure(slot, alarms ? "Core alarm snapshot unavailable"
                                  : "Core event snapshot unavailable");
        return false;
    }
    return finish_json(slot, root);
}

"""
p = p.replace(insert_before, shared_builder + insert_before, 1)

old_boundary = """    if (slot->capacity == 0U) {
        slot->valid = false;
        if (!s_logged_operations_boundary) {
            ESP_LOGW(TAG,
                     "Alarms/events remain conservative-unavailable until Core exports authoritative operational snapshots");
            s_logged_operations_boundary = true;
        }
        return false;
    }
"""
assert old_boundary in p
p = p.replace(old_boundary, "", 1)
p = p.replace(
    "    if (strcmp(path, SCREEN_API_TELEMETRY_PATH) == 0) return build_telemetry(slot);\n"
    "    note_failure(slot, \"unsupported in-process read model\");",
    "    if (strcmp(path, SCREEN_API_TELEMETRY_PATH) == 0) return build_telemetry(slot);\n"
    "    if (strcmp(path, SCREEN_API_EVENTS_PATH) == 0) return build_operational(slot, false);\n"
    "    if (strcmp(path, SCREEN_API_ALARMS_PATH) == 0) return build_operational(slot, true);\n"
    "    note_failure(slot, \"unsupported in-process read model\");",
    1,
)
assert "s_logged_operations_boundary" not in p
assert "Alarms/events remain conservative-unavailable" not in p
PROVIDER.write_text(p, encoding="utf-8")


# -------------------------------------------------------------------------
# Gate the architectural property, not just non-zero capacities.
# -------------------------------------------------------------------------
TEST.write_text(
    '''#!/usr/bin/env python3
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
''',
    encoding="utf-8",
)

print("shared operational builder refactor staged")
