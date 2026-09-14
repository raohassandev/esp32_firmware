"""Regression coverage for the receive-side transport resynchronisation added
against the live ZLAN gateway defect.

Physically proven on hardware (candidate 1ebbdf3ee16cb9df3514d9a28d3bbe94f13320eb,
see docs/ZLAN_GATEWAY_FIX.md and issue #174): with the ESP32 completely off the
Modbus bus, a single strict client still received frames carrying transaction ids
and unit ids nobody sent -- the gateway's own internal RTU poller cache, handed to
the wrong TCP session. The firmware's job is to detect and discard those frames
without ever accepting mismatched data and without replaying a write.

This contract locks the properties that make that safe:
  - a response is accepted only when both transaction id AND unit id match;
  - a mismatched frame is drained (its declared length consumed) and discarded,
    never returned to the caller as data;
  - draining happens under the SAME original deadline, not a fresh one;
  - there is a bounded limit on how many stale frames one exchange will drain,
    so a wedged/flooding gateway cannot hang a meter task forever;
  - the original request is transmitted exactly once -- this is receive
    resynchronisation, not a retry, and it must never apply to writes.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODBUS = (ROOT / "components/modbus_tcp/modbus_tcp.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


# --- the match condition: transaction id AND unit id, not transaction id alone ---
require(
    "transaction == c->transaction_id &&" in MODBUS
    and "header[6] == c->endpoint.unit_id" in MODBUS,
    "current_frame acceptance must require both the transaction id and the "
    "unit id to match -- a transaction-id-only check would have accepted the "
    "wrong slave's payload under a matching id, which is exactly the "
    "cross-delivery observed from the live ZLAN gateway",
)

# --- a mismatched frame is drained, not just skipped ---
require(
    "stale_body[MODBUS_MAX_MBAP_LENGTH - 1U]" in MODBUS,
    "a scratch buffer sized to the maximum MBAP body must exist to drain a "
    "stale frame's declared length off the socket",
)
require(
    "recv_all(c->socket_fd, stale_body, body_len, deadline_us)" in MODBUS,
    "a mismatched frame's body must be read off the socket (drained) rather "
    "than left in the stream, or the next read would desynchronise on it",
)

# --- draining stays inside the ORIGINAL deadline, never a fresh one ---
require(
    MODBUS.count("deadline_us") >= 6,
    "the deadline_us parameter must thread through header receive, stale-body "
    "drain and payload receive alike -- resynchronisation must not extend the "
    "caller's original timeout budget",
)

# --- bounded stale-frame drain: cannot hang forever on a flooding/wedged gateway ---
require(
    "MODBUS_MAX_STALE_FRAMES_PER_EXCHANGE" in MODBUS,
    "the number of stale frames drained per exchange must be bounded",
)
require(
    "stale_frames > MODBUS_MAX_STALE_FRAMES_PER_EXCHANGE" in MODBUS
    and "return ESP_ERR_INVALID_RESPONSE;" in MODBUS,
    "exceeding the stale-frame bound must fail the exchange rather than loop "
    "indefinitely waiting for a frame that may never arrive",
)

# --- a mismatched frame must never be treated as the answer ---
require(
    "continue;" in MODBUS,
    "a mismatched (stale) frame must loop back to wait for the current "
    "transaction, not fall through into payload interpretation",
)

# --- this is resync, not retry: the request must not be retransmitted ---
require(
    "receive\n     * resynchronisation, not a same-call retry/replay." in MODBUS
    or "resynchronisation, not a same-call retry/replay" in MODBUS,
    "the resync path must be documented as receive-side only; retransmitting "
    "a request whose delivery is uncertain would risk a duplicate write",
)

print(
    "Modbus receive resync source contract passed "
    "(TID+unit match required, stale frames drained under the original "
    "deadline, bounded drain count, no retransmit)"
)
