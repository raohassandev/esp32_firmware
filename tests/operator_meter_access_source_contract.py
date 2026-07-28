from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODE = (ROOT / "web/product-mode.js").read_text(encoding="utf-8")
CSS = (ROOT / "web/product-mode.css").read_text(encoding="utf-8")
ERRORS = (ROOT / "web/engineering-errors.js").read_text(encoding="utf-8")
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")


def require(value: bool, message: str) -> None:
    if not value:
        raise AssertionError(message)


for token in [
    "dataset.access = 'pending'", "ENGINEERING_ONLY_SELECTORS",
    "enforceEngineeringDom", "MutationObserver", "amx-access-change"
]:
    require(token in MODE, f"operator engineering DOM gate missing {token}")

for token in [
    'html[data-access="pending"] #em500Workspace',
    'html[data-access="operator"] #em500Workspace',
    'html[data-access="operator"] #inverterProfilePicker',
]:
    require(token in CSS, f"immediate engineering workspace hiding missing {token}")

for token in [
    "ESP_ERR_INVALID_RESPONSE", "Measurement group unavailable",
    "Not supported by the selected meter profile", "diagnosticCode"
]:
    require(token in ERRORS, f"friendly engineering error handling missing {token}")

require("Diagnostic code: ${translated.code}" not in ERRORS,
        "visible ESP_ERR token can recursively retrigger the error observer")
require("characterData: true" not in ERRORS,
        "error observer must not watch its own text mutations")

require("engineering-errors.js" in CMAKE,
        "engineering error module is not embedded")
require("web_assets_engineering_errors_js" in SERVER,
        "engineering error module is not served")

print("operator meter access and engineering error presentation contract passed")
