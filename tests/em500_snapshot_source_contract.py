from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
API = (ROOT / "components/web_server/em500_api.c").read_text(encoding="utf-8")
MANAGER = (ROOT / "components/meter_manager/meter_manager.c").read_text(encoding="utf-8")
HEADER = (ROOT / "components/meter_manager/include/meter_manager.h").read_text(encoding="utf-8")
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require('"/api/meters/em500/snapshot"' in API,
        "EM500 snapshot endpoint is missing")
require("HTTP_GET" in API, "EM500 snapshot must be read-only GET")
require("HTTP_POST" not in API, "EM500 snapshot source must not expose POST")
require("modbus_tcp_write_single" not in API and "modbus_tcp_write_multiple" not in API,
        "EM500 snapshot must not contain Modbus writes")
require("EM500_MAX_READ_REGISTERS 80" in API,
        "DMG6-compatible register reads must be bounded to 80 registers")
require("0x2160" in API and '"clone_specific"' in API,
        "clone source input 0x2160 must remain explicitly classified")
require('"0=grid,1=generator"' in API,
        "site source-input mapping is missing")
require("modbus_decode_u64_be_scaled" in API,
        "energy counters must use the 64-bit decoder")
require('"raw_u64"' in API and '"raw_hex"' in API,
        "energy response must preserve exact raw counter data")
require('"writes_enabled", false' in API,
        "setup snapshot must state that writes are disabled")
require("0x5000" in API and "0x500C" in API,
        "CT/PT/wiring setup block is incomplete")
require("0x1B20" in API and "0x1E70" in API,
        "total and per-phase energy blocks are incomplete")
require("meter_manager_read_registers" in HEADER,
        "serialized read-only register interface is missing")
require("modbus_tcp_read_registers(&meter->connection" in MANAGER,
        "commissioning reads must reuse the meter manager connection")
require("modbus_tcp_write" not in MANAGER.split("meter_manager_read_registers", 1)[1],
        "meter-manager read helper must not perform a write")
require("em500_api_register(s_server)" in SERVER,
        "EM500 endpoint is not registered")
capacity = re.search(r"config\.max_uri_handlers\s*=\s*(\d+)", SERVER)
require(capacity is not None and int(capacity.group(1)) >= 21,
        "HTTP handler capacity must retain room for all meter/settings endpoints")
require('"em500_api.c"' in CMAKE and "modbus_tcp" in CMAKE,
        "EM500 source/dependency is missing from the component build")

print("EM500 read-only snapshot source contract passed")
