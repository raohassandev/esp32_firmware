from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
API = (ROOT / "components/web_server/em500_api.c").read_text(encoding="utf-8")
CACHE = (ROOT / "components/web_server/em500_cache.c").read_text(encoding="utf-8")
ADAPTER = (ROOT / "components/web_server/em500_cache_adapter.c").read_text(encoding="utf-8")
CACHE_HEADER = (ROOT / "components/web_server/include/em500_cache.h").read_text(encoding="utf-8")
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
require("EM500_SOURCE_INPUT_TABLE_ADDRESS" in API and '"clone_specific"' in API,
        "the clone source input must remain explicitly classified and must read the "
        "shared register definition rather than a literal")
require("0x2160" not in API and "0x2160" not in CACHE and "0x2160" not in ADAPTER,
        "0x2160 answers Modbus exception 0x02 on the installed meters; it must not "
        "survive as a live address on the snapshot, cache or adapter path")
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

# The HTTP source retains its stable decoder/JSON implementation, but its read
# symbol is replaced at compile time with an immediate cache adapter. Only the
# low-priority cache task may call the meter manager and block on Modbus.
for token in [
    "background acquisition cache started",
    "EM500_INSTANTANEOUS_PERIOD_MS 2000U",
    "EM500_ENERGY_PERIOD_MS 30000U",
    "EM500_SETUP_PERIOD_MS 300000U",
    "em500_cache_request",
    "em500_cache_read_registers",
    "last-good cached block immediately",
]:
    require(token in CACHE or token in ADAPTER or token in CACHE_HEADER,
            f"EM500 asynchronous cache missing: {token}")
# The cache task is created with the capability-aware API so its stack lives in
# PSRAM. It performs only Modbus/TCP acquisition and esp_timer reads - it never
# touches NVS, esp_partition or esp_flash - so its stack is never accessed while
# the flash cache is disabled, and keeping it out of internal RAM preserves the
# scarce internal DMA pool for the Product Core's control and safety tasks.
require("xTaskCreateWithCaps(cache_task" in CACHE,
        "EM500 cache task must be created with the capability-aware API")
require("MALLOC_CAP_SPIRAM" in CACHE,
        "EM500 cache task stack must be requested from PSRAM")
for forbidden in ("nvs_", "esp_partition_", "esp_flash_"):
    require(forbidden not in CACHE,
            f"EM500 cache task must not touch flash ({forbidden}): a PSRAM stack "
            "is unsafe while the cache is disabled")
require("return ESP_ERR_NO_MEM" in CACHE,
        "EM500 cache task creation failure must stay explicitly checked")

require("meter_manager_read_registers" in CACHE,
        "background cache must own the real serialized Modbus reads")
require("meter_manager_read_registers" not in ADAPTER,
        "HTTP cache adapter must never execute a direct meter read")
require("meter_manager_read_registers=em500_cache_read_pdu_registers" in CMAKE,
        "em500_api.c is not compile-routed through the cache adapter")
require('"em500_cache.c"' in CMAKE and '"em500_cache_adapter.c"' in CMAKE,
        "EM500 cache sources are not compiled")
require("em500_cache_init()" in SERVER,
        "EM500 cache is not initialized before the HTTP server")
require(SERVER.index("em500_cache_init()") < SERVER.index("httpd_start"),
        "EM500 cache must start before requests can reach the snapshot API")

require("em500_api_register(s_server)" in SERVER,
        "EM500 endpoint is not registered")
capacity = re.search(r"config\.max_uri_handlers\s*=\s*(\d+)", SERVER)
require(capacity is not None and int(capacity.group(1)) >= 21,
        "HTTP handler capacity must retain room for all meter/settings endpoints")
require('"em500_api.c"' in CMAKE and "modbus_tcp" in CMAKE,
        "EM500 source/dependency is missing from the component build")

print("EM500 snapshot is read-only, cache-backed, and free of HTTP-thread Modbus I/O")
