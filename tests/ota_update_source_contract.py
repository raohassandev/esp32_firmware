from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PARTITIONS = (ROOT / "partitions.csv").read_text(encoding="utf-8")
SDKCONFIG = (ROOT / "sdkconfig.defaults").read_text(encoding="utf-8")
MANAGER_H = (ROOT / "components/ota_manager/include/ota_manager.h").read_text(encoding="utf-8")
MANAGER = (ROOT / "components/ota_manager/ota_manager.c").read_text(encoding="utf-8")
APP_CORE = (ROOT / "components/app_core/app_core.c").read_text(encoding="utf-8")
APP_MAIN = (ROOT / "main/app_main.c").read_text(encoding="utf-8")
API = (ROOT / "components/web_server/ota_api.c").read_text(encoding="utf-8")
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")
WEB_CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
ASSETS = (ROOT / "components/web_server/web_assets.c").read_text(encoding="utf-8")
JS = (ROOT / "web/ota.js").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


for token in ["otadata", "ota_0", "ota_1", "0x300000"]:
    require(token in PARTITIONS, f"OTA partition table missing: {token}")
require("CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y" in SDKCONFIG,
        "first-boot rollback must be enabled")

for token in [
    "esp_ota_get_next_update_partition",
    "esp_ota_begin",
    "esp_ota_write",
    "esp_ota_end",
    "esp_ota_set_boot_partition",
    "esp_ota_mark_app_valid_cancel_rollback",
    "esp_ota_mark_app_invalid_rollback_and_reboot",
    "ESP_IMAGE_HEADER_MAGIC",
    "ESP_APP_DESC_MAGIC_WORD",
    "candidate.project_name",
    "candidate.secure_version < running->secure_version",
    "complete_image_size > partition->size",
    "s_upload_claimed",
    "OTA_MANAGER_PREFIX_BYTES",
    "ota_manager_schedule_boot_validation",
    "validation_scheduled",
]:
    require(token in MANAGER or token in MANAGER_H,
            f"OTA manager safeguard missing: {token}")
require("fill_partition_status(&snapshot)" in MANAGER,
        "partition metadata must be refreshed outside the status critical section")
require("refresh_partition_status_locked" not in MANAGER,
        "flash/partition APIs must not run inside an interrupt-disabled critical section")

for forbidden in ["nvs_flash_erase", "erase_flash", "erase-flash"]:
    require(forbidden not in MANAGER and forbidden not in API,
            f"OTA implementation must never invoke {forbidden}")

require("ota_manager_init()" in APP_CORE,
        "OTA manager must initialize during application startup")
require(APP_CORE.index("ota_manager_init()") < APP_CORE.index("network_manager_init()"),
        "OTA pending state must be available before mandatory network initialization")
require("ota_manager_running_pending_verify()" in APP_CORE,
        "degraded first OTA boot must not be accepted")
require("ota_manager_schedule_boot_validation(30000U)" in APP_CORE,
        "first OTA boot must survive a stabilization window")
require("ota_manager_rollback_pending_and_reboot()" in APP_MAIN,
        "mandatory first-boot failure must trigger rollback")

for token in [
    '"/api/ota/status"',
    '"/api/ota/upload"',
    '"/api/ota/reboot"',
    "application/octet-stream",
    "OTA_UPLOAD_CHUNK_BYTES 4096U",
    "OTA_UPLOAD_DEADLINE_MS 600000U",
    "control_engine_force_disable()",
    "OTA_SAFE_ZERO_TOLERANCE_KW",
    "ota_manager_validate_prefix",
    "ota_manager_begin",
    "ota_manager_write",
    "ota_manager_finish",
    "update_staged",
    '"automatic_reboot", false',
    '"nvs_erase_required", false',
]:
    require(token in API, f"OTA API safeguard missing: {token}")
require(API.index("ota_manager_validate_prefix") < API.index("ota_manager_begin"),
        "image identity must be validated before erasing the inactive OTA slot")
require(API.index("wait_for_safe_zero()") < API.index("ota_manager_begin"),
        "safe zero must be confirmed before OTA flash writing begins")
require("malloc(OTA_UPLOAD_CHUNK_BYTES)" in API,
        "OTA API must use one bounded streaming buffer")
require("malloc(image_size)" not in API and "malloc(request->content_len)" not in API,
        "OTA API must never buffer the complete firmware image in RAM")
require("esp_restart()" not in API[:API.index("static void delayed_restart_task")],
        "upload handler must never reboot automatically")

for token in [
    '"ota_api.c"',
    "ota_manager",
    'configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/ota.js"',
    'configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/ota.css"',
    '"${CMAKE_CURRENT_BINARY_DIR}/ota.js"',
    '"${CMAKE_CURRENT_BINARY_DIR}/ota.css"',
]:
    require(token in WEB_CMAKE, f"OTA build integration missing: {token}")
require("ota_api.c" not in WEB_CMAKE.split("if(NOT source STREQUAL", 1)[1].split("endif()", 1)[0],
        "OTA API must remain behind the Engineering registration gateway")

for token in [
    "web_assets_ota_css",
    "web_assets_ota_js",
    "ota_api_register(s_server)",
    "config.max_uri_handlers = 44",
]:
    require(token in SERVER, f"OTA server integration missing: {token}")
require("DECLARE_ASSET(ota_css)" in ASSETS and "DECLARE_ASSET(ota_js)" in ASSETS,
        "OTA UI assets are not embedded")

for token in [
    "XMLHttpRequest",
    "xhr.upload.onprogress",
    "xhr.withCredentials = true",
    "xhr.timeout = 600000",
    "application/octet-stream",
    "window.confirm",
    "The controller will not reboot automatically",
    "NVS",
    "document.hidden",
    "beforeunload",
    "AbortController",
    "/api/ota/status",
    "/api/ota/upload",
    "/api/ota/reboot",
]:
    require(token in JS, f"OTA browser safety/lifecycle contract missing: {token}")
require("xhr.onload" in JS and "xhr.onerror" in JS and "xhr.ontimeout" in JS,
        "OTA uploader must handle completion, connection failure and timeout")
require("setInterval(" not in JS,
        "OTA status polling must be route-aware and timeout-based")

print("Rollback-safe authenticated OTA source contract passed")
