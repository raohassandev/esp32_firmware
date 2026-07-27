from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
API = (ROOT / "components/web_server/inverter_config_api.c").read_text(encoding="utf-8")
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
UI = (ROOT / "web/inverter-config.js").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require('"/api/inverters/config"' in API, "full inverter configuration endpoint is missing")
require("APP_MAX_INVERTERS" in API, "inverter count must be bounded")
require("duplicate_enabled_endpoint" in API, "duplicate endpoints must be rejected")
require('"409 Conflict"' in API, "duplicate endpoint conflict response is missing")
require("memset(config->inverters, 0, sizeof(config->inverters))" in API,
        "removed inverter slots must be cleared")
require("config->inverter_count = (uint8_t)requested_count" in API,
        "saved inverter count must match the request")
require("config->control.enabled = false" in API,
        "inverter changes must disable automatic control")
require("command_registers_changed" in API and "false}" in API,
        "endpoint must disclose that command mappings are untouched")
require("modbus_tcp_write" not in API and "modbus_write" not in API,
        "configuration endpoint must never write an inverter")
require("inverter_config_api_register(s_server)" in SERVER,
        "web server must register inverter configuration")
require('"inverter_config_api.c"' in CMAKE and '"inverter-config.js"' in CMAKE,
        "firmware must compile and embed inverter configuration modules")
require("/api/inverters/config" in UI, "browser editor must use dedicated endpoint")
require("Automatic control will be disabled" in UI,
        "browser editor must disclose the safety effect")
require("same endpoint and unit ID" in UI,
        "browser editor must reject duplicate endpoints")
require("command registers" in UI,
        "browser editor must state that command registers are not changed")

print("inverter configuration source contract: PASS")
