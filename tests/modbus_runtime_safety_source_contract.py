from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODBUS = (ROOT / "components/modbus_tcp/modbus_tcp.c").read_text(encoding="utf-8")
DECODER = (ROOT / "components/modbus_tcp/modbus_decoder.c").read_text(encoding="utf-8")
METER = (ROOT / "components/meter_manager/meter_manager.c").read_text(encoding="utf-8")
CONTROL = (ROOT / "components/control_engine/control_engine.c").read_text(encoding="utf-8")


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

require("if (!isfinite(value)" in CONTROL,
        "control clamp must fail closed for non-finite input")
require("measurement_valid" in CONTROL and "requested_kw = 0.0f" in CONTROL,
        "control loop must block calculations on invalid source data")

print("Modbus and numeric runtime safety source contract passed")
