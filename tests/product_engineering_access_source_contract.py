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
    "Electrical supply status", "Inverter fleet status", "installed capacity",
    "Solar production", "/api/meters",
    "/api/inverters", "/api/inverter-telemetry", "op-gauge",
    "operatorTrendHost", "Fleet availability",
]:
    require(token in OPERATOR, f"operator product view missing {token}")

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
