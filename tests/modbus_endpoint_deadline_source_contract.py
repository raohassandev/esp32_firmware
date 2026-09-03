#!/usr/bin/env python3
"""A configured Modbus endpoint must not enter synchronous DNS resolution."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CMAKE = (ROOT / "components/modbus_tcp/CMakeLists.txt").read_text(encoding="utf-8")
GUARD = (ROOT / "components/modbus_tcp/modbus_endpoint_guard.c").read_text(encoding="utf-8")
CORE = (ROOT / "components/modbus_tcp/modbus_tcp.c").read_text(encoding="utf-8")
HEADER = (ROOT / "components/modbus_tcp/include/modbus_tcp.h").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


for token in (
    '"modbus_endpoint_guard.c"',
    'modbus_tcp_connection_init=modbus_tcp_connection_init_core',
):
    require(token in CMAKE, f"public Modbus init is not routed through endpoint guard: {token}")

for token in (
    'modbus_tcp_connection_init_core',
    'inet_pton(AF_INET, endpoint->host, &address) != 1',
    'return ESP_ERR_INVALID_ARG;',
):
    require(token in GUARD, f"literal-IPv4 fail-closed guard missing: {token}")

require('modbus_tcp_connection_init(' in HEADER,
        "public Modbus connection initializer disappeared")
require('getaddrinfo(' in CORE,
        "legacy compatibility resolver unexpectedly moved; review this contract")
require('inet_pton(AF_INET, c->endpoint.host' in CORE,
        "literal IPv4 transport fast path is missing")

# The synchronous resolver is acceptable only as unreachable legacy code behind
# the source-local renamed initializer. Removing it later is fine, but allowing
# the public initializer to accept a hostname is not.
require('synchronous DNS is not covered by the transaction deadline' in CMAKE,
        "deadline rationale for rejecting hostnames is missing")
require('stall meter/inverter acquisition and safety-control freshness' in GUARD,
        "safety rationale for the fail-closed endpoint boundary is missing")

print("Modbus endpoint resolution deadline contract passed")
