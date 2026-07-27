from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
API = (ROOT / "components/web_server/meter_config_api.c").read_text(encoding="utf-8")
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
LEGACY = (ROOT / "components/config_manager/config_manager.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require('"/api/meters/config"' in API, "dedicated meter configuration endpoint is missing")
require("HTTP_POST" in API, "meter configuration endpoint must be POST")
require("APP_MAX_METERS" in API, "meter count must be bounded by APP_MAX_METERS")
require("for (int index = 0; index < requested_count; ++index)" in API,
        "all requested meter profiles must be parsed")
require("config_manager_get_snapshot(config)" in API,
        "meter update must start from the complete current configuration")
require("memset(config->meters, 0, sizeof(config->meters))" in API,
        "removed meter slots must be cleared")
require("config->meter_count = (uint8_t)requested_count" in API,
        "persisted meter count must match the request")
require("config->control.enabled = false" in API,
        "meter changes must force automatic control disabled")
require("duplicate_enabled_endpoint" in API,
        "duplicate enabled host/port/unit endpoints must be rejected")
require('"409 Conflict"' in API, "duplicate endpoint conflict response is missing")
require('"restart_required\\\":true' in API,
        "successful update must state that restart is required")
require("config_manager_save(config)" in API,
        "meter configuration must use verified NVS persistence")
require("esp_wifi_" not in API, "meter configuration API must not operate the Wi-Fi radio")
require("modbus_write" not in API and "modbus_tcp_write" not in API,
        "meter profile commissioning must not write meter registers")
require("meter_config_api_register(s_server)" in SERVER,
        "meter configuration endpoint is not registered")
require("config.max_uri_handlers = 18" in SERVER,
        "HTTP handler capacity must include meter configuration and EM500 snapshot endpoints")
require('"meter_config_api.c"' in CMAKE,
        "meter configuration source is not part of the ESP-IDF component")

# The generic importer remains for compatibility, but the dedicated endpoint must
# be used for multi-meter commissioning until it is upgraded separately.
require("cJSON_GetArrayItem(meters, 0)" in LEGACY,
        "update this contract when the legacy generic importer becomes multi-meter aware")

print("Multi-meter configuration source contract passed")
