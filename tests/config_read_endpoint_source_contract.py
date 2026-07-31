#!/usr/bin/env python3
"""The configuration read endpoints must stay engineering-gated and round-trip.

GET /api/meters/config and GET /api/inverters/config exist so the commissioning
wizard can read what is already commissioned before editing it. Before they
existed the wizard opened on blank defaults and its save replaced a working
configuration with them -- that is how a meter reading 372 kW became 25 kW on a
live unit, a 15x error produced by an editor that could not see what it was
editing.

Two properties have to hold, and neither is visible from reading one file.

FIRST: these routes disclose the plant's Modbus topology -- host, port, unit id,
register address. GET /api/meters and GET /api/inverters are deliberately
operator-safe projections with exactly that information removed. The new routes
are protected only because engineering_guard.c defaults an unlisted route to
GATEWAY_MODE_PROTECTED. Adding either to public_uri(), or giving either a
GATEWAY_MODE_SAFE_* classification, would publish the topology to any
unauthenticated client on the network while every other check still passed.

SECOND: the read must be postable back unchanged. If the GET emitted
"rated_power_kw" where the POST parses "rated_kw", a client doing the obvious
read-edit-write would silently lose the field -- and losing a field is exactly
the failure this endpoint was added to prevent. So every key the POST parser
accepts must be a key the GET emits.

Both are asserted against comment-stripped source, so a key named only in a
comment cannot satisfy this contract.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
WEB_SERVER = ROOT / "components" / "web_server"

failures = []


def require(condition, message):
    if not condition:
        failures.append(message)


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def source(name):
    return strip_comments((WEB_SERVER / name).read_text(encoding="utf-8", errors="replace"))


guard = source("engineering_guard.c")
meter = source("meter_config_api.c")
inverter = source("inverter_config_api.c")

ROUTES = ("/api/meters/config", "/api/inverters/config")

# --- FIRST PROPERTY: still protected -------------------------------------

public_match = re.search(r"static bool public_uri\(const char \*uri\)\s*\{(.*?)\n\}", guard, re.DOTALL)
require(public_match is not None, "engineering_guard.c no longer defines public_uri()")
if public_match is not None:
    allowlist = public_match.group(1)
    for route in ROUTES:
        require(
            route not in allowlist,
            f"{route} appears in public_uri(): the configuration read endpoints "
            f"disclose host, port and unit id and must require an engineering session",
        )

# The SAFE_* classifications relax the guard for named routes. Neither config
# route may acquire one; the operator-safe projections are the separate
# /api/meters and /api/inverters routes and those are what these must stay
# distinct from.
classification = re.search(
    r"esp_err_t engineering_register_uri_handler\(.*?\n\}", guard, re.DOTALL
)
require(classification is not None, "engineering_guard.c no longer defines engineering_register_uri_handler()")
if classification is not None:
    body = classification.group(0)
    for route in ROUTES:
        for line in body.splitlines():
            if "GATEWAY_MODE_SAFE" in line and f'"{route}"' in line:
                failures.append(
                    f"{route} is classified {line.strip()}; a SAFE_* mode bypasses the "
                    f"engineering session this route depends on"
                )

# The default for an unlisted route is what actually protects these. If it ever
# became something other than PROTECTED, both routes would open at once with no
# edit to either file.
require(
    re.search(r"uint8_t mode = GATEWAY_MODE_PROTECTED;", guard) is not None,
    "engineering_register_uri_handler no longer defaults an unlisted route to "
    "GATEWAY_MODE_PROTECTED; the configuration read endpoints rely on that default",
)

# --- Both routes are actually registered for GET --------------------------

for name, text, route in (
    ("meter_config_api.c", meter, "/api/meters/config"),
    ("inverter_config_api.c", inverter, "/api/inverters/config"),
):
    require(
        re.search(
            r'\.uri\s*=\s*"' + re.escape(route) + r'"\s*,\s*\.method\s*=\s*HTTP_GET', text
        )
        is not None,
        f"{name} does not register {route} for HTTP_GET",
    )
    require(
        re.search(
            r'\.uri\s*=\s*"' + re.escape(route) + r'"\s*,\s*\.method\s*=\s*HTTP_POST', text
        )
        is not None,
        f"{name} no longer registers {route} for HTTP_POST",
    )

# --- SECOND PROPERTY: every parsed key is an emitted key -------------------

def parsed_keys(text, reader_pattern):
    return set(re.findall(reader_pattern, text))


def emitted_keys(text):
    return set(re.findall(r'cJSON_Add\w+ToObject\(item,\s*"([a-z_]+)"', text))


meter_parsed = parsed_keys(meter, r'read_optional_\w+\(object,\s*"([a-z_]+)"')
require(
    len(meter_parsed) >= 10,
    f"only {len(meter_parsed)} meter POST keys were found; the extraction pattern "
    f"has probably stopped matching and this contract is no longer checking anything",
)
missing = sorted(meter_parsed - emitted_keys(meter))
require(
    not missing,
    f"GET /api/meters/config does not emit {missing}, which POST accepts; a "
    f"read-edit-write round trip would silently drop those fields",
)

inverter_parsed = parsed_keys(inverter, r'read_(?:bool|string|integer)\(object,\s*"([a-z_]+)"')
inverter_parsed.add("rated_kw")  # read_rated_kw() hard-codes its key
require(
    len(inverter_parsed) >= 6,
    f"only {len(inverter_parsed)} inverter POST keys were found; the extraction "
    f"pattern has probably stopped matching",
)
missing = sorted(inverter_parsed - emitted_keys(inverter))
require(
    not missing,
    f"GET /api/inverters/config does not emit {missing}, which POST accepts; a "
    f"read-edit-write round trip would silently drop those fields",
)

# --- The read must not touch the bus --------------------------------------

# A configuration read is served from persisted config. If it ever issued a
# Modbus transaction it would block the HTTP task on a device that may be
# unreachable, which is the failure mode the whole acquisition design exists to
# avoid.
for name, text in (("meter_config_api.c", meter), ("inverter_config_api.c", inverter)):
    require(
        "modbus_tcp_read" not in text and "modbus_tcp_write" not in text,
        f"{name} issues a Modbus transaction; configuration endpoints must be "
        f"served from persisted configuration only",
    )

if failures:
    print("Configuration read endpoint contract FAILED:")
    for failure in failures:
        print(f"  - {failure}")
    sys.exit(1)

print(
    "Configuration read endpoint contract passed "
    f"({len(meter_parsed)} meter keys, {len(inverter_parsed)} inverter keys "
    "round-trip; both routes engineering-gated)"
)
