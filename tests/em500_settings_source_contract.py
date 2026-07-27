from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
API = (ROOT / "components/web_server/em500_settings_api.c").read_text(encoding="utf-8")
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require('"/api/meters/em500/settings"' in API,
        "complete EM500 settings endpoint is missing")
require("HTTP_GET" in API and "HTTP_POST" not in API,
        "settings catalogue must be read-only GET")
require("SETTINGS_MAX_WORDS 80" in API,
        "DMG6 setup reads must remain bounded to 80 registers")
for menu in range(1, 19):
    require(f'"M{menu:02d}"' in API, f"M{menu:02d} setup menu is missing")
for address in ("0x5000", "0x5080", "0x5100", "0x5180", "0x5200",
                "0x5280", "0x5300", "0x5400", "0x5800", "0x5C00",
                "0x5E00", "0x6080", "0x6480", "0x6880", "0x6C80",
                "0x6E80", "0x7080", "0x6B40"):
    require(address in API, f"setup base {address} is missing")
require("SETTING_SENSITIVE_U16" in API,
        "password registers are not classified as sensitive")
require('"user_password"' in API and '"advanced_password"' in API,
        "password fields must be explicitly catalogued and masked")
require('"masked", true' in API and 'cJSON_AddNullToObject(object, "value")' in API,
        "sensitive values must never be returned")
require('"writes_enabled", false' in API,
        "setup writes must remain disabled")
require('"tariff_write_exposed", false' in API,
        "tariff writes must remain gated")
require('"reset_energy_exposed", false' in API,
        "destructive energy reset must not be exposed")
require("modbus_tcp_write" not in API,
        "settings catalogue must not contain Modbus writes")
require("meter_manager_read_registers" in API,
        "settings reads must use the serialized meter-manager connection")
require("em500_settings_api_register(s_server)" in SERVER,
        "settings endpoint is not registered")
require("config.max_uri_handlers = 19" in SERVER,
        "HTTP handler capacity must include the settings endpoint")
require('"em500_settings_api.c"' in CMAKE,
        "settings source is missing from the ESP-IDF component")

print("Complete EM500 settings catalogue source contract passed")
