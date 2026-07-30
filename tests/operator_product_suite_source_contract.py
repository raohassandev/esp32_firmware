import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def code(text: str) -> str:
    """Executable source only. Ownership is documented in comments that name
    the identifiers a module must NOT touch, so an ownership assertion has to
    look at what the module runs, not at what it says about itself."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"(?m)^\s*//.*$", "", text)
API = (ROOT / "components/web_server/operational_api.c").read_text(encoding="utf-8")
SUITE = (ROOT / "web/operator-product-suite.js").read_text(encoding="utf-8")
# web/commissioning-route.js was a second router: it re-activated the
# commissioning page, rewrote #pageTitle, #breadcrumbCurrent and document.title
# after app.js had already written them, and re-ran itself from a
# MutationObserver on documentElement. app.js is now the only router, and the
# access rule below is asserted against the module that actually owns access.
MODE = (ROOT / "web/product-mode.js").read_text(encoding="utf-8")
CSS = (ROOT / "web/operator-product-suite.css").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


for token in [
    '"/api/operator/history"',
    '"/api/operator/events"',
    "FAST_SAMPLE_COUNT 180",
    "MINUTE_SAMPLE_COUNT 1440",
    "EVENT_COUNT 96",
    "Grid measurement unavailable",
    "Solar fleet attention required",
    "recommended_action",
]:
    require(token in API, f"operator history/event engine missing: {token}")

for token in [
    "product-mobile-nav",
    "Kiosk mode",
    "Compact view",
    "openMeterDetails",
    "openInverterDetails",
    "Guided site commissioning",
    "exportCommissioningReport",
    "Automatic control and physical inverter writes remain locked",
    "Engineering",
]:
    require(token in SUITE, f"operator product feature missing: {token}")

require("data-page=\"commissioning\"" in SUITE, "protected commissioning page is missing")
# The commissioning route must still be refused to an unauthenticated operator,
# and it must be refused from live authorisation state rather than from a
# data-attribute another module happens to have set. Asserted as a property of
# the access module: 'commissioning' is a protected route, and the protected
# route check consults state.authenticated and sends the caller to sign in.
require("PROTECTED_ROUTES" in MODE and "'commissioning'" in MODE.split("const ENGINEERING_ONLY_SELECTORS", 1)[0],
        "commissioning must be declared a protected route")
enforce = MODE[MODE.index("async function enforceRoute"):MODE.index("document.addEventListener('DOMContentLoaded'")]
require("PROTECTED_ROUTES.has(route)" in enforce and "!state.authenticated" in enforce,
        "the protected-route check must consult live authorisation state")
require("openLogin(" in enforce,
        "an unauthorised protected route must send the operator to sign in")
# Browser-side refusal is noise removal, not authorization: the server's
# default-deny gateway is the barrier. Neither may be the only one.
require("engineering_auth_is_authorized" in
        (ROOT / "components/web_server/engineering_guard.c").read_text(encoding="utf-8"),
        "server-side default-deny enforcement must remain the real barrier")
# There must not be a second router rewriting the route title behind app.js.
require(not (ROOT / "web/commissioning-route.js").exists(),
        "the duplicate commissioning router must stay deleted")
for owner in ("web/product-mode.js", "web/product-shell-v2.js", "web/product-experience-v2.js"):
    body = code((ROOT / owner).read_text(encoding="utf-8"))
    require("breadcrumbCurrent" not in body,
            f"{owner} must not write the route breadcrumb; app.js owns it")
    require("pageTitle" not in body,
            f"{owner} must not write the route title; app.js owns it")
require("breadcrumbCurrent" in code((ROOT / "web/app.js").read_text(encoding="utf-8")),
        "app.js must remain the module that writes the breadcrumb")
require("requestFullscreen" in SUITE and "fullscreenchange" in SUITE, "kiosk/fullscreen lifecycle is missing")
require("localStorage" in SUITE and "density" in SUITE, "display-density preference persistence is missing")
require("Blob" in SUITE and "download" in SUITE, "commissioning report export is missing")

for token in [
    ".product-mobile-nav",
    ".product-modal-backdrop",
    ".commissioning-step",
    ".kiosk-mode",
    'html[data-density="compact"]',
    "@media (max-width: 860px)",
]:
    require(token in CSS, f"operator product styling missing: {token}")

for asset in [
    "operator-product-suite.css",
    "operator-product-suite.js",
]:
    require(asset in CMAKE, f"{asset} is not embedded")

require("web_assets_operator_product_suite_css" in SERVER, "operator suite CSS is not served")
require("web_assets_operator_product_suite_js" in SERVER, "operator suite JS is not served")
# The deleted module must be gone from all three registration points, not just
# from the source tree; a stale getter or EMBED_FILES entry breaks the build.
for stale in ("commissioning-route.js", "commissioning_route_js"):
    require(stale not in CMAKE, f"deleted asset still referenced in CMakeLists: {stale}")
require("commissioning_route_js" not in SERVER, "deleted asset still served")
require("commissioning_route_js" not in
        (ROOT / "components/web_server/web_assets.c").read_text(encoding="utf-8"),
        "deleted asset still declared in web_assets.c")
require("commissioning_route_js" not in
        (ROOT / "components/web_server/include/web_assets.h").read_text(encoding="utf-8"),
        "deleted asset still declared in web_assets.h")

print("Complete operator product suite source contract passed")
