from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
API = (ROOT / "components/web_server/em500_history_api.c").read_text(encoding="utf-8")
JOBS = (ROOT / "components/web_server/meter_read_jobs.c").read_text(encoding="utf-8")
JOBS_HEADER = (ROOT / "components/web_server/include/meter_read_jobs.h").read_text(encoding="utf-8")
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
        "history decoder source must retain its stable read abstraction")
require("modbus_tcp_write" not in API,
        "history endpoint must not perform Modbus writes")

for token in [
    "METER_READ_JOB_SLOTS 20U",
    "METER_READ_JOB_MAX_REGISTERS 125U",
    "METER_READ_JOB_FRESH_MS 5000U",
    "claim_pending_job",
    "read_job_task",
    "xTaskCreate(read_job_task",
    "meter_read_jobs_cached_read",
    "returns ESP_ERR_INVALID_STATE",
]:
    require(token in JOBS or token in JOBS_HEADER,
            f"asynchronous meter read-job queue missing: {token}")
require("meter_manager_read_registers" in JOBS,
        "only the background read-job task may own the real meter read")
require('"meter_read_jobs.c"' in CMAKE,
        "meter read-job source is not compiled")
require("meter_manager_read_registers=meter_read_jobs_cached_read" in CMAKE,
        "history/settings sources are not compile-routed through background jobs")
for source in ("em500_history_api.c", "em500_settings_api.c", "em500_settings_plan_api.c"):
    require(f'"{source}"' in CMAKE, f"{source} is missing from the component")
require("meter_read_jobs_init()" in SERVER,
        "meter read-job queue is not initialized")
require(SERVER.index("meter_read_jobs_init()") < SERVER.index("httpd_start"),
        "read-job queue must start before HTTP handlers")

require("em500_history_api_register(s_server)" in SERVER,
        "history endpoint is not registered")
capacity = re.search(r"config\.max_uri_handlers\s*=\s*(\d+)", SERVER)
require(capacity is not None and int(capacity.group(1)) >= 33,
        "HTTP handler capacity must retain room for cache/status endpoints")

print("EM500 history, settings, and planning reads are asynchronous and bounded")
