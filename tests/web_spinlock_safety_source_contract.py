#!/usr/bin/env python3
"""Keep web-component spinlocks free of allocation, I/O, logging and blocking work."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "components" / "web_server"

FORBIDDEN = (
    "cJSON_",
    "malloc(",
    "calloc(",
    "realloc(",
    "free(",
    "httpd_resp_",
    "httpd_req_recv(",
    "ESP_LOG",
    "nvs_open(",
    "nvs_get_",
    "nvs_set_",
    "nvs_commit(",
    "modbus_",
    "meter_manager_read_registers(",
    "vTaskDelay(",
    "xSemaphoreTake(",
    "xQueueReceive(",
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def critical_spans(source: str, path: Path):
    cursor = 0
    while True:
        start = source.find("portENTER_CRITICAL", cursor)
        if start < 0:
            return
        end = source.find("portEXIT_CRITICAL", start)
        require(end > start, f"{path}: unbalanced portENTER_CRITICAL")
        yield source[start:end]
        cursor = end + len("portEXIT_CRITICAL")


files = sorted(WEB.glob("*.c"))
require(files, "web_server C sources not found")
span_count = 0
for path in files:
    source = path.read_text(encoding="utf-8")
    for span in critical_spans(source, path.relative_to(ROOT)):
        span_count += 1
        for token in FORBIDDEN:
            require(token not in span,
                    f"{path.relative_to(ROOT)}: {token} inside spinlock critical section")

require(span_count > 0, "contract did not inspect any web-component critical sections")

# The high-volume operator history/event/alarm endpoints must snapshot shared
# state while locked, then allocate/serialize after interrupts are restored.
OP = (WEB / "operational_api.c").read_text(encoding="utf-8")
for token in (
    "operational_sample_t *snapshot = calloc",
    "operational_event_t *snapshot = calloc",
    "portENTER_CRITICAL(&s_lock)",
    "portEXIT_CRITICAL(&s_lock)",
    "cJSON_CreateObject()",
):
    require(token in OP, f"operational snapshot/serialization contract missing: {token}")

history = OP[OP.index("static esp_err_t history_get"):OP.index("static esp_err_t events_get")]
require(history.index("portEXIT_CRITICAL(&s_lock)") < history.index("cJSON_CreateObject()"),
        "history JSON allocation must occur after snapshot lock release")
events = OP[OP.index("static esp_err_t events_get"):OP.index("static esp_err_t alarms_get")]
require(events.index("portEXIT_CRITICAL(&s_lock)") < events.index("cJSON_CreateObject()"),
        "event JSON allocation must occur after snapshot lock release")
alarms = OP[OP.index("static esp_err_t alarms_get"):]
require(alarms.index("portEXIT_CRITICAL(&s_lock)") < alarms.index("cJSON_CreateObject()"),
        "alarm JSON allocation must occur after snapshot lock release")

# Engineering authentication performs PBKDF2, cookie parsing and response I/O
# outside the spinlock; the lock protects only small state copies/mutations.
AUTH = (WEB / "engineering_auth.c").read_text(encoding="utf-8")
verify = AUTH[AUTH.index("static bool verify_password"):AUTH.index("static void bytes_to_hex")]
require(verify.index("portEXIT_CRITICAL(&s_lock)") < verify.index("derive_password_hash"),
        "password derivation must never execute with interrupts disabled")

print(f"Web spinlock safety contract passed across {span_count} critical sections")
