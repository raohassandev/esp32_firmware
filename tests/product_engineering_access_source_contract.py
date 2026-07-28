from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUTH = (ROOT / "components/web_server/engineering_auth.c").read_text(encoding="utf-8")
AUTH_HEADER = (ROOT / "components/web_server/include/engineering_auth.h").read_text(encoding="utf-8")
GUARD = (ROOT / "components/web_server/engineering_guard.c").read_text(encoding="utf-8")
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
JS = (ROOT / "web/product-mode.js").read_text(encoding="utf-8")
OPERATOR = (ROOT / "web/operator-view.js").read_text(encoding="utf-8")
CSS = (ROOT / "web/product-mode.css").read_text(encoding="utf-8")


def require(value: bool, message: str) -> None:
    if not value:
        raise AssertionError(message)


field_bypass = "AUTH_TEMPORARY_FIELD_BYPASS 1" in AUTH
if field_bypass:
    for token in [
        "TEMPORARY FIELD-DEVELOPMENT BYPASS",
        '"temporary_field_bypass", true',
        "Engineering authentication disabled for field development",
        "Password management is disabled",
        "return AUTH_TEMPORARY_FIELD_BYPASS != 0",
    ]:
        require(token in AUTH, f"temporary field bypass is not explicit: {token}")
else:
    for token in [
        "AUTH_SESSION_MS", "AUTH_LOCKOUT_MS", "AUTH_MAX_FAILURES",
        "hash_password", "constant_time_equal", "constant_time_token_equal", "new_session",
        "Engineering temporary password", "/api/engineering/login",
        "/api/engineering/logout", "/api/engineering/password",
        "Set-Cookie", "HttpOnly", "SameSite=Strict", "AMXENG",
    ]:
        require(token in AUTH, f"engineering authentication missing {token}")

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
        "engineering auth is not initialized and registered")
require("web_assets_product_mode_js" in SERVER and "web_assets_product_mode_css" in SERVER,
        "product-mode assets are not served")
require("web_assets_operator_view_js" in SERVER and "operator-view.js" in CMAKE,
        "dedicated operator product view is not embedded and served")

for token in [
    "credentials: 'same-origin'", "Engineering and commissioning",
    "Change engineering password", "PROTECTED_ROUTES", "activateEngineeringRoute",
]:
    require(token in JS, f"product UI access flow missing {token}")
require("sessionStorage" not in JS,
        "engineering session token must not be stored in browser-accessible sessionStorage")
require("X-Engineering-Token" not in JS,
        "operator browser code must rely on the HTTP-only session cookie")

for token in [
    "Electrical supply status", "Inverter fleet status", "installed capacity",
    "Solar production", "Grid Power", "Solar Inverters", "/api/meters",
    "/api/inverters", "/api/inverter-telemetry", "op-gauge", "sparkline",
    "Grid power trend", "Fleet availability", "Operator guidance",
]:
    require(token in OPERATOR, f"operator product view missing {token}")

for forbidden in ["PDU", "function code", "raw words", "meterScale", "meterAddress", "limit register"]:
    require(forbidden.lower() not in OPERATOR.lower(),
            f"operator product view exposes engineering data or controls: {forbidden}")

for token in [
    'html[data-access="operator"] #em500Workspace',
    'html[data-access="operator"] .device-meta-grid',
    'html[data-access="operator"] #inverterProfilePicker',
    'html[data-access="operator"] #inverterConfigurationEditor',
    'html[data-access="operator"] [data-page="system"] .panel-actions',
    'html[data-access="operator"] #meterRuntimeList',
    'html[data-access="operator"] #inverterRuntimeList',
]:
    require(token in CSS, f"operator UI does not hide engineering detail: {token}")

print("product/operator separation contract passed (field bypass=%s)" % field_bypass)
