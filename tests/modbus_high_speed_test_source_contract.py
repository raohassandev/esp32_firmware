"""Regression coverage for tools/modbus_high_speed_test.js.

This tool measures the TCP-loopback ceiling of the firmware's own
request/response code path against the standalone SolTrix simulator. That
number is easy to misquote as "the achieved field Modbus rate" once it looks
good on a screen -- it is not: the real RS485 leg past the ZLAN gateway is
bounded by Modbus RTU physics (t3.5 + byte time), not by this code path.
This contract locks in the honesty guardrails, the serialized
(one-outstanding-transaction) pattern, and strict MBAP response matching so
stale/cross-delivered responses can never be counted as successful latency
samples.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = (ROOT / "tools/modbus_high_speed_test.js").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


# --- the tool must disclose it is not proving the real field rate ---
require(
    "does NOT measure the real ZLAN gateway" in TOOL,
    "the tool must disclose loopback-vs-real-bus scope so its numbers are "
    "never mistaken for a physically proven field transaction rate",
)
require(
    "docs/ZLAN_GATEWAY_FIX.md" in TOOL and "docs/MODBUS_ARCHITECTURE_PROMPT.md" in TOOL,
    "the tool must point back at the physical acceptance test and the "
    "derived RTU timing floor, not stand alone as if it were sufficient "
    "evidence",
)

# --- the burst must be serialized (one outstanding transaction), matching
#     the firmware's own client, not pipelined (which no real RTU bus could
#     ever sustain and would make the numbers meaningless for this purpose)
require(
    "sendNext()" in TOOL and "sent >= transactions" in TOOL,
    "the burst must send one transaction, wait for its reply, then send the "
    "next -- exactly the firmware's blocking request/response pattern",
)
send_idx = TOOL.index("function sendNext")
data_idx = TOOL.index("client.on('data'")
require(
    send_idx < data_idx,
    "sendNext must be defined before the response handler that re-invokes it",
)

# --- every received frame must be tied to the one outstanding request before
#     it is allowed to become a latency sample. This is the same hard rule used
#     after the ZLAN cross-delivery defect: wrong TID or unit-id is not data. ---
require(
    "function validateReadResponse" in TOOL,
    "the test must have an explicit response validator rather than assuming "
    "the next TCP frame belongs to the outstanding request",
)
for token in (
    "transactionId !== expectedTransactionId",
    "unitId !== expectedUnitId",
    "protocolId !== 0",
    "functionCode !== 3",
    "byteCount !== expectedBytes",
):
    require(token in TOOL, f"strict Modbus response check missing: {token}")

validate_idx = TOOL.index("validateReadResponse(frame, inFlightTransactionId, unitId, count)")
latency_idx = TOOL.index("latenciesMs.push(elapsedMs)")
require(
    validate_idx < latency_idx,
    "a frame must pass strict TID/unit/function/payload validation before its "
    "latency is counted as a successful sample",
)
require(
    "stale/cross-delivered" in TOOL,
    "the source comment must preserve why strict matching is non-negotiable",
)

# --- it must exercise the real simulator devices/registers, not synthetic ones ---
require(
    "require('./soltrix_modbus_simulator.js')" in TOOL,
    "the high-speed test must run against the actual standalone simulator, "
    "not a bespoke stub that could drift from it",
)
for token in ("unitId: 31", "unitId: 41", "address: 58"):
    require(token in TOOL, f"expected test case referencing {token} is missing")

print("Modbus high-speed test tool source contract passed")
