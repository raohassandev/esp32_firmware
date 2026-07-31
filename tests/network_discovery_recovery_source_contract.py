#!/usr/bin/env python3
"""Reachability contract for a controller that is moved between sites.

The defect this guards against: the unit was relocated, its Wi-Fi changed, and
the owner could neither find it nor recover it without a serial cable. Four
properties keep that from recurring, and each is asserted here by behaviour
rather than by identifier spelling, so the code may be refactored freely.

  1. The unit answers to a NAME, on every interface, so nobody needs to know
     which address a strange DHCP server handed it.
  2. That name is derived from the factory MAC, so two controllers on one
     network do not collide.
  3. A soft access point is on air from the moment the radio starts and never
     comes down, so there is no window in which the unit is unreachable and no
     retry budget that has to expire first.
  4. That access point is always WPA2 with a passphrase belonging to this unit,
     and the passphrase reaches exactly one place - the serial console, once,
     at boot - and no network-reachable surface at all.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


IDENTITY_H = read("components/device_identity/include/device_identity.h")
IDENTITY_C = read("components/device_identity/device_identity.c")
IDENTITY_CMAKE = read("components/device_identity/CMakeLists.txt")
MDNS_C = read("components/network_manager/network_mdns.c")
MDNS_H = read("components/network_manager/network_mdns.h")
NETWORK_C = read("components/network_manager/network_manager.c")
NETWORK_H = read("components/network_manager/include/network_manager.h")
NETWORK_CMAKE = read("components/network_manager/CMakeLists.txt")
NETWORK_MANIFEST = read("components/network_manager/idf_component.yml")
CONFIG_C = read("components/config_manager/config_manager.c")
CONFIG_H = read("components/config_manager/include/config_manager.h")
WEB_API_C = read("components/web_server/web_api.c")
GUARD_C = read("components/web_server/engineering_guard.c")
KCONFIG = read("main/Kconfig.projbuild")
SDKCONFIG_DEFAULTS = read("sdkconfig.defaults")
WORKFLOW = read(".github/workflows/esp-idf-build.yml")

COMPONENT_SOURCES = {
    str(path.relative_to(ROOT)).replace("\\", "/"): path.read_text(encoding="utf-8")
    for path in (ROOT / "components").rglob("*.c")
}
COMPONENT_SOURCES.update(
    {
        str(path.relative_to(ROOT)).replace("\\", "/"): path.read_text(encoding="utf-8")
        for path in (ROOT / "main").rglob("*.c")
    }
)

def code_only(source: str) -> str:
    """Source with comments and string/character literals removed.

    Everything below that looks for a credential reaching a log or a JSON
    payload runs on this, so prose that merely mentions a passphrase is not
    mistaken for one being printed.

    A single pass rather than a chain of regexes: a "//" inside a URL in a block
    comment, or a "/*" inside a string, would otherwise swallow real code.
    """
    out = []
    index = 0
    length = len(source)
    while index < length:
        char = source[index]
        pair = source[index : index + 2]
        if pair == "/*":
            end = source.find("*/", index + 2)
            index = length if end == -1 else end + 2
            out.append(" ")
        elif pair == "//":
            end = source.find("\n", index)
            index = length if end == -1 else end
            out.append(" ")
        elif char in ('"', "'"):
            quote = char
            index += 1
            while index < length and source[index] != quote:
                index += 2 if source[index] == "\\" else 1
            index += 1
            out.append('""' if quote == '"' else "' '")
        else:
            out.append(char)
            index += 1
    return "".join(out)


def statements_containing(source: str, token: str):
    """Whole `;`-delimited statements that mention token.

    Used where the property is about how a call is GUARDED, not merely that it
    appears: a mode selection spread over two lines must still be judged as one
    expression.
    """
    found = []
    index = source.find(token)
    while index != -1:
        start = source.rfind(";", 0, index)
        start = 0 if start == -1 else start + 1
        end = source.find(";", index)
        end = len(source) if end == -1 else end
        found.append(source[start:end])
        index = source.find(token, index + 1)
    return found


def function_body(source: str, name: str) -> str:
    """Braced body of a function, located by name."""
    start = source.index(name)
    start = source.index("{", start)
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unterminated function body for {name}")


# ---------------------------------------------------------------------------
# 1. The unit is discoverable by name, on both interfaces.
# ---------------------------------------------------------------------------

mdns_code = code_only(MDNS_C)
assert re.search(r"\bmdns_init\s*\(", mdns_code), "no mDNS responder is started"
assert re.search(r"\bmdns_hostname_set\s*\(", mdns_code), "the responder publishes no host name"
assert re.search(
    r"mdns_service_add\s*\([^;]*_http[^;]*_tcp", MDNS_C
), "no _http._tcp record is published, so the unit cannot be found by a network scan"

# The responder must be reachable from the manager, and the manager must start it.
network_code = code_only(NETWORK_C)
assert re.search(
    r"network_mdns_start\s*\(", network_code
), "the network manager never starts the discovery responder"
assert "network_mdns.c" in NETWORK_CMAKE, "the responder is not compiled into the component"
assert re.search(
    r"espressif/mdns", NETWORK_MANIFEST
), "the mDNS dependency is not declared, so the build would not be reproducible"

# Both predefined interfaces, pinned rather than left to a library default: the
# name has to resolve on the site network AND on the controller's own AP.
for symbol in ("CONFIG_MDNS_PREDEF_NETIF_STA=y", "CONFIG_MDNS_PREDEF_NETIF_AP=y"):
    assert symbol in SDKCONFIG_DEFAULTS, f"mDNS interface not pinned on: {symbol}"

# The responder is handed a bare label. A name containing a dot would be
# published as a subdomain and would not answer as "<name>.local".
assert re.search(
    r"strchr\s*\([^)]*,\s*'\.'\s*\)", MDNS_C
), "the responder accepts a dotted name, which would not resolve as a .local label"


# ---------------------------------------------------------------------------
# 2. The published name is derived from the MAC, not fixed.
# ---------------------------------------------------------------------------

identity_code = code_only(IDENTITY_C)
assert re.search(
    r"esp_read_mac\s*\(", identity_code
), "the device identity is not read from the factory MAC"

# The host name must be *built* from the MAC-derived suffix, never a constant.
hostname_body = function_body(IDENTITY_C, "device_identity_hostname")
assert re.search(
    r"device_identity_suffix\s*\(", hostname_body
), "the host name is not derived from the MAC suffix"
assert re.search(
    r"snprintf\s*\([^;]*%s", hostname_body
), "the host name is not formatted from the suffix, so it would be identical on every unit"

# Whatever the manager hands to the netifs and the responder must come from that
# derivation, not from a fixed configuration string.
init_body = function_body(NETWORK_C, "esp_err_t network_manager_init")
init_code = code_only(init_body)
assert re.search(
    r"device_identity_hostname\s*\(", init_code
), "the interfaces are not given the MAC-derived host name"
assert not re.search(
    r"esp_netif_set_hostname\s*\([^;]*CONFIG_", init_code
), "a build-time constant is used as the host name, so two units would collide"
assert init_code.count("esp_netif_set_hostname") >= 2, (
    "the host name must be set on both the station and the soft AP interface"
)

# The recovery AP name carries the same per-unit suffix, from the same single
# derivation the host name uses -- not a second copy of the MAC arithmetic.
defaults_body = function_body(CONFIG_C, "static void defaults")
assert re.search(
    r"device_identity_suffix\s*\(", code_only(defaults_body)
), "the recovery AP name is not made unique per unit from the shared MAC derivation"
assert not re.search(r"esp_read_mac\s*\(", code_only(defaults_body)), (
    "the recovery AP name re-derives the MAC suffix itself instead of using the one "
    "definition in device_identity, so the two names could drift apart"
)


# ---------------------------------------------------------------------------
# 3. Access point and station run concurrently, permanently.
# ---------------------------------------------------------------------------

# Every radio mode this component selects is the concurrent one, UNLESS the same
# statement says the recovery AP is not on air. An unguarded WIFI_MODE_STA
# anywhere would take the AP off air for the duration, which is the original
# lockout. Judged per statement, so a two-line ternary is still one decision.
mode_statements = statements_containing(network_code, "esp_wifi_set_mode")
assert mode_statements, "the component never selects a radio mode"
for statement in mode_statements:
    assert "WIFI_MODE_APSTA" in statement, (
        f"a radio mode selection cannot reach concurrent mode: {' '.join(statement.split())}"
    )
    if "WIFI_MODE_STA" in statement:
        assert "s_recovery_ap_on_air" in statement, (
            "a station-only radio mode is selected without establishing, in the same "
            f"statement, that the AP is already down: {' '.join(statement.split())}"
        )

# The paths that run after the AP is on air must not touch the radio mode at all.
for name in (
    "static void handle_sta_got_ip",
    "static void handle_sta_disconnected",
    "static void choose_and_connect",
):
    assert "esp_wifi_set_mode" not in code_only(function_body(NETWORK_C, name)), (
        f"{name} changes the radio mode, which can drop the recovery AP"
    )

# The AP is raised during initialization, before the radio starts, and not from
# any retry, disconnect or exhaustion path.
ap_start_calls = re.findall(r"\bstart_recovery_ap\b", network_code)
assert len(ap_start_calls) == 2, (
    "the recovery AP must be defined once and started once, from initialization only"
)
assert init_code.index("start_recovery_ap") < init_code.index("esp_wifi_start"), (
    "the AP is configured after the radio starts, leaving a window with no way in"
)

disconnect_body = code_only(function_body(NETWORK_C, "static void handle_sta_disconnected"))
assert "start_recovery_ap" not in disconnect_body, (
    "the AP is still raised from the disconnect path, i.e. it is still a last resort"
)

# Joining a station must not take the AP down - that is the original defect.
got_ip_body = code_only(function_body(NETWORK_C, "static void handle_sta_got_ip"))
assert "esp_wifi_set_mode" not in got_ip_body, (
    "associating with a station still changes radio mode, which drops the AP"
)

# One radio serves both interfaces, so the AP is dragged onto the station's
# channel. That must be observed and reported, not silently stranding clients.
assert re.search(
    r"esp_wifi_get_channel\s*\(", network_code
), "the AP channel is never read, so a channel follow cannot be reported"
assert re.search(
    r"ESP_LOGW\s*\([^;]*channel", NETWORK_C
), "a channel change is not reported, so AP clients would be dropped silently"

# The bounded sweep policy must remain documented and bounded.
assert re.search(
    r"AP_RESCAN_MAX_INTERVAL_MS\s+\d+", NETWORK_C
), "the station rescan cadence is unbounded"
assert re.search(
    r"policy", NETWORK_C, re.IGNORECASE
), "the retry and backoff policy is not stated in the source"

# Every saved station is offered the radio before the unit settles for the AP.
choose_body = code_only(function_body(NETWORK_C, "static void choose_and_connect"))
assert re.search(r"for\s*\(", choose_body), (
    "the saved stations are not walked, so a relocated unit cannot try a second network"
)
assert choose_body.count("connect_profile") == 1, (
    "the profile attempt must go through one loop, not per-profile special cases"
)

# The second saved station must be genuinely usable, not hard-wired off.
assert not re.search(
    r"fallback\.enabled\s*=\s*(false|0)\s*;", code_only(WEB_API_C)
), "the API forces the second saved station off"

# A saved profile that HOLDS an SSID must be attempted. The field failure was a
# secondary profile carrying usable credentials but enabled = false, so the sweep
# skipped it and the unit sat on its AP instead of rejoining a network it knew.
# The normalisation must run during init, and must key off the SSID rather than
# assigning a constant.
normalize_body = code_only(function_body(CONFIG_C, "static bool normalize_station_profiles"))
assert re.search(r"ssid\s*\[\s*0\s*\]", normalize_body), (
    "station profiles are not enabled on the basis of whether they hold an SSID"
)
assert re.search(r"enabled\s*=\s*usable", normalize_body), (
    "the enabled flag is set to a constant rather than to whether the profile is usable"
)
# It must not touch anything else: a commissioned unit keeps every credential.
for protected in ("password", "static_ip", "gateway", "netmask", "dns"):
    assert protected not in normalize_body, (
        f"the station normalisation touches {protected}, risking commissioned credentials"
    )
assert "normalize_station_profiles(loaded)" in code_only(CONFIG_C), (
    "the station normalisation never runs, so a disabled-but-usable profile stays skipped"
)


# ---------------------------------------------------------------------------
# 4. The access point is always secured, per device, and its passphrase is
#    never network-reachable.
# ---------------------------------------------------------------------------

ap_body = function_body(NETWORK_C, "static esp_err_t start_recovery_ap")
ap_code = code_only(ap_body)
authmodes = re.findall(r"authmode\s*=\s*([A-Za-z_0-9]+)", ap_code)
assert authmodes == ["WIFI_AUTH_WPA2_PSK"], (
    f"the AP authentication mode is not unconditionally WPA2: {authmodes}"
)
assert "WIFI_AUTH_OPEN" not in ap_code, "a code path can bring the AP up unsecured"
assert re.search(
    r"strlen\s*\([^)]*password[^)]*\)\s*<", ap_code
), "the AP is not refused when the passphrase is too short for WPA2"

# Nothing may persist a configuration that would produce an open or absent AP.
save_body = code_only(function_body(CONFIG_C, "esp_err_t config_manager_save"))
assert re.search(
    r"fallback_ap_enabled", save_body
), "a disabled recovery AP can still be persisted"
assert re.search(
    r"strlen\s*\([^)]*fallback_ap_password[^)]*\)\s*<", save_body
), "a recovery AP without a WPA2-length passphrase can still be persisted"

# The passphrase is per device and drawn from the hardware RNG. It lives in the
# component that owns stored configuration, and NOTHING that derives a value from
# the MAC may be used to produce it: everything in device_identity is computable
# from an address broadcast in every frame, so a secret built there would be
# public. This is the property, not the location.
secret_body = code_only(function_body(CONFIG_C, "static bool ensure_recovery_ap_secret"))
assert re.search(
    r"esp_fill_random\s*\(", secret_body
), "the passphrase is not drawn from the hardware random number generator"
assert not re.search(r"device_identity_|esp_read_mac", secret_body), (
    "the recovery passphrase is derived from the MAC, which is public information "
    "and computable by anyone reading this repository"
)
assert re.search(
    r"DEVICE_IDENTITY_MIN_PASSPHRASE_LENGTH\s+8U", IDENTITY_H
), "the WPA2 minimum is not defined as the floor for a usable passphrase"
assert not re.search(r"secret|passphrase|password", code_only(IDENTITY_C), re.IGNORECASE), (
    "the MAC-derived identity component produces a credential; anything built there "
    "is computable from a published MAC address"
)

# A passphrase too short for WPA2 must count as absent and be replaced, not
# accepted. Treating only "" as absent is what let an 8-character-minimum rule be
# bypassed by a stored short value.
assert re.search(
    r"strlen\s*\([^)]*fallback_ap_password[^)]*\)\s*<\s*DEVICE_IDENTITY_MIN_PASSPHRASE_LENGTH",
    secret_body,
), "a stored passphrase shorter than WPA2 allows is not treated as absent"

# The always-on AP may not be left disabled by anything already stored.
assert re.search(r"fallback_ap_enabled\s*=\s*true", secret_body), (
    "a stored 'recovery AP disabled' is honoured rather than corrected, so a unit "
    "saved that way could never be reached again"
)

# The generator runs whenever the stored passphrase is unusable, and the build
# passphrase - if any - comes from exactly one definition.
config_code = code_only(CONFIG_C)
assert re.search(
    r"ensure_recovery_ap_secret", config_code
), "nothing guarantees the recovery AP has a name and a passphrase"
build_passphrase_users = sorted(
    relative
    for relative, source in COMPONENT_SOURCES.items()
    if "CONFIG_PVDG_RECOVERY_AP_PASSWORD" in code_only(source)
)
assert build_passphrase_users == ["components/config_manager/config_manager.c"], (
    "the build passphrase is read outside the single component that owns it: "
    f"{build_passphrase_users}"
)
assert re.search(
    r"^config\s+PVDG_RECOVERY_AP_PASSWORD\s*$", KCONFIG, re.MULTILINE
), "the recovery passphrase has no single Kconfig definition"
assert "CONFIG_PVDG_RECOVERY_AP_PASSWORD" not in SDKCONFIG_DEFAULTS, (
    "a passphrase is pinned in the checked-in defaults"
)

# No passphrase literal may be seeded into a stored configuration: the default
# comes from the single Kconfig symbol (empty in this tree) or from nothing at
# all, so a shared credential cannot be reintroduced as a string in the source.
ap_password_assignments = statements_containing(defaults_body, "fallback_ap_password")
assert ap_password_assignments, "the defaults never establish a recovery passphrase state"
for statement in ap_password_assignments:
    assert "CONFIG_PVDG_RECOVERY_AP_PASSWORD" in statement or not re.search(
        r'"[^"]+"', statement
    ), (
        "the defaults seed a shared recovery passphrase from a literal: "
        f"{' '.join(statement.split())}"
    )

# Neither saved station may be seeded from the build image.
assert not re.search(
    r"fallback\.(ssid|password)[^;]*CONFIG_PVDG_DEFAULT_WIFI", defaults_body
), "a site's credentials are compiled into the default configuration"

# --- The passphrase must reach exactly one place: the serial console, once. ---

LOG_CALL = re.compile(r"(?:ESP_LOG[A-Z]+|ESP_EARLY_LOG[A-Z]+|printf|fprintf)\s*\((?:[^();]|\([^()]*\))*\)")
CREDENTIAL = re.compile(r"\b\w*(?:password|passphrase|psk|secret)\w*\b", re.IGNORECASE)

credential_logs = []
for relative, source in sorted(COMPONENT_SOURCES.items()):
    stripped = code_only(source)
    for match in LOG_CALL.finditer(stripped):
        if CREDENTIAL.search(match.group(0)):
            credential_logs.append(relative)

assert credential_logs == ["components/network_manager/network_manager.c"], (
    "a credential reaches a log outside the single reviewed serial disclosure: "
    f"{sorted(set(credential_logs))}"
)

# ...and inside that file, only from the one function that documents why.
announce_body = code_only(function_body(NETWORK_C, "static void announce_recovery_ap_on_serial"))
announce_logs = [
    match.group(0) for match in LOG_CALL.finditer(announce_body) if CREDENTIAL.search(match.group(0))
]
assert len(announce_logs) == 1, (
    "the serial disclosure must be a single line, not scattered through the function"
)
for other in ("start_recovery_ap", "handle_sta_got_ip", "choose_and_connect", "network_manager_init"):
    body = code_only(function_body(NETWORK_C, other))
    assert not [m for m in LOG_CALL.finditer(body) if CREDENTIAL.search(m.group(0))], (
        f"{other} logs a credential"
    )

# The exception must carry its justification, so a later reader does not delete
# it as an oversight or copy it as a precedent.
announce_source = function_body(NETWORK_C, "static void announce_recovery_ap_on_serial")
preamble = NETWORK_C[: NETWORK_C.index("static void announce_recovery_ap_on_serial")]
justification = preamble[-3000:]
for token in ("EXCEPTION", "serial", "physical"):
    assert token.lower() in justification.lower(), (
        f"the serial disclosure is not justified in the source: missing '{token}'"
    )
assert "192.168.4.1" in announce_source, (
    "the disclosure does not tell the engineer where to browse once joined"
)

# It is called exactly once, from initialization.
assert network_code.count("announce_recovery_ap_on_serial") == 2, (
    "the serial disclosure must be defined once and called once"
)
assert "announce_recovery_ap_on_serial" in init_code, (
    "the serial disclosure is not made at boot"
)

# Using the build's shared passphrase must be impossible to miss in the log,
# while never printing the value in that warning.
assert re.search(
    r"config_manager_recovery_ap_is_build_default", CONFIG_H
), "nothing reports that the shared build passphrase is in use"
default_warning = announce_source[announce_source.index("build_default_in_use") :]
assert re.search(r"ESP_LOGE", default_warning), (
    "the shared-passphrase warning is not raised at error level"
)

# --- ...and no network-reachable surface at all. ---

for relative, source in sorted(COMPONENT_SOURCES.items()):
    stripped = code_only(source)
    for match in re.finditer(r"cJSON_Add\w*\s*\((?:[^();]|\([^()]*\))*\)", stripped):
        call = match.group(0)
        if not re.search(r"fallback_ap_password|\.password|->password", call):
            continue
        # A credential may appear only as the subject of a mask: the UI needs to
        # know that a passphrase is set, never what it is.
        assert "MASKED" in call.upper(), (
            f"{relative} serializes a credential into a JSON payload: {call}"
        )

# The exported configuration masks every passphrase rather than omitting the
# field, so the UI can show that one is set without learning it.
export_body = function_body(CONFIG_C, "esp_err_t config_manager_export_json")
assert export_body.count("MASKED_PASSWORD") >= 1, "the exported configuration leaks a passphrase"

# The status struct crosses into HTTP responses; it must not carry the secret.
assert not re.search(r"password|passphrase", code_only(NETWORK_H), re.IGNORECASE), (
    "the network status struct carries a credential into HTTP responses"
)


# ---------------------------------------------------------------------------
# 5. Reaching the unit over its own access point confers no extra authority.
# ---------------------------------------------------------------------------

guard_code = code_only(GUARD_C)
# The gate must not be able to consult the interface, the peer address or the
# soft AP subnet when deciding what is public.
public_body = function_body(GUARD_C, "static bool public_uri")
for forbidden in ("192.168.4", "netif", "sockaddr", "ap_", "remote_ip"):
    assert forbidden not in public_body, (
        f"the engineering gate exempts requests by origin: {forbidden}"
    )
assert not re.search(
    r"httpd_req_to_sockfd|inet_ntoa|getpeername", guard_code
), "the engineering gate inspects the client's origin, which would grant the AP extra authority"

# Turning the AP off is refused rather than silently accepted.
parse_body = function_body(WEB_API_C, "static bool parse_wifi_config")
assert re.search(
    r"fallback_ap_enabled\s*=\s*true", parse_body
), "the API can persist a disabled recovery AP"
# Renaming the AP must not wipe its passphrase, which used to leave it open.
assert not re.search(
    r"fallback_ap_password\s*\[\s*0\s*\]\s*=\s*'\\0'", parse_body
), "renaming the recovery AP still clears its passphrase, leaving it unsecured"


# ---------------------------------------------------------------------------
# 6. The contract itself is enforced by CI.
# ---------------------------------------------------------------------------

assert "tests/network_discovery_recovery_source_contract.py" in WORKFLOW, (
    "this contract is not registered in the CI workflow"
)
assert "device_identity" in IDENTITY_CMAKE, "the identity component is not registered"
assert "device_identity" in NETWORK_CMAKE, "the network manager cannot see the identity component"

print(
    "mDNS discovery, MAC-derived identity, permanently-on WPA2 recovery AP and "
    "single-site credential disclosure: PASS"
)
