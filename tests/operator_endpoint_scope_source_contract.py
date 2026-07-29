"""Operator screens must not request Engineering-only endpoints.

Finding S4 of docs/UI_VISUAL_AUDIT_2026-07-29.md measured, over 60 browser runs
against the live controller, 80 x 401 on /api/inverter-profiles, 44 on
/api/wifi/scan, 40 on /api/solar-grid/config and 7 on /api/meters/em500/cache.
Every module initialised on every route, so the operator dashboard issued
requests it could never be authorised for.

That is expensive on this device, not merely noisy: the HTTP server offers a
very small client socket pool (S1), each response allocates on a controller
whose minimum free heap has been measured close to its own critical threshold
(S2), and the failures print on the customer-facing operator screen.

This contract asserts the fix is structural: one shared scope predicate in
web/product-mode.js (route AND authorisation), consulted at every Engineering
call site before the request is issued -- not a swallowed error afterwards.
It also asserts the schema-5 ramp editor exists and is Engineering-gated.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODE = (ROOT / "web/product-mode.js").read_text(encoding="utf-8")
WIFI = (ROOT / "web/wifi.js").read_text(encoding="utf-8")
WIFI_GUARD = (ROOT / "web/wifi-guard.js").read_text(encoding="utf-8")
NETWORK_FIX = (ROOT / "web/network-commissioning-fix.js").read_text(encoding="utf-8")
SOLAR_GRID = (ROOT / "web/solar-grid.js").read_text(encoding="utf-8")
PROFILES = (ROOT / "web/inverter-profiles.js").read_text(encoding="utf-8")
INVERTER_CONFIG = (ROOT / "web/inverter-config.js").read_text(encoding="utf-8")
EM500_CORE = (ROOT / "web/em500-core.js").read_text(encoding="utf-8")
EM500_QUALITY = (ROOT / "web/em500-quality.js").read_text(encoding="utf-8")
WIZARD_V2 = (ROOT / "web/commissioning-wizard-v2.js").read_text(encoding="utf-8")
RELEASE_V3 = (ROOT / "web/commissioning-release-v3.js").read_text(encoding="utf-8")
CONFIG_MANAGER = (ROOT / "components/config_manager/config_manager.c").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/esp-idf-build.yml").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


# ---------------------------------------------------------------- shared gate

for token in [
    "ENGINEERING_ONLY_ENDPOINTS",
    "function engineeringScopeAllows",
    "function engineeringEndpointScope",
    "function mayRequest",
    "function onScopeChange",
    "mayUseEngineering",
]:
    require(token in MODE, f"shared request-scope mechanism missing: {token}")

# The predicate is route AND authorisation, reusing the module's own route
# tracking rather than a second copy of it.
scope_start = MODE.index("function engineeringScopeAllows")
scope_body = MODE[scope_start:MODE.index("function engineeringEndpointScope")]
require("state.authenticated" in scope_body,
        "request scope must consult the live Engineering authorisation state")
require("currentRoute()" in scope_body,
        "request scope must consult the active route, not authorisation alone")

# Every endpoint the audit measured as a guaranteed operator 401 is declared,
# together with the routes that legitimately need it.
for endpoint in [
    "'/api/inverter-profiles'",
    "'/api/wifi/scan'",
    "'/api/solar-grid/config'",
    "'/api/meters/em500/'",
]:
    require(endpoint in MODE.split("const state =", 1)[0],
            f"Engineering-only endpoint not declared in the scope table: {endpoint}")

# Operator endpoints must stay reachable without authentication, so they must
# never appear in the Engineering-only table.
scope_table = MODE[MODE.index("const ENGINEERING_ONLY_ENDPOINTS"):MODE.index("const state =")]
for operator_endpoint in [
    "/api/status",
    "/api/telemetry",
    "/api/operator/",
    "/api/inverter-telemetry",
]:
    require(operator_endpoint not in scope_table,
            f"operator endpoint must not require Engineering scope: {operator_endpoint}")
require("'/api/meters'" not in scope_table,
        "the operator meter summary must not require Engineering scope")

# The scope is re-evaluated on sign-in as well as on navigation, so Engineering
# data loads immediately after login without a manual refresh.
hook = MODE[MODE.index("function onScopeChange"):MODE.index("async function renewEngineeringSession")]
require("'amx-access-change'" in hook and "'hashchange'" in hook,
        "scope changes must follow both authorisation changes and route changes")


# ------------------------------------------------- gated at every call site

GATED_MODULES = [
    (WIFI, "wifi.js", "'/api/wifi/scan'"),
    (WIFI_GUARD, "wifi-guard.js", "'/api/wifi/scan'"),
    (SOLAR_GRID, "solar-grid.js", "'/api/solar-grid/config'"),
    (PROFILES, "inverter-profiles.js", "'/api/inverter-profiles'"),
    (WIZARD_V2, "commissioning-wizard-v2.js", "'/api/inverter-profiles'"),
    (RELEASE_V3, "commissioning-release-v3.js", "'/api/inverter-profiles'"),
    (EM500_CORE, "em500-core.js", "'/api/meters/em500/"),
    (EM500_QUALITY, "em500-quality.js", "'/api/meters/em500/"),
]
for source, name, endpoint in GATED_MODULES:
    require("AutomatrixEngineeringAccess" in source,
            f"{name} must reuse the shared access scope, not its own auth check")
    require(f"mayRequest({endpoint}" in source,
            f"{name} must ask the shared scope before requesting {endpoint}")

for source, name in [(NETWORK_FIX, "network-commissioning-fix.js"),
                     (INVERTER_CONFIG, "inverter-config.js")]:
    require("mayUseEngineering(" in source,
            f"{name} must scope its Engineering baseline read to its own route")

# A module may not simply reintroduce an unconditional load on DOMContentLoaded.
require("if (!catalogueScopeAllowed()) return;" in PROFILES,
        "the inverter profile catalogue must be gated inside its loader")
require("if (!meterScopeAllowed()) return;" in EM500_CORE,
        "the EM500 workspace must be gated inside its refresh path")
require("if (!meterScopeAllowed()) return;" in EM500_QUALITY,
        "the EM500 acquisition-quality poll must be gated inside its refresh path")
require("if (!scanScopeAllowed()) return;" in WIFI,
        "the Wi-Fi scan snapshot must be gated inside its loader")
require("if (!controlScopeAllowed()) return;" in SOLAR_GRID,
        "the Solar+Grid status poll must be gated inside its refresh path")
require("if (!access()?.mayRequest('/api/wifi/scan')) return;" in WIFI_GUARD,
        "the Wi-Fi guard baseline must be gated inside its loader")

# The requests must be prevented, not silenced after the fact.
for source, name, _endpoint in GATED_MODULES:
    for forbidden in ["console.error = ", "console.warn = ", "window.onerror ="]:
        require(forbidden not in source,
                f"{name} must not suppress console output instead of scoping requests")

# Signing in must re-run the gated loaders without a page refresh.
for source, name in [
    (WIFI, "wifi.js"),
    (WIFI_GUARD, "wifi-guard.js"),
    (SOLAR_GRID, "solar-grid.js"),
    (PROFILES, "inverter-profiles.js"),
    (EM500_CORE, "em500-core.js"),
    (EM500_QUALITY, "em500-quality.js"),
    (INVERTER_CONFIG, "inverter-config.js"),
    (NETWORK_FIX, "network-commissioning-fix.js"),
    (WIZARD_V2, "commissioning-wizard-v2.js"),
    (RELEASE_V3, "commissioning-release-v3.js"),
]:
    require("onScopeChange(" in source,
            f"{name} must reload through the shared scope hook so data appears after login")


# -------------------------------------------- schema 5 ramp commissioning UI

require("#controlRampEditor" in MODE,
        "the ramp editor must be registered as an Engineering-only DOM region")
require("controlRampEditor" in SOLAR_GRID, "the ramp editor is not built")

for token in [
    "RAMP_PROFILES",
    "'grid_ramp'",
    "'generator_ramp'",
    "up_pct_s",
    "down_pct_s",
    "prefix: 'gridRamp'",
    "prefix: 'generatorRamp'",
    "generatorRampEnabled",
    "Limit the rate of change",
    "function collectRamps",
    "function renderRamps",
    "async function saveRamps",
    "async function loadRamps",
]:
    require(token in SOLAR_GRID, f"ramp editor is incomplete: {token}")

# It edits the fields the firmware actually persists under "control".
for token in ['"grid_ramp"', '"generator_ramp"', '"up_pct_s"', '"down_pct_s"']:
    require(token in CONFIG_MANAGER,
            f"ramp editor targets a field the firmware does not expose: {token}")

# The editor is loaded and saved only inside the Engineering control scope.
load_ramps = SOLAR_GRID[SOLAR_GRID.index("async function loadRamps"):
                        SOLAR_GRID.index("async function saveRamps")]
require("api('/api/config'" in load_ramps, "the ramp editor must read the persisted config")
require("await loadRamps();" in SOLAR_GRID.split("if (!controlScopeAllowed())", 1)[1],
        "the ramp read must sit behind the Engineering control scope gate")

# Safety copy: these three statements are the semantics, not decoration.
require("Disabling a ramp removes only the rate limit." in SOLAR_GRID,
        "the UI must state that disabling a ramp removes only the rate limit")
require("minimum-loading limit and every other safety clamp are applied first and still hold" in SOLAR_GRID,
        "the UI must state that policy, generator minimum loading and safety clamps still apply")
require("reach the allowed target immediately" in SOLAR_GRID,
        "the UI must define disabled as reaching the allowed target immediately")
require("never ignore the limits" in SOLAR_GRID,
        "the UI must deny that disabling a ramp ignores the limits")
require("Set the generator down rate higher than its up rate." in SOLAR_GRID,
        "the UI must direct the generator down rate above its up rate")
require("under-loading and reverse power" in SOLAR_GRID,
        "the UI must explain why the generator down direction is the protective one")

# The firmware rejects an enabled profile with a zero rate; that must surface as
# a validation message rather than an opaque POST failure.
require("ramp_profile_valid" in CONFIG_MANAGER,
        "firmware ramp validation predicate is missing")
require("r->enabled && (r->up_percent_per_second <= 0.0f || r->down_percent_per_second <= 0.0f)"
        in CONFIG_MANAGER,
        "firmware no longer rejects an enabled zero-rate ramp; the UI message would be wrong")
collect = SOLAR_GRID[SOLAR_GRID.index("function collectRamps"):
                     SOLAR_GRID.index("async function loadRamps")]
require("enabled && (up <= 0 || down <= 0)" in collect,
        "the ramp editor must reject an enabled zero-rate profile before it POSTs")
require("would freeze the PV command" in collect,
        "the zero-rate rejection must explain that a zero rate freezes the command")


# ------------------------------------------------------------ CI registration

require("tests/operator_endpoint_scope_source_contract.py" in WORKFLOW,
        "this contract is not registered in the build workflow")

print("operator endpoint scope and ramp commissioning source contract: PASS")
