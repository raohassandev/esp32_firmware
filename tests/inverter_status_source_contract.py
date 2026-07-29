#!/usr/bin/env python3
"""Inverter operational status safety contract.

Status reading exists so the control engine can tell "successfully
synchronised" from "still checking / faulted / we have no idea". The whole
value of the feature is that it is honest about uncertainty, so this contract
enforces:

  * a normalised state enum whose default (value 0) is UNKNOWN;
  * UNKNOWN whenever the register is unconfigured, the read failed, the raw
    value is unmapped, or the sample is stale;
  * a fleet predicate that is true only when every enabled inverter is ON_GRID
    with a fresh sample;
  * status sampling in the background acquisition path, never in an HTTP
    handler;
  * read-only Modbus function codes 3 and 4 only, never a write;
  * NO hardcoded status register address for any manufacturer profile.

The behavioural clauses are executed, not merely grepped: the pure status
module is compiled and run natively.
"""

import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STATUS_HEADER = (ROOT / "components/inverter_manager/include/inverter_status.h").read_text(encoding="utf-8")
STATUS_SOURCE = (ROOT / "components/inverter_manager/inverter_status.c").read_text(encoding="utf-8")
TYPES = (ROOT / "components/inverter_manager/include/inverter_types.h").read_text(encoding="utf-8")
MANAGER_HEADER = (ROOT / "components/inverter_manager/include/inverter_manager.h").read_text(encoding="utf-8")
MANAGER = (ROOT / "components/inverter_manager/inverter_manager.c").read_text(encoding="utf-8")
PROFILES_HEADER = (ROOT / "components/inverter_manager/include/inverter_profiles.h").read_text(encoding="utf-8")
PROFILES = (ROOT / "components/inverter_manager/inverter_profiles.c").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/inverter_manager/CMakeLists.txt").read_text(encoding="utf-8")
API = (ROOT / "components/web_server/inverter_profile_api.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


# ---------------------------------------------------------------- enum shape
STATES = [
    "INVERTER_STATE_UNKNOWN",
    "INVERTER_STATE_OFFLINE",
    "INVERTER_STATE_STANDBY",
    "INVERTER_STATE_CHECKING",
    "INVERTER_STATE_ON_GRID",
    "INVERTER_STATE_DERATED",
    "INVERTER_STATE_FAULT",
]
for state in STATES:
    require(state in STATUS_HEADER, f"normalised state {state} is missing")

require(re.search(r"INVERTER_STATE_UNKNOWN\s*=\s*0", STATUS_HEADER) is not None,
        "UNKNOWN must be enumerator 0 so zero-initialised state is UNKNOWN")
for state in STATES[1:]:
    require(re.search(rf"{state}\s*=\s*0", STATUS_HEADER) is None,
            f"{state} must not share the zero/default enumerator with UNKNOWN")

require("inverter_state_t status_state;" in TYPES,
        "telemetry record must carry the normalised operational state")
require("status_stale" in TYPES and "last_status_ms" in TYPES and "status_raw" in TYPES,
        "telemetry record must carry the raw value, its timestamp and staleness")

# ------------------------------------------------- profile model, no guesses
require("inverter_status_register_t status_register;" in PROFILES_HEADER,
        "profile model must carry an optional status register description")
for field in ("uint8_t function;", "uint16_t address;", "uint8_t words;",
              "inverter_value_type_t type;", "inverter_word_order_t word_order;",
              "inverter_status_mapping_t mappings["):
    require(field in STATUS_HEADER,
            f"status register description must include {field}")
require("inverter_profile_decode.h" in STATUS_HEADER,
        "status register must reuse the existing data type / word order vocabulary")

require(".status_register" not in PROFILES,
        "no shipped profile may configure a status register: manufacturer status "
        "addresses are not present in this tree and must never be guessed")
require(".configured = true" not in PROFILES,
        "no shipped profile may mark a status register as configured")

# No manufacturer profile block may hardcode a status address of any kind.
for match in re.finditer(r'\.id = "([^"]+)"', PROFILES):
    profile_id = match.group(1)
    block = PROFILES[match.start():PROFILES.find("    },", match.start())]
    require("status_register" not in block,
            f"{profile_id} hardcodes a status register; commissioning evidence is required")
    require("status_address" not in block and "status_function" not in block,
            f"{profile_id} hardcodes a status address or function code")

require("inverter_profile_has_status_register" in PROFILES_HEADER,
        "catalogue must expose whether a profile has a verified status register")

# ------------------------------------------------- read-only function codes
require("INVERTER_STATUS_FUNCTION_HOLDING 3U" in STATUS_HEADER,
        "status must allow Modbus function code 3")
require("INVERTER_STATUS_FUNCTION_INPUT 4U" in STATUS_HEADER,
        "status must allow Modbus function code 4")
require("function_code == INVERTER_STATUS_FUNCTION_HOLDING" in STATUS_SOURCE and
        "function_code == INVERTER_STATUS_FUNCTION_INPUT" in STATUS_SOURCE,
        "status function codes must be validated against 3 and 4 only")

require("static esp_err_t poll_status" in MANAGER, "background status poll is missing")
status_body = MANAGER.split("static esp_err_t poll_status", 1)[1].split(
    "static void update_stale_state", 1)[0]
require("inverter_status_function_is_read_only" in status_body,
        "status poll must validate the function code before any transaction")
require("read_profile_block" in status_body,
        "status poll must use the shared read path")
for forbidden in ("modbus_tcp_write_single", "modbus_tcp_write_multiple",
                  "write_profile_command"):
    require(forbidden not in status_body,
            f"status reading must never write to an inverter ({forbidden})")

# ------------------------------------ acquired in the background task, not HTTP
require("poll_status(runtime, timestamp)" in MANAGER, "status poll must be scheduled")
task_body = MANAGER.split("static void inverter_telemetry_task", 1)[1].split(
    "esp_err_t inverter_manager_init", 1)[0]
require("poll_status(runtime, timestamp)" in task_body,
        "status must be sampled by the background acquisition task")
for forbidden in ("read_profile_block", "modbus_tcp_read_registers", "poll_status(",
                  "inverter_status_decode_raw"):
    require(forbidden not in API,
            f"HTTP handlers must never perform a blocking Modbus transaction ({forbidden})")

# ------------------------------------------------------------ API disclosure
for token in ('"status_state"', '"status_raw"', '"status_age_ms"', '"status_stale"',
              '"status_supported"', '"fleet_synchronised"'):
    require(token in API, f"inverter telemetry API must expose {token}")
require("inverter_state_label" in API, "API must publish the normalised state name")

# ------------------------------------------------------------ fleet predicate
require("bool inverter_manager_fleet_synchronised(void);" in MANAGER_HEADER,
        "fleet synchronisation predicate must be exposed for the control engine")
require("bool inverter_manager_fleet_synchronised(void)" in MANAGER,
        "fleet synchronisation predicate must be implemented")
fleet_body = MANAGER.split("bool inverter_manager_fleet_synchronised(void)", 1)[1]
require("inverter_status_fleet_synchronised(samples, count)" in fleet_body,
        "fleet predicate must delegate to the pure, tested decision function")
require("runtime->config.enabled" in fleet_body,
        "fleet predicate must only consider enabled inverters")
require("status_stale" in fleet_body and "INVERTER_STATE_UNKNOWN" in fleet_body,
        "fleet predicate must degrade a stale sample to UNKNOWN")
for forbidden in ("read_profile_block", "modbus_tcp_"):
    require(forbidden not in fleet_body,
            f"fleet predicate must not perform Modbus I/O ({forbidden})")

require('"inverter_status.c"' in CMAKE, "status module must be compiled")

# ------------------------------------------------------- executed behaviour
TEST_MAIN = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "inverter_status.h"

static inverter_status_register_t configured_register(void)
{
    inverter_status_register_t reg;
    memset(&reg, 0, sizeof(reg));
    reg.configured = true;
    reg.function = INVERTER_STATUS_FUNCTION_HOLDING;
    reg.address = 1234;
    reg.words = 1;
    reg.type = INVERTER_VALUE_U16;
    reg.word_order = INVERTER_WORD_ORDER_AB;
    reg.mapping_count = 3;
    reg.mappings[0].raw_value = 1; reg.mappings[0].state = INVERTER_STATE_CHECKING;
    reg.mappings[1].raw_value = 2; reg.mappings[1].state = INVERTER_STATE_ON_GRID;
    reg.mappings[2].raw_value = 9; reg.mappings[2].state = INVERTER_STATE_FAULT;
    return reg;
}

int main(void)
{
    /* Zero initialisation is UNKNOWN. */
    inverter_state_t zero_state;
    memset(&zero_state, 0, sizeof(zero_state));
    assert(zero_state == INVERTER_STATE_UNKNOWN);
    assert((int)INVERTER_STATE_UNKNOWN == 0);

    inverter_status_register_t unconfigured;
    memset(&unconfigured, 0, sizeof(unconfigured));
    assert(!inverter_status_register_is_configured(&unconfigured));
    assert(!inverter_status_register_is_configured(NULL));

    /* Unconfigured register -> UNKNOWN, even on a "successful" read. */
    assert(inverter_status_evaluate(&unconfigured, true, 2, 0, 5000) == INVERTER_STATE_UNKNOWN);
    assert(inverter_status_evaluate(NULL, true, 2, 0, 5000) == INVERTER_STATE_UNKNOWN);

    inverter_status_register_t reg = configured_register();
    assert(inverter_status_register_is_configured(&reg));

    /* Fresh, mapped, successful read -> the mapped state. */
    assert(inverter_status_evaluate(&reg, true, 2, 0, 5000) == INVERTER_STATE_ON_GRID);
    assert(inverter_status_evaluate(&reg, true, 1, 100, 5000) == INVERTER_STATE_CHECKING);
    assert(inverter_status_evaluate(&reg, true, 9, 100, 5000) == INVERTER_STATE_FAULT);

    /* Failed read -> UNKNOWN. */
    assert(inverter_status_evaluate(&reg, false, 2, 0, 5000) == INVERTER_STATE_UNKNOWN);
    /* Unmapped raw value -> UNKNOWN. */
    assert(inverter_status_evaluate(&reg, true, 4242, 0, 5000) == INVERTER_STATE_UNKNOWN);
    assert(inverter_status_map_raw(&reg, 4242) == INVERTER_STATE_UNKNOWN);
    /* Stale sample -> UNKNOWN. */
    assert(inverter_status_evaluate(&reg, true, 2, 5001, 5000) == INVERTER_STATE_UNKNOWN);
    assert(inverter_status_evaluate(&reg, true, 2, 1, 0) == INVERTER_STATE_UNKNOWN);

    /* Only function codes 3 and 4 are acceptable. */
    assert(inverter_status_function_is_read_only(3));
    assert(inverter_status_function_is_read_only(4));
    for (unsigned code = 0; code < 256u; ++code) {
        if (code == 3u || code == 4u) continue;
        assert(!inverter_status_function_is_read_only((uint8_t)code));
    }
    inverter_status_register_t writeish = configured_register();
    writeish.function = 6; /* write single register */
    assert(!inverter_status_register_is_configured(&writeish));
    writeish.function = 16; /* write multiple registers */
    assert(!inverter_status_register_is_configured(&writeish));

    /* Word order vocabulary is reused and honoured. */
    inverter_status_register_t wide = configured_register();
    wide.words = 2;
    wide.type = INVERTER_VALUE_U32;
    uint16_t words[2] = {0x0001u, 0x0002u};
    uint32_t raw = 0;
    assert(inverter_status_decode_raw(&wide, words, 2, &raw) && raw == 0x00010002u);
    wide.word_order = INVERTER_WORD_ORDER_BA;
    assert(inverter_status_decode_raw(&wide, words, 2, &raw) && raw == 0x00020001u);
    assert(!inverter_status_decode_raw(&unconfigured, words, 2, &raw));

    /* Only ON_GRID counts as synchronised. */
    assert(inverter_state_is_synchronised(INVERTER_STATE_ON_GRID));
    assert(!inverter_state_is_synchronised(INVERTER_STATE_UNKNOWN));
    assert(!inverter_state_is_synchronised(INVERTER_STATE_DERATED));
    assert(!inverter_state_is_synchronised(INVERTER_STATE_CHECKING));
    assert(!inverter_state_is_synchronised(INVERTER_STATE_FAULT));
    assert(!inverter_state_is_synchronised(INVERTER_STATE_STANDBY));
    assert(!inverter_state_is_synchronised(INVERTER_STATE_OFFLINE));

    /* Fleet predicate. */
    inverter_status_sample_t fleet[3];
    memset(fleet, 0, sizeof(fleet));
    assert(!inverter_status_fleet_synchronised(fleet, 0));
    assert(!inverter_status_fleet_synchronised(NULL, 3));
    /* All zeroed: disabled everywhere -> no evidence -> false. */
    assert(!inverter_status_fleet_synchronised(fleet, 3));

    for (int i = 0; i < 3; ++i) {
        fleet[i].enabled = true;
        fleet[i].sample_fresh = true;
        fleet[i].state = INVERTER_STATE_ON_GRID;
    }
    assert(inverter_status_fleet_synchronised(fleet, 3));

    /* One UNKNOWN breaks it. */
    fleet[1].state = INVERTER_STATE_UNKNOWN;
    assert(!inverter_status_fleet_synchronised(fleet, 3));
    fleet[1].state = INVERTER_STATE_DERATED;
    assert(!inverter_status_fleet_synchronised(fleet, 3));
    fleet[1].state = INVERTER_STATE_ON_GRID;
    assert(inverter_status_fleet_synchronised(fleet, 3));

    /* One stale sample breaks it. */
    fleet[2].sample_fresh = false;
    assert(!inverter_status_fleet_synchronised(fleet, 3));
    fleet[2].sample_fresh = true;
    assert(inverter_status_fleet_synchronised(fleet, 3));

    /* Disabled inverters are ignored, but at least one must be enabled. */
    fleet[2].enabled = false;
    fleet[2].state = INVERTER_STATE_FAULT;
    fleet[2].sample_fresh = false;
    assert(inverter_status_fleet_synchronised(fleet, 3));
    fleet[0].enabled = false;
    fleet[1].enabled = false;
    assert(!inverter_status_fleet_synchronised(fleet, 3));

    printf("inverter status behaviour tests passed\n");
    return 0;
}
"""

ESP_ERR_STUB = """
#pragma once
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_SUPPORTED 0x106
"""

with tempfile.TemporaryDirectory() as directory:
    work = Path(directory)
    (work / "esp_err.h").write_text(ESP_ERR_STUB, encoding="utf-8")
    main_c = work / "inverter_status_behaviour_test.c"
    main_c.write_text(TEST_MAIN, encoding="utf-8")
    binary = work / "inverter_status_behaviour_test"
    subprocess.run([
        "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I", str(ROOT / "components/inverter_manager/include"),
        "-I", str(work),
        str(main_c),
        str(ROOT / "components/inverter_manager/inverter_status.c"),
        "-lm", "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)

print("inverter operational status source contract passed")
