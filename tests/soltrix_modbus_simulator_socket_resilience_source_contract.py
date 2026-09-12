"""Regression coverage for a real bug found live on the bench 2026-09-07.

The board's Wi-Fi link was weak (RSSI -73 dBm) and its TCP connection to the
simulator got force-reset. The simulator's per-connection socket had no
'error' listener, so Node treated the reset as an unhandled exception and
crashed the ENTIRE process -- silently killing every other device's
connection with it. The board then spent several minutes accumulating
connection-refused errors against a simulator that no longer existed, which
looked like a firmware reconnect bug until the simulator's own log was
checked and showed the crash.

One flaky client must never be able to take the whole simulator down.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SIM = (ROOT / "tools/soltrix_modbus_simulator.js").read_text(encoding="utf-8")
SIM_TEST = (ROOT / "tools/soltrix_modbus_simulator_test.js").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


create_server_start = SIM.index("function createServer(")
create_server_end = SIM.index("\nfunction ", create_server_start + 1)
create_server_body = SIM[create_server_start:create_server_end]

require(
    "socket.on('error'" in create_server_body,
    "createServer's per-connection socket must have an 'error' listener -- "
    "without one, a client's TCP reset is an unhandled exception that "
    "crashes the whole process, taking every other connection down with it",
)
error_idx = create_server_body.index("socket.on('error'")
data_idx = create_server_body.index("socket.on('data'")
require(
    error_idx < data_idx,
    "the error handler must be attached before 'data' arrives, so a reset "
    "racing the first request is still caught",
)

require(
    "resetAndDestroy" in SIM_TEST,
    "the regression test must actually force a hard TCP reset (FIN alone "
    "will not reproduce the crash), not merely close the connection politely",
)
require(
    "const survived = await readI32" in SIM_TEST,
    "the regression test must prove the server still answers a fresh "
    "request after the reset, not just that no exception was printed",
)

print("SolTrix simulator socket-resilience source contract passed")
