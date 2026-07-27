from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
API = (ROOT / "components/web_server/em500_settings_plan_api.c").read_text(encoding="utf-8")
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require('"/api/meters/em500/settings/plan"' in API,
        "EM500 settings plan endpoint is missing")
require("HTTP_POST" in API,
        "settings planner must accept an explicit JSON plan request")
require("modbus_tcp_write" not in API,
        "settings planner must never write Modbus registers")
require('"writes_performed", false' in API,
        "planner must state that no writes occurred")
require('"apply_available", false' in API,
        "physical apply must remain unavailable")
for key in ("ct_primary_a", "ct_secondary_a", "rated_voltage_v", "use_vt",
            "vt_primary_v", "vt_secondary_v", "wiring", "active_tariff"):
    require(f'"{key}"' in API, f"planner is missing {key}")
require("Unknown setting" in API,
        "planner must reject unknown fields")
require("memcmp(current_words, requested_words" in API,
        "planner must omit unchanged values")
require('"service_authorization", true' in API,
        "service authorization gate is missing")
require('"model_fingerprint", true' in API,
        "model fingerprint gate is missing")
require('"readback", true' in API and '"rollback", true' in API,
        "readback/rollback gates are missing")
require('"EM500_UNVERIFIED_COMMAND"' in API,
        "tariff command must remain classified as unverified")
require("em500_settings_plan_api_register(s_server)" in SERVER,
        "settings planner is not registered")
capacity = re.search(r"config\.max_uri_handlers\s*=\s*(\d+)", SERVER)
require(capacity is not None and int(capacity.group(1)) >= 21,
        "HTTP handler capacity must retain room for settings planner")
require('"em500_settings_plan_api.c"' in CMAKE,
        "settings planner is missing from the ESP-IDF component")

print("EM500 settings change-plan safety contract passed")
