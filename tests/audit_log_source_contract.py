#!/usr/bin/env python3
"""Controller audit trail and field-service identity source contract.

The audit trail exists so that a control action, a configuration write or an
authentication event can be reconstructed after an incident. This contract
protects the properties that make it trustworthy rather than merely present:

  * all three audited categories are actually recorded from real code paths;
  * a FAILED authentication attempt is recorded;
  * no credential, credential length or session token can reach a log line;
  * reading the trail requires an authenticated engineering session;
  * nothing about the unit's identity is invented - no serial number, no
    hardware revision, no wall-clock date, no operator name.

It also runs the storage core as a host unit test.
"""

import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "components/web_server"

CORE_H = (WEB / "include/audit_log_core.h").read_text(encoding="utf-8")
CORE_C = (WEB / "audit_log_core.c").read_text(encoding="utf-8")
GLUE_H = (WEB / "include/audit_log.h").read_text(encoding="utf-8")
GLUE_C = (WEB / "audit_log.c").read_text(encoding="utf-8")
API_C = (WEB / "audit_log_api.c").read_text(encoding="utf-8")
AUTH_C = (WEB / "engineering_auth.c").read_text(encoding="utf-8")
RESOURCE_C = (WEB / "system_resource_api.c").read_text(encoding="utf-8")
CMAKE = (WEB / "CMakeLists.txt").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/esp-idf-build.yml").read_text(encoding="utf-8")


def strip_comments(source: str) -> str:
    """Code only. Prose that explains why a credential is never logged must not
    itself trip a check for credential handling."""
    source = re.sub(r"/\*.*?\*/", " ", source, flags=re.S)
    return re.sub(r"//[^\n]*", " ", source)


def strip_literals(source: str) -> str:
    """Executable code only. String literals are compile-time text that ships in
    the exported payload; they are checked by the identity and payload sections
    below. What this view is for is proving that no runtime credential value is
    handled, so the explanatory notes must not mask that check."""
    return re.sub(r'"(?:[^"\\]|\\.)*"', '""', strip_comments(source))


CORE_CODE = strip_literals(CORE_C)
GLUE_CODE = strip_literals(GLUE_C)
API_CODE = strip_literals(API_C)
ALL_C = CORE_C + GLUE_C + API_C
ALL_CODE = CORE_CODE + GLUE_CODE + API_CODE

# --------------------------------------------------------------------------
# 1. All three audited categories are recorded, from real paths.
# --------------------------------------------------------------------------

for category in ("AUDIT_CATEGORY_CONTROL", "AUDIT_CATEGORY_CONFIGURATION",
                 "AUDIT_CATEGORY_AUTHENTICATION"):
    assert category in CORE_H, f"audit category missing: {category}"

for name in ('"control"', '"configuration"', '"authentication"'):
    assert name in CORE_C, f"audit category name missing from the fixed vocabulary: {name}"

# Control: enable/disable, setpoint, mode. Derived from a real before/after
# comparison of the persisted configuration, not asserted by a caller.
for token in (
    "AUDIT_ACTION_CONTROL_ENABLED",
    "AUDIT_ACTION_CONTROL_DISABLED",
    "AUDIT_ACTION_CONTROL_SETPOINT_CHANGED",
    "AUDIT_ACTION_CONTROL_MODE_CHANGED",
    "config_manager_get_snapshot",
    "previous->control",
    "grid_import_target_kw",
):
    assert token in GLUE_C, f"control auditing incomplete: {token}"
assert "audit_log_record(AUDIT_CATEGORY_CONTROL" in GLUE_C
assert "audit_log_record_value(AUDIT_CATEGORY_CONTROL" in GLUE_C

# Configuration: every persisted write, success or failure.
assert "audit_log_record(AUDIT_CATEGORY_CONFIGURATION" in GLUE_C
assert "AUDIT_ACTION_CONFIGURATION_PERSISTED" in GLUE_C
assert "AUDIT_OUTCOME_FAILURE" in GLUE_C, "a failed persist must be distinguishable from a success"
assert "esp_err_t audit_config_manager_save(const app_config_t *config)" in GLUE_C
assert "esp_err_t audit_solar_grid_config_save(const solar_grid_config_t *config)" in GLUE_C

# The persisted-write interception must actually be wired into the build, or the
# configuration and control categories would be dead code.
assert "config_manager_save=audit_config_manager_save" in CMAKE, \
    "persisted configuration writes are not routed through the audit wrapper"
assert "solar_grid_config_save=audit_solar_grid_config_save" in CMAKE, \
    "persisted source-model writes are not routed through the audit wrapper"
assert "-include;audit_log.h" in CMAKE
for source in ('"web_api.c"', '"inverter_config_api.c"', '"meter_config_api.c"',
               '"solar_grid_api.c"'):
    assert source in CMAKE.split("AUDIT_PERSISTENCE_SOURCES", 1)[1].split(")", 1)[0], \
        f"configuration-writing source not audited: {source}"
for source in ('"audit_log_core.c"', '"audit_log.c"', '"audit_log_api.c"'):
    assert source in CMAKE, f"audit source not compiled: {source}"

# The wrapper must reach the real persistence function, not recurse into itself.
assert "#undef config_manager_save" in GLUE_C
assert "#undef solar_grid_config_save" in GLUE_C

# Authentication: success, failure, lockout, logout and password change.
assert "audit_log_record(AUDIT_CATEGORY_AUTHENTICATION, AUDIT_ACTION_LOGIN, AUDIT_OUTCOME_SUCCESS)" \
    in AUTH_C, "successful sign-in is not audited"
assert "AUDIT_ACTION_LOGIN, AUDIT_OUTCOME_FAILURE" in AUTH_C, \
    "a FAILED authentication attempt must be recorded"
assert "AUDIT_OUTCOME_LOCKED_OUT" in AUTH_C, "lockout is not audited"
assert "AUDIT_ACTION_LOGOUT" in AUTH_C, "logout is not audited"
assert "AUDIT_ACTION_PASSWORD_CHANGE" in AUTH_C, "password change is not audited"
assert AUTH_C.count("AUDIT_ACTION_PASSWORD_CHANGE") >= 4, \
    "password change must be audited on denial and failure, not only on success"

# The failure record must be produced by the shared failure path, so no future
# login rejection can bypass it.
failure_fn = AUTH_C[AUTH_C.index("static void record_login_failure(void)"):]
failure_fn = failure_fn[:failure_fn.index("\n}\n")]
assert "AUDIT_ACTION_LOGIN, AUDIT_OUTCOME_FAILURE" in failure_fn

# --------------------------------------------------------------------------
# 2. No credential, credential length or token can reach a log line.
# --------------------------------------------------------------------------

# Structural: an audit entry has no character storage at all, so there is no
# field into which a password, a fragment, a length or a token could be copied.
entry_struct = CORE_H[CORE_H.index("} audit_entry_t;") - 600:CORE_H.index("} audit_entry_t;")]
entry_struct = entry_struct[entry_struct.rindex("typedef struct {"):]
for forbidden in ("char", "char*", "const char", "[]"):
    assert forbidden not in entry_struct, \
        f"an audit entry must not be able to hold text: found {forbidden!r}"

# The recording API accepts enumerators and one numeric value; it accepts no
# caller string, so no call site can pass one even by mistake.
record_decls = re.findall(r"void audit_log_record[a-z_]*\([^;]*\);", GLUE_H, re.S)
assert record_decls, "audit recording API not declared"
for declaration in record_decls:
    assert "char" not in declaration, \
        f"the audit recording API must not accept caller text: {declaration}"

# The value-carrying variant must never be used on an authentication path: a
# number recorded beside a credential event is how a password length leaks.
assert "audit_log_record_value" not in AUTH_C, \
    "authentication events must not attach a numeric value"
assert "audit_log_record_value(AUDIT_CATEGORY_AUTHENTICATION" not in ALL_CODE

# No code in the audit implementation may touch a credential or a token. The
# explanatory prose is stripped first: a comment saying a password is never
# logged is not a password being logged.
# "password_change" is a fixed enumerator name in the audit vocabulary - the
# NAME of an event, never the value of a credential - so it is removed before
# the scan rather than special-cased away afterwards.
for forbidden in ("password", "passwd", "session_token", "setup_code",
                  "psk", "secret", "cookie"):
    for name, code in (("audit_log_core.c", CORE_CODE), ("audit_log.c", GLUE_CODE),
                       ("audit_log_api.c", API_CODE)):
        scanned = code.lower().replace("password_change", "")
        assert forbidden not in scanned, f"{name} handles {forbidden!r}"

# Length is a credential attribute, not a fact worth logging.
for token in ("strlen", "sizeof(password", "password_length"):
    assert token not in ALL_CODE, f"audit implementation must not measure a credential: {token}"

# The audited authentication call sites must not pass anything derived from the
# submitted password.
for match in re.finditer(r"audit_log_record\([^;]*\);", AUTH_C, re.S):
    call = match.group(0)
    assert "password" not in call and "cookie" not in call and "token" not in call, \
        f"authentication audit call carries credential material: {call}"

# --------------------------------------------------------------------------
# 3. Reading the trail requires an authenticated engineering session.
# --------------------------------------------------------------------------

assert '"/api/system/audit-log"' in API_C
assert "engineering_auth_is_authorized(request)" in API_C, \
    "audit log read must verify the engineering session"
assert "return engineering_auth_require(request);" in API_C, \
    "an unauthenticated audit log read must be refused"
assert "AUDIT_ACTION_AUDIT_LOG_READ" in API_C and "AUDIT_OUTCOME_DENIED" in API_C, \
    "a refused audit log read must itself be recorded"
denied_index = API_C.index("AUDIT_OUTCOME_DENIED")
assert denied_index < API_C.index("cJSON_CreateObject"), \
    "the authorization check must precede any response construction"
# Registration goes through the component gateway, which wraps every non-public
# URI, so the endpoint cannot be reachable without the session check above.
assert "httpd_register_uri_handler(server, &handler)" in API_C
assert "audit_log_api_register" in RESOURCE_C, "the audit log endpoint is never registered"

# --------------------------------------------------------------------------
# 4. No invented identity, serial number or wall-clock date.
# --------------------------------------------------------------------------

assert "uptime_relative" in API_C
assert '"controller_uptime_ms"' in API_C
assert "uptime_ms" in CORE_H
for forbidden in ("strftime", "localtime", "gmtime", "time(NULL)", "settimeofday",
                  "gettimeofday", "esp_sntp", "sntp_", "iso8601", "timestamp_utc"):
    assert forbidden not in ALL_C, f"the audit trail must not invent a wall-clock date: {forbidden}"

assert '"actor", "authenticated_engineering_session"' in API_C, \
    "an entry must state the session, not a name"
assert "actor_identified" in API_C and '"actor_identified", false' in API_C
for forbidden in ("username", "user_name", '"user"', "operator_name", "logged_in_as"):
    assert forbidden not in API_C, f"the audit payload must not invent an operator identity: {forbidden}"

# RAM-only retention is stated plainly in the payload, not left to be discovered.
assert '"storage", "ram_only"' in API_C
assert '"persisted_across_reboot", false' in API_C
assert "lost on reboot" in API_C
# Another agent owns flash persistence for alarms; the audit trail must not
# write to the storage partition or duplicate that work.
for forbidden in ("nvs_open", "nvs_set_blob", "esp_partition_write", "fopen"):
    assert forbidden not in ALL_C, f"the audit trail must not persist to flash: {forbidden}"

# A wrapped ring must report the evidence it dropped.
assert "dropped_oldest" in API_C and '"truncated"' in API_C
assert "overwritten" in CORE_C

# --------------------------------------------------------------------------
# 5. Field-service identity: real facts only.
# --------------------------------------------------------------------------

for token in (
    '"/api/system/identity"',
    '"product_model"',
    '"mac_address"',
    "esp_read_mac",
    "ESP_MAC_WIFI_STA",
    '"chip_model_name"',
    '"chip_revision_major"',
    "esp_app_get_description",
    '"idf_version"',
    '"build_date"',
    "flash_size_bytes",
    "esp_partition_find",
    "esp_ota_get_running_partition",
    '"running_label"',
    '"schema_version_supported"',
    "APP_CONFIG_VERSION",
    '"uptime_ms"',
    "reset_reason_name",
    '"last_reboot_unexpected"',
    "psram",
    '"thresholds"',
):
    assert token in RESOURCE_C, f"field-service identity or health field missing: {token}"

# Serial number and hardware revision are not knowable and must be reported as
# unavailable rather than fabricated.
assert '"serial_number_available", false' in RESOURCE_C
assert 'cJSON_AddNullToObject(root, "serial_number")' in RESOURCE_C
assert '"hardware_revision_available", false' in RESOURCE_C
for forbidden in ("SN-", "serial_number\", \"", "snprintf(serial", "generate_serial",
                  "fake_serial", "esp_random"):
    assert forbidden not in RESOURCE_C, f"a serial number must never be manufactured: {forbidden}"
assert '"wall_clock_available", false' in RESOURCE_C
for forbidden in ("strftime", "localtime", "gmtime", "esp_sntp"):
    assert forbidden not in RESOURCE_C, f"the controller has no wall clock: {forbidden}"

# The published thresholds must be the same constants that decide the state, so
# the service page cannot disagree with the controller.
assert "RESOURCE_FREE_INTERNAL_WARNING_BYTES" in RESOURCE_C
assert RESOURCE_C.count("RESOURCE_FREE_INTERNAL_WARNING_BYTES") >= 3

# --------------------------------------------------------------------------
# 6. Real-time safety rules.
# --------------------------------------------------------------------------

# Nothing that allocates, logs or formats may run with interrupts disabled.
for source_name, source in (("audit_log.c", GLUE_C), ("audit_log_core.c", CORE_C)):
    for start in [m.start() for m in re.finditer(r"portENTER_CRITICAL", source)]:
        end = source.index("portEXIT_CRITICAL", start)
        section = source[start:end]
        for forbidden in ("ESP_LOG", "malloc(", "calloc(", "free(", "cJSON_", "snprintf",
                          "printf"):
            assert forbidden not in section, \
                f"{source_name}: {forbidden} inside a critical section"

# The exporter builds JSON outside the lock, by copying a snapshot first.
assert "audit_log_snapshot(" in API_C
assert "portENTER_CRITICAL" not in API_C, \
    "the HTTP exporter must not take the audit spinlock directly"

# HTTP handlers must never perform a blocking Modbus transaction.
for name, source in (("audit_log_api.c", API_C), ("system_resource_api.c", RESOURCE_C)):
    for forbidden in ("meter_manager_read_registers", "modbus_", "inverter_manager_write",
                      "inverter_manager_set_total_power_kw"):
        assert forbidden not in source, f"{name} must not perform Modbus I/O: {forbidden}"

# --------------------------------------------------------------------------
# 7. The safety-critical authentication invariants are unchanged.
# --------------------------------------------------------------------------

for token in (
    "AUTH_PBKDF2_ITERATIONS 20000u",
    "AUTH_MAX_FAILURES 5u",
    "AUTH_LOCKOUT_MS 30000ULL",
    "AUTH_SESSION_TIMEOUT_MS (30ULL * 60ULL * 1000ULL)",
    "char cookie_header[AUTH_COOKIE_HEADER_BYTES];",
    "set_session_cookie(request, cookie_header, sizeof(cookie_header), cookie);",
    "HttpOnly; SameSite=Strict",
):
    assert token in AUTH_C, f"audit instrumentation weakened engineering authentication: {token}"
assert AUTH_C.count("char cookie_header[AUTH_COOKIE_HEADER_BYTES];") == 2, \
    "the Set-Cookie header buffer must stay owned by the request handler"

# --------------------------------------------------------------------------
# 8. Registered in CI, and the storage core actually runs.
# --------------------------------------------------------------------------

assert "tests/audit_log_source_contract.py" in WORKFLOW, \
    "the audit contract must run in CI"

with tempfile.TemporaryDirectory() as directory:
    binary = Path(directory) / "audit_log_core_test"
    subprocess.run([
        "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I", str(WEB / "include"),
        str(ROOT / "tests/audit_log_core_test.c"),
        str(WEB / "audit_log_core.c"),
        "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)

print("audit trail and field-service identity source contract: PASS")
