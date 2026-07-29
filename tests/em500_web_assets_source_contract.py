from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")
ASSETS = (ROOT / "components/web_server/web_assets.c").read_text(encoding="utf-8")
HEADER = (ROOT / "components/web_server/include/web_assets.h").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


css_assets = [
    "web_assets_em500_css",
]
js_assets = [
    "web_assets_em500_utils_js",
    "web_assets_em500_core_js",
    "web_assets_em500_quality_js",
    "web_assets_em500_profiles_js",
    "web_assets_em500_plan_js",
]

for symbol in css_assets + js_assets:
    require(symbol in SERVER, f"{symbol} is not delivered by the HTTP asset handlers")
    require(symbol in ASSETS, f"{symbol} is not exported by web_assets.c")
    require(symbol in HEADER, f"{symbol} is not declared by web_assets.h")

for filename in [
    "em500.css",
    "em500-utils.js",
    "em500-core.js",
    "em500-quality.js",
    "em500-profiles.js",
    "em500-plan.js",
]:
    require(filename in CMAKE, f"{filename} is not embedded by the web component build")

js_order = [SERVER.index(symbol) for symbol in js_assets]
require(js_order == sorted(js_order),
        "EM500 browser modules must be delivered in utils, core, quality, profiles, plan order")

require("web_assets_em500_css" in SERVER.split("static esp_err_t css_handler", 1)[1].split("static esp_err_t js_handler", 1)[0],
        "EM500 stylesheet must be included in /app.css")
require(all(symbol in SERVER.split("static esp_err_t js_handler", 1)[1].split("esp_err_t web_server_start", 1)[0]
            for symbol in js_assets),
        "All EM500 scripts must be included in /app.js")
require("DECLARE_ASSET(em500_quality_js)" in ASSETS,
        "analyser quality linker asset is missing")
require("ASSET_GETTER(web_assets_em500_quality_js, em500_quality_js)" in ASSETS,
        "analyser quality asset getter is missing")

print("EM500 web asset delivery source contract passed")
