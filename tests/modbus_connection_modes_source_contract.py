#!/usr/bin/env python3
"""Source contract for configurable Modbus TCP connection modes.

This lane deliberately keeps physical PCB/TIME_WAIT/endurance out of software
acceptance. These checks lock the software semantics and migration/API surface
that can be proven in CI.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TYPES = (ROOT / "components/modbus_tcp/include/modbus_types.h").read_text(encoding="utf-8")
POLICY_H = (ROOT / "components/modbus_tcp/include/modbus_connection_policy.h").read_text(encoding="utf-8")
POLICY_C = (ROOT / "components/modbus_tcp/modbus_connection_policy.c").read_text(encoding="utf-8")
HEADER = (ROOT / "components/modbus_tcp/include/modbus_tcp.h").read_text(encoding="utf-8")
TRANSPORT = (ROOT / "components/modbus_tcp/modbus_tcp.c").read_text(encoding="utf-8")
MODBUS_CMAKE = (ROOT / "components/modbus_tcp/CMakeLists.txt").read_text(encoding="utf-8")
CONFIG_TYPES = (ROOT / "components/config_manager/include/config_types.h").read_text(encoding="utf-8")
CONFIG_V6 = (ROOT / "components/config_manager/config_manager_v6.c").read_text(encoding="utf-8")
CONFIG_CMAKE = (ROOT / "components/config_manager/CMakeLists.txt").read_text(encoding="utf-8")
DIAGNOSTICS = (ROOT / "components/web_server/modbus_connection_api.c").read_text(encoding="utf-8")
WEB_SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")
WEB_CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


# Stable persisted enum values: schema migration depends on legacy behaviour == 0.
for token in (
    "MODBUS_CONNECTION_PER_TRANSACTION = 0",
    "MODBUS_CONNECTION_PERSISTENT = 1",
    "MODBUS_CONNECTION_RECONNECT_ON_ERROR = 2",
):
    require(token in POLICY_H, f"missing connection-mode enum contract: {token}")
require('#include "modbus_connection_policy.h"' in TYPES and
        "uint8_t connection_mode" in TYPES,
        "persisted endpoint is not tied to the shared connection policy")
require('"modbus_connection_policy.c"' in MODBUS_CMAKE,
        "connection policy helper is not compiled into the Modbus component")
require("#define APP_CONFIG_VERSION 6u" in CONFIG_TYPES,
        "connection-mode schema must remain version 6")
require("modbus_tcp_connection_mode_name" in HEADER,
        "mode name API missing from public Modbus header")
for name in ("per_transaction", "persistent", "reconnect_on_error"):
    require(f'"{name}"' in TRANSPORT, f"transport missing mode name {name}")

# Transport policy. A valid Modbus exception is a device response, not broken TCP.
require("modbus_connection_should_close(c->endpoint.connection_mode" in TRANSPORT,
        "transport does not use the tested connection-close policy")
require("result == ESP_OK" in TRANSPORT and "device_exception" in TRANSPORT,
        "transport does not pass transaction/device-exception state to close policy")
require("c->last_exchange_device_exception = true" in TRANSPORT,
        "valid Modbus exceptions are not classified as device responses")
require("NEXT caller transaction" in TRANSPORT and "same-call retry" in TRANSPORT,
        "no-same-call-replay safety contract is not explicit")
require("!modbus_connection_mode_valid(endpoint->connection_mode)" in TRANSPORT,
        "connection init does not reject invalid modes through shared policy")
for token in (
    "if (!modbus_connection_mode_valid(mode)) return true",
    "mode == MODBUS_CONNECTION_PER_TRANSACTION",
    "if (transaction_ok) return false",
    "mode == MODBUS_CONNECTION_RECONNECT_ON_ERROR",
    "return !device_exception",
):
    require(token in POLICY_C, f"connection policy implementation missing: {token}")


def function_body(signature: str, next_signature: str) -> str:
    require(signature in TRANSPORT, f"missing transport function {signature}")
    body = TRANSPORT.split(signature, 1)[1]
    if next_signature:
        require(next_signature in body, f"cannot bound transport function {signature}")
        body = body.split(next_signature, 1)[0]
    return body


read_body = function_body("esp_err_t modbus_tcp_read_registers", "esp_err_t modbus_tcp_write_single")
single_body = function_body("esp_err_t modbus_tcp_write_single", "esp_err_t modbus_tcp_write_multiple")
multiple_body = function_body("esp_err_t modbus_tcp_write_multiple", "bool modbus_tcp_get_last_exception")
for label, body in (("read", read_body), ("write_single", single_body), ("write_multiple", multiple_body)):
    require(body.count("exchange(c,") == 1,
            f"{label} must perform exactly one exchange; transparent replay is forbidden")
    require("finish_transaction(c, err)" in body,
            f"{label} does not apply connection-mode finish policy")

# Expected socket-close truth table mirrors the compiled pure-C policy, whose
# host unit test independently executes every row.
PER_TRANSACTION = 0
PERSISTENT = 1
RECONNECT_ON_ERROR = 2


def should_close(mode: int, ok: bool, device_exception: bool) -> bool:
    if mode not in (PER_TRANSACTION, PERSISTENT, RECONNECT_ON_ERROR):
        return True
    if mode == PER_TRANSACTION:
        return True
    if ok:
        return False
    if mode == RECONNECT_ON_ERROR:
        return True
    return not device_exception


cases = {
    (PER_TRANSACTION, True, False): True,
    (PER_TRANSACTION, False, True): True,
    (PER_TRANSACTION, False, False): True,
    (PERSISTENT, True, False): False,
    (PERSISTENT, False, True): False,
    (PERSISTENT, False, False): True,
    (RECONNECT_ON_ERROR, True, False): False,
    (RECONNECT_ON_ERROR, False, True): True,
    (RECONNECT_ON_ERROR, False, False): True,
    (99, True, False): True,
}
for inputs, expected in cases.items():
    require(should_close(*inputs) is expected, f"connection close matrix failed for {inputs}")

# Schema 5 has the same byte size as schema 6. It must be recognized by version
# before the old core rejects it, and all legacy endpoint padding must normalize
# to per_transaction while preserving every other commissioned field byte-for-byte.
for token in (
    "version == 5U",
    "size != sizeof(app_config_t)",
    "normalize_modes_to_per_transaction",
    "migrated.version = APP_CONFIG_VERSION",
    "persist_blob_exact",
    "legacy_schema",
    "version < APP_CONFIG_VERSION",
    "config_manager_get_snapshot(&snapshot)",
):
    require(token in CONFIG_V6, f"schema-6 migration contract missing: {token}")
require("for (uint8_t index = 0; index < APP_MAX_METERS; ++index)" in CONFIG_V6,
        "legacy migration must normalize every meter endpoint slot")
require("for (uint8_t index = 0; index < APP_MAX_INVERTERS; ++index)" in CONFIG_V6,
        "legacy migration must normalize every inverter endpoint slot")
require("nvs_flash_erase(" not in CONFIG_V6,
        "schema-6 boundary must never erase NVS")
require("nvs_flash_erase=config_manager_forbidden_nvs_erase" in CONFIG_CMAKE,
        "legacy destructive NVS recovery is not intercepted")
require("Refusing destructive NVS erase" in CONFIG_V6,
        "intercepted NVS erase must fail closed visibly")

# Every configured meter and inverter is validated before public persistence.
require("configured_modes_valid(config)" in CONFIG_V6,
        "public config save does not validate connection modes")
require("index < config->meter_count" in CONFIG_V6 and
        "index < config->inverter_count" in CONFIG_V6,
        "mode validation does not cover configured meter and inverter arrays")

# Configuration API accepts and exports names/codes for both endpoint classes.
for token in (
    '"connection_mode"',
    '"connection_mode_code"',
    '"per_transaction"',
    '"persistent"',
    '"reconnect_on_error"',
    "modes.meter_present",
    "modes.inverter_present",
    "config_manager_import_json_core",
    "config_manager_export_json_core",
):
    require(token in CONFIG_V6, f"configuration API mode contract missing: {token}")

# Diagnostics are a separate read-only surface and must report both meters and
# inverters plus the explicit no-replay policy.
for token in (
    '"/api/modbus/connections"',
    '"meters"',
    '"inverters"',
    '"connection_mode"',
    '"same_call_retry"',
    "modbus_tcp_connection_mode_name",
):
    require(token in DIAGNOSTICS, f"diagnostics mode contract missing: {token}")
require('"modbus_connection_api.c"' in WEB_CMAKE,
        "connection diagnostics source is not compiled")
require('#include "modbus_connection_api.h"' in WEB_SERVER and
        "modbus_connection_api_register(s_server)" in WEB_SERVER,
        "connection diagnostics endpoint is not registered")

print("modbus connection-mode source contract: PASS")
