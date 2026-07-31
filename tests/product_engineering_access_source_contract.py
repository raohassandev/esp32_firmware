from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUTH = (ROOT / "components/web_server/engineering_auth.c").read_text(encoding="utf-8")
AUTH_HEADER = (ROOT / "components/web_server/include/engineering_auth.h").read_text(encoding="utf-8")
GUARD = (ROOT / "components/web_server/engineering_guard.c").read_text(encoding="utf-8")
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
JS = (ROOT / "web/product-mode.js").read_text(encoding="utf-8")
OPERATOR = (ROOT / "web/operator-view.js").read_text(encoding="utf-8")
# The operator view no longer draws its own trend: it provides the single
# mount point that the shared chart component is placed into. The trend
# titles are therefore asserted in the component that now renders them.
CHART = (ROOT / "web/pvdg-chart.js").read_text(encoding="utf-8")
OPERATIONS = (ROOT / "web/operator-operations.js").read_text(encoding="utf-8")
CSS = (ROOT / "web/product-mode.css").read_text(encoding="utf-8")


def require(value: bool, message: str) -> None:
    if not value:
        raise AssertionError(message)


require("AUTH_TEMPORARY_FIELD_BYPASS" not in AUTH,
        "temporary Engineering authentication bypass must not return")
for token in [
    "AUTH_SESSION_TIMEOUT_MS", "AUTH_LOCKOUT_MS", "AUTH_MAX_FAILURES",
    "derive_password_hash", "constant_time_equal", "session_cookie_valid", "create_session",
    "One-time Engineering setup code", "/api/engineering/login",
    "/api/engineering/logout", "/api/engineering/password",
    "Set-Cookie", "HttpOnly", "SameSite=Strict", "eng_session",
    "nvs_set_blob", "mbedtls_md_hmac", "AUTH_BODY_DEADLINE_MS", "json_depth_valid",
]:
    require(token in AUTH, f"Engineering authentication missing {token}")

for token in [
    "engineering_register_uri_handler", "engineering_auth_guarded_handler",
    "safe_config", "safe_meters", "safe_inverters", "safe_inverter_telemetry",
    '"engineering_details_hidden"', '"operator_view"',
    '"/api/status"', '"/api/telemetry"',
]:
    require(token in GUARD, f"API security gateway missing {token}")

require("#define httpd_register_uri_handler engineering_register_uri_handler" in AUTH_HEADER,
        "web APIs are not routed through the registration security gateway")
require("#ifndef ENGINEERING_GUARD_IMPLEMENTATION" in AUTH_HEADER,
        "gateway implementation cannot safely opt out of URI registration replacement")
require("ENGINEERING_GUARD_IMPLEMENTATION=1" in CMAKE,
        "engineering gateway source is not isolated from the registration macro")
require("httpd_register_uri_handler=engineering_register_uri_handler" not in CMAKE,
        "fragile component-wide command-line URI replacement must not return")
require("engineering_auth_register" in SERVER and "engineering_auth_init" in SERVER,
        "Engineering auth is not initialized and registered")
require("web_assets_product_mode_js" in SERVER and "web_assets_product_mode_css" in SERVER,
        "product-mode assets are not served")
require("web_assets_operator_view_js" in SERVER and "operator-view.js" in CMAKE,
        "dedicated operator product view is not embedded and served")

for token in [
    "credentials: 'same-origin'", "Engineering and commissioning",
    "Change engineering password", "PROTECTED_ROUTES",
]:
    require(token in JS, f"product UI access flow missing {token}")

# The Engineering route used to be activated here, by a second copy of the
# router that also rewrote the page title and breadcrumb after app.js had
# written them. This module now only injects the page; app.js selects it when
# the shared content notifier reports the addition. Assert the handover, which
# is the property that matters, rather than the name of the removed function.
APP = (ROOT / "web/app.js").read_text(encoding="utf-8")
require("page.dataset.page = 'engineering'" in JS,
        "the Engineering page is no longer injected")
require("onContentChange" in JS,
        "the access module must publish the shared content notifier")
# The route table carries one durable name per page (nav label, title,
# breadcrumb and document.title are the same string) rather than a separate
# title and breadcrumb; see tests/ia_taxonomy_source_contract.py. The property
# asserted is unchanged: the router must know this route, or routeFromHash()
# falls back to 'dashboard' and stamps that into document.title.
require("engineering: { name: 'Engineering access'" in APP,
        "the router does not know the Engineering route")
require("onContentChange(applyRoute)" in APP,
        "the router must re-apply the route when a page is injected late")
require("classList.toggle('active'" in APP and "classList.toggle('active'" not in JS,
        "page activation must happen in exactly one module")
require("sessionStorage" not in JS,
        "Engineering session token must not be stored in browser-accessible sessionStorage")
require("X-Engineering-Token" not in JS,
        "operator browser code must rely on the HTTP-only session cookie")

for token in [
    "installed capacity", "Solar production", "/api/meters",
    "/api/inverters", "/api/inverter-telemetry",
    "operatorTrendHost",
]:
    require(token in OPERATOR, f"operator product view missing {token}")

# "Electrical supply status" and "Inverter fleet status" were on the list above.
# They were section headings printed under a top bar that already prints "Grid
# power" and "Solar inverters" with a subtitle each, so they were the third copy
# of the page's identity, and the row that carried them was a 60px band with one
# button in it. They are gone for the same reason the capitalised page names
# below are forbidden. The property is now asserted as structure rather than as
# a pinned sentence: a header with nothing to say must produce no element.
require("if (!title && !subtitle) return null;" in OPERATOR,
        "an operator section header with no title must render nothing at all; the "
        "empty header row was the largest single piece of dead space in the product")
require("view.append(sectionHeader('', '', 'Refresh'))" not in OPERATOR,
        "a section header must not be emitted purely to carry a Refresh button")

# "op-gauge" and "Fleet availability" were on the list above. Both are gone, and
# both are replaced by components that carry more than one number each. These
# assertions are deliberately stronger than the two tokens they replace: they
# pin the SAFETY behaviour of the replacements, not merely their existence.
for token in ["op-measure", "function measureBar", "function fleetCard", "op-fleet-bar"]:
    require(token in OPERATOR,
            f"operator product view missing the instrument that replaced the gauge: {token}")

# A gauge drew |value| against a maximum recomputed from the live reading, so
# import and export folded onto the same side and the zero-export boundary was
# not on the instrument at all. The replacement must put zero on the scale.
require("'Zero export'" in OPERATOR and "signed: true" in OPERATOR,
        "the grid-power instrument must carry the zero-export boundary on its scale")

# SAFETY. An unmeasured quantity gets no marker anywhere on the track. The
# marker is drawn inside a conditional on the measured-ness of the value, and
# the track is marked so the stylesheet can draw it as an incomplete instrument.
require("op-measure-unmeasured" in OPERATOR,
        "an instrument with no measurement must be marked as such, not drawn as a "
        "complete scale whose indicator happens to rest at zero")
require("if (known) {" in OPERATOR,
        "the current-value marker must be drawn only when the value is measured")

# SAFETY. "0.00 %" for fleet availability with no inverter enabled is a
# fabricated measurement: the denominator is zero. The replacement must refuse
# to state a percentage it cannot compute.
require("enabled > 0 ? (online / enabled) * 100 : NaN" in OPERATOR,
        "fleet availability must be unavailable, not zero, when no inverter is "
        "enabled; there is no denominator for the percentage")
# Bound to the fleet card itself, not to the file: commandable_rated_kw also
# appears in the control screen's prerequisites, so a file-wide check would stay
# green with the fleet card's capacity accounting deleted.
_fleet = OPERATOR[OPERATOR.index("function fleetCard("):]
_fleet = _fleet[:_fleet.index("\n    function ")]
for _figure in ["configured_rated_kw", "enabled_rated_kw", "commandable_rated_kw"]:
    require(_figure in _fleet,
            f"the fleet card must account for {_figure}; commandable capacity in "
            "particular is what automatic control can actually move, and it is the "
            "number the control loop runs on")
require("'Commandable'" in _fleet,
        "the fleet card must label the commandable capacity for a reader")

# SAFETY. Number(null) is 0, and /api/inverters returns measured_power_kw: null
# for an inverter that is not enabled. Coercing that to a reading printed
# "0.00 kW" for an inverter nobody was measuring. The numeric guard must reject
# the values that mean "not measured" whatever they coerce to.
require("value === null || value === undefined || value === ''" in OPERATOR
        and "typeof value === 'boolean'" in OPERATOR,
        "the operator view must not accept null, empty string or a boolean as a "
        "measurement; Number(null) is 0 and that prints an unmeasured quantity as "
        "zero on a reverse-power controller")

# "Grid Power" and "Solar Inverters" used to be on the list above. They were the
# operator-only spellings of two page names, printed as an eyebrow over each
# screen - and tests/ia_taxonomy_source_contract.py now forbids exactly that:
# one durable name per page, "Grid power" and "Solar inverters", owned by the
# route table in web/app.js and applied to the sidebar, the title, the breadcrumb
# and document.title from one place. Asserting the capitalised variants here was
# pulling against that rule and pinning a third spelling of each page name into
# the operator screen. The property is stronger stated as a prohibition.
#
# Only the operator-side variants are listed. "Inverters" as the heading of a
# table of inverters is a noun, not a second name for a page.
for invented in ["'Grid Power'", "'Solar Inverters'", "'Meters'"]:
    require(invented not in OPERATOR,
            f"the operator view spells a page name of its own ({invented}); page "
            "names come from the route table in web/app.js so that an instruction "
            "given over the phone matches what is on the screen")

# "Operator guidance" was the headline of a card that printed a paragraph in
# every state, including the state where nothing is wrong. It is now a card that
# appears ONLY when something is blocking automatic control, and says what to do
# rather than describing the situation. The assertion follows the requirement:
# this screen must tell an operator what action is required.
require("'Required action'" in OPERATOR and "function controlActions" in OPERATOR,
        "the control screen must state the required action when automatic control "
        "is blocked")
require("action:" in OPERATOR and "why:" in OPERATOR and "condition:" in OPERATOR,
        "an operator message must carry all three beats - what is true now, why it "
        "matters, and what to do - not just a description")
require(".slice(0, 3)" in OPERATOR,
        "the required actions must be bounded; an unbounded list of things to do "
        "is a list nobody does")

# The operator trend is still present on the dashboard, the grid page and the
# solar page - it is now one shared component rather than a second chart
# implementation copied into this file.
require("function sparkline" not in OPERATOR,
        "the operator view must not carry a second chart implementation")
for token in ["Grid power trend", "Solar production trend", "Plant power trend"]:
    require(token in OPERATIONS, f"operator trend chart missing {token}")
require("PvdgChart" in OPERATIONS and "create" in CHART,
        "the operator screens must mount the shared chart component")

for forbidden in ["PDU", "function code", "raw words", "meterScale", "meterAddress", "limit register"]:
    require(forbidden.lower() not in OPERATOR.lower(),
            f"operator product view exposes Engineering data or controls: {forbidden}")

for token in [
    'html[data-access="operator"] #em500Workspace',
    'html[data-access="operator"] .device-meta-grid',
    'html[data-access="operator"] #inverterProfilePicker',
    'html[data-access="operator"] #inverterConfigurationEditor',
    'html[data-access="operator"] [data-page="system"] .panel-actions',
    'html[data-access="operator"] #meterRuntimeList',
    'html[data-access="operator"] #inverterRuntimeList',
]:
    require(token in CSS, f"operator UI does not hide Engineering detail: {token}")

print("product/operator separation contract passed with production Engineering authentication")
