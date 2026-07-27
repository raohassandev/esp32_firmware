from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
API = (ROOT / "components/web_server/operational_api.c").read_text(encoding="utf-8")
SUITE = (ROOT / "web/operator-product-suite.js").read_text(encoding="utf-8")
ROUTE = (ROOT / "web/commissioning-route.js").read_text(encoding="utf-8")
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
require("dataset.access !== 'engineering'" in ROUTE, "commissioning route must reject operator access")
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
    "commissioning-route.js",
]:
    require(asset in CMAKE, f"{asset} is not embedded")

require("web_assets_operator_product_suite_css" in SERVER, "operator suite CSS is not served")
require("web_assets_operator_product_suite_js" in SERVER, "operator suite JS is not served")
require("web_assets_commissioning_route_js" in SERVER, "commissioning route JS is not served")

print("Complete operator product suite source contract passed")
