from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
API = (ROOT / "components/web_server/inverter_profile_api.c").read_text(encoding="utf-8")
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require('"/api/inverter-profiles"' in API, "profile catalogue endpoint is missing")
require("inverter_profiles_count" in API and "inverter_profiles_get" in API,
        "endpoint must enumerate the firmware catalogue")
require("inverter_profile_allows_write" in API,
        "endpoint must report centralized write eligibility")
require('"writes_require_production_approval"' in API,
        "endpoint must disclose the production approval gate")
require("inverter_profile_api_register(s_server)" in SERVER,
        "web server must register the profile endpoint")
require('"inverter_profile_api.c"' in CMAKE,
        "web component must compile the profile endpoint")

print("Inverter profile API source contract passed")
