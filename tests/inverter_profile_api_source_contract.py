from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
API = (ROOT / "components/web_server/inverter_profile_api.c").read_text(encoding="utf-8")
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
STORE = (ROOT / "components/inverter_manager/inverter_profile_store.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require('"/api/inverter-profiles"' in API, "profile catalogue endpoint is missing")
require('"/api/inverter-profile-assignment"' in API,
        "profile assignment endpoint is missing")
require("HTTP_POST" in API, "profile assignment must use POST")
require("inverter_profiles_count" in API and "inverter_profiles_get" in API,
        "endpoint must enumerate the firmware catalogue")
require("inverter_profile_allows_write" in API,
        "endpoint must report centralized write eligibility")
require('"writes_require_production_approval"' in API,
        "endpoint must disclose the production approval gate")
require("inverter_profile_store_set" in API,
        "assignment endpoint must persist through the profile store")
require('"automatic_control_disabled"' in API and '"restart_required"' in API,
        "assignment response must disclose safety effects")
require("config->control.enabled = false" in STORE,
        "profile changes must disable automatic control")
require("inverter_profiles_find(profile_id)" in STORE,
        "profile store must reject unknown profile ids")
require("inverter_profile_api_register(s_server)" in SERVER,
        "web server must register the profile endpoint")
require('"inverter_profile_api.c"' in CMAKE,
        "web component must compile the profile endpoint")

print("Inverter profile API source contract passed")
