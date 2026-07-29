from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODBUS = (ROOT / "components/modbus_tcp/modbus_tcp.c").read_text(encoding="utf-8")
MODBUS_TYPES = (ROOT / "components/modbus_tcp/include/modbus_types.h").read_text(encoding="utf-8")
MODBUS_HEADER = (ROOT / "components/modbus_tcp/include/modbus_tcp.h").read_text(encoding="utf-8")
DECODER = (ROOT / "components/modbus_tcp/modbus_decoder.c").read_text(encoding="utf-8")
METER = (ROOT / "components/meter_manager/meter_manager.c").read_text(encoding="utf-8")
METER_TYPES = (ROOT / "components/meter_manager/include/meter_types.h").read_text(encoding="utf-8")
CACHE_API = (ROOT / "components/web_server/em500_cache_api.c").read_text(encoding="utf-8")
QUALITY_UI = (ROOT / "web/em500-quality.js").read_text(encoding="utf-8")
CONTROL = (ROOT / "components/control_engine/control_engine.c").read_text(encoding="utf-8")
POLICY = (ROOT / "components/control_engine/power_control_policy.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


for token in [
    "MODBUS_MIN_TIMEOUT_MS 100U",
    "MODBUS_MAX_TIMEOUT_MS 60000U",
    "remaining_timeout",
    "deadline_us",
    "set_socket_deadline",
    "inet_pton(AF_INET",
    "for (struct addrinfo *entry = result; entry; entry = entry->ai_next)",
    "errno == EINTR",
    "!c->lock",
]:
    require(token in MODBUS, f"Modbus transaction hardening missing: {token}")

require("recv_all(c->socket_fd, header, sizeof(header), deadline_us)" in MODBUS,
        "MBAP header receive is not bounded by the cumulative transaction deadline")
require("recv_all(c->socket_fd, pdu, body_len, deadline_us)" in MODBUS,
        "Modbus payload receive is not bounded by the cumulative transaction deadline")
require("memset(c, 0, sizeof(*c));" in MODBUS and "c->socket_fd = -1;" in MODBUS,
        "connection state must be initialized before endpoint validation exits")

for token in [
    "last_exception_valid",
    "last_exception_function",
    "last_exception_code",
    "last_exception_ms",
    "exception_count",
]:
    require(token in MODBUS_TYPES, f"Modbus connection exception field missing: {token}")
require("pdu[0] == (expected_function | 0x80U)" in MODBUS,
        "Modbus exception response function is not detected")
require("body_len != 2U" in MODBUS,
        "Modbus exception PDU length is not validated")
require("c->last_exception_function = pdu[0]" in MODBUS and
        "c->last_exception_code = pdu[1]" in MODBUS,
        "exception function/code are not preserved")
require("c->last_exception_ms" in MODBUS and "c->exception_count++" in MODBUS,
        "exception time/count are not preserved")
require("modbus_tcp_get_last_exception" in MODBUS and
        "modbus_tcp_get_last_exception" in MODBUS_HEADER,
        "thread-safe exception snapshot API is missing")
require("last_exception_valid = false" not in MODBUS,
        "later successful or transport-failed polls must not erase the last device exception")

for token in [
    "last_modbus_exception_valid",
    "last_modbus_exception_function",
    "last_modbus_exception_code",
    "last_modbus_exception_ms",
    "modbus_exception_count",
]:
    require(token in METER_TYPES, f"meter exception runtime field missing: {token}")
require("capture_modbus_exception" in METER and
        "modbus_tcp_get_last_exception(&meter->connection" in METER,
        "background meter transactions do not propagate preserved exceptions")
require(METER.index("capture_modbus_exception(meter)") < METER.index("xSemaphoreGive(meter->io_mutex)"),
        "exception state must be captured before releasing the serialized meter transaction")
for token in [
    '"last_modbus_exception"',
    '"function"',
    '"request_function"',
    '"code"',
    '"received_ms"',
    '"age_ms"',
    '"count"',
]:
    require(token in CACHE_API, f"non-blocking exception API field missing: {token}")
require('"modbus_io_in_http_handler", false' in CACHE_API,
        "exception diagnostics must not add Modbus I/O to the HTTP handler")
require("Preserved Modbus exception" in QUALITY_UI and
        "Later successful polls do not erase this diagnostic." in QUALITY_UI,
        "Engineering analyser does not display the preserved exception")

require("isfinite(scale)" in DECODER and "isfinite(offset)" in DECODER,
        "scaled decoder must reject non-finite configuration")
require("isfinite(value)" in DECODER and "isfinite(scaled)" in DECODER,
        "scaled decoder must reject NaN, infinity, and non-finite scaled results")
require("isfinite(decoded)" in METER,
        "meter boundary must reject non-finite decoded data")
require("s_meter_count = cfg->meter_count <= APP_MAX_METERS" in METER,
        "meter runtime count is not clamped")
require("uint32_t ceiling = base > METER_MAX_BACKOFF_MS ? base : METER_MAX_BACKOFF_MS" in METER,
        "degraded backoff can still poll faster than the configured healthy interval")
require("return delay < base ? base" in METER,
        "degraded backoff must never be shorter than the normal interval")

require("if (!isfinite(value) || !isfinite(maximum)" in POLICY,
        "power policy clamp must fail closed for non-finite input")
require("!input->measurement_fresh" in POLICY and "output.transition_blocked = true" in POLICY,
        "power policy must block calculations on invalid source data")
require("measurement_fresh && fleet_valid" in CONTROL and
        "policy.requested_pv_kw = 0.0f" in CONTROL,
        "live control loop must request zero on invalid source data")
require("if (!isfinite(applied_kw) || applied_kw < 0.0f) applied_kw = 0.0f;" in CONTROL,
        "live applied target must fail closed for invalid safety output")

print("Modbus, exception diagnostics and numeric runtime safety source contract passed")
