from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
API = (ROOT / "components/web_server/em500_history_api.c").read_text(encoding="utf-8")
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require('"/api/meters/em500/history"' in API,
        "EM500 historical measurement endpoint is missing")
require("HTTP_GET" in API and "HTTP_POST" not in API,
        "historical measurements must be read-only GET")
require("HISTORY_WORDS 72" in API,
        "historical block must use the documented 72-register span")
require("HISTORY_WORDS" in API and "72" in API,
        "historical read bound is missing")
for block in ("maximum", "minimum", "average", "demand"):
    require(f'"{block}"' in API, f"{block} measurement block is missing")
for address in ("0x0400", "0x0600", "0x0800", "0x0A00"):
    require(address in API, f"historical block base {address} is missing")
for measurement in ("voltage_l1_n", "current_l1", "active_power_total",
                    "reactive_power_total", "apparent_power_total",
                    "frequency", "power_factor_total", "current_neutral"):
    require(f'"{measurement}"' in API,
            f"historical measurement {measurement} is missing")
require("meter_manager_read_registers" in API,
        "history endpoint must use the serialized meter-manager connection")
require("modbus_tcp_write" not in API,
        "history endpoint must not perform Modbus writes")
require("em500_history_api_register(s_server)" in SERVER,
        "history endpoint is not registered")
require("config.max_uri_handlers = 21" in SERVER,
        "HTTP handler capacity must include the history endpoint")
require('"em500_history_api.c"' in CMAKE,
        "history source is missing from the ESP-IDF component")

print("EM500 historical measurement source contract passed")
