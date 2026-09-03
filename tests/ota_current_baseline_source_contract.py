#!/usr/bin/env python3
"""Current-dev rollback-safe secure OTA integration and safety contract."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PARTITIONS = (ROOT / "partitions.csv").read_text(encoding="utf-8")
SDKCONFIG = (ROOT / "sdkconfig.defaults").read_text(encoding="utf-8")
MANAGER_H = (ROOT / "components/ota_manager/include/ota_manager.h").read_text(encoding="utf-8")
MANAGER = (ROOT / "components/ota_manager/ota_manager.c").read_text(encoding="utf-8")
API = (ROOT / "components/web_server/ota_api.c").read_text(encoding="utf-8")
WEB_CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
ASSETS_H = (ROOT / "components/web_server/include/web_assets.h").read_text(encoding="utf-8")
ASSETS_C = (ROOT / "components/web_server/web_assets.c").read_text(encoding="utf-8")
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")
APP_CORE_CMAKE = (ROOT / "components/app_core/CMakeLists.txt").read_text(encoding="utf-8")
APP_CORE = (ROOT / "components/app_core/app_core.c").read_text(encoding="utf-8")
MAIN_CMAKE = (ROOT / "main/CMakeLists.txt").read_text(encoding="utf-8")
APP_MAIN = (ROOT / "main/app_main.c").read_text(encoding="utf-8")
JS = (ROOT / "web/ota.js").read_text(encoding="utf-8")
CSS = (ROOT / "web/ota.css").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening:index + 1]
    raise AssertionError(f"unterminated function: {signature}")


for token in ("otadata", "ota_0", "ota_1", "0x300000"):
    require(token in PARTITIONS, f"OTA partition layout missing {token}")
require("CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y" in SDKCONFIG,
        "bootloader rollback is not enabled")
require('#define OTA_MANAGER_PRODUCT_ID "automatrix_pvdg"' in MANAGER_H,
        "OTA product identity is not pinned")

for token in (
    "ESP_IMAGE_HEADER_MAGIC",
    "ESP_APP_DESC_MAGIC_WORD",
    "CONFIG_IDF_FIRMWARE_CHIP_ID",
    "candidate->secure_version >= running->secure_version",
    "esp_ota_get_next_update_partition",
    "esp_ota_begin",
    "esp_ota_write",
    "esp_ota_end",
    "esp_ota_get_partition_description",
    "esp_ota_set_boot_partition",
    "esp_ota_mark_app_valid_cancel_rollback",
    "esp_ota_mark_app_invalid_rollback_and_reboot",
    "boot_partition_preserved_after_abort",
    "image_identity_verified",
    "image_validated",
):
    require(token in MANAGER or token in MANAGER_H,
            f"OTA manager safeguard missing: {token}")

validate = function_body(MANAGER, "esp_err_t ota_manager_validate_prefix")
begin = function_body(MANAGER, "esp_err_t ota_manager_begin")
finish = function_body(MANAGER, "esp_err_t ota_manager_finish")
abort = function_body(MANAGER, "void ota_manager_abort")
upload = function_body(API, "static esp_err_t upload_handler")
reboot = function_body(API, "static esp_err_t reboot_handler")

require(validate.index("image_identity_valid") < validate.index("*out_candidate = candidate"),
        "prefix identity validation ordering regressed")
require(begin.index("image_identity_valid") < begin.index("esp_ota_begin"),
        "esp_ota_begin can run before identity recheck")
require("esp_ota_write" not in validate and "esp_ota_write" not in begin,
        "identity/admission path writes firmware bytes")
require(finish.index("esp_ota_end") < finish.index("esp_ota_get_partition_description") <
        finish.index("session->image_validated = true") < finish.index("esp_ota_set_boot_partition"),
        "boot selection occurs before complete image/descriptor validation")
require("esp_ota_set_boot_partition" not in abort,
        "abort path can change boot partition")
require("boot_before->address == boot_after->address" in abort,
        "abort path does not verify original boot partition")

for forbidden in ("nvs_flash_erase", "erase_flash", "erase-flash"):
    require(forbidden not in MANAGER and forbidden not in API and forbidden not in APP_CORE and forbidden not in APP_MAIN,
            f"destructive erase token present: {forbidden}")

require(upload.index("ota_manager_validate_prefix") < upload.index("wait_for_safe_zero()") <
        upload.index("ota_manager_begin") < upload.index("ota_manager_write"),
        "identity + safe-zero do not precede first OTA write")
for token in (
    "control_engine_force_disable()",
    "OTA_SAFE_ZERO_TOLERANCE_KW",
    "OTA_UPLOAD_CHUNK_BYTES 4096U",
    "OTA_UPLOAD_DEADLINE_MS 600000U",
    "application/octet-stream",
    "ota_manager_abort",
):
    require(token in API, f"OTA upload safeguard missing: {token}")
require("malloc(OTA_UPLOAD_CHUNK_BYTES)" in API,
        "OTA upload is not bounded to the streaming buffer")
require("malloc(image_size)" not in API and "malloc(request->content_len)" not in API,
        "OTA implementation buffers the complete image")
require("update_staged" in reboot and "image_identity_verified" in reboot and
        "image_validated" in reboot,
        "reboot endpoint is not gated by fully validated staged state")

require("ota_manager" in APP_CORE_CMAKE,
        "app_core does not depend on OTA manager")
require("ota_manager_init()" in APP_CORE and
        APP_CORE.index("ota_manager_init()") < APP_CORE.index("network_manager_init()"),
        "OTA state is not initialized before mandatory network startup")
require("ota_manager_running_pending_verify()" in APP_CORE,
        "degraded first boot can be accepted")
require("ota_manager_schedule_boot_validation(30000U)" in APP_CORE,
        "first-boot stabilization window is missing")
require("ota_manager" in MAIN_CMAKE,
        "bootstrap component cannot call OTA rollback manager")
require("ota_manager_rollback_pending_and_reboot()" in APP_MAIN,
        "mandatory startup failure cannot trigger OTA rollback")

for endpoint in ('"/api/ota/status"', '"/api/ota/upload"', '"/api/ota/reboot"'):
    require(endpoint in API, f"OTA endpoint missing: {endpoint}")
require('"ota_api.c"' in WEB_CMAKE and "ota_manager" in WEB_CMAKE,
        "OTA API/component is not in current web build")
# Current web build force-includes Engineering auth on every source except the
# two explicit public-side implementations below. OTA must not join that exemption.
require('source STREQUAL "engineering_guard.c"' in WEB_CMAKE and
        'source STREQUAL "operational_api.c"' in WEB_CMAKE,
        "Engineering gateway compile policy changed")
require('source STREQUAL "ota_api.c"' not in WEB_CMAKE,
        "OTA API was exempted from Engineering authentication")
require("ota_api_register(s_server)" in SERVER,
        "OTA API is not registered by the current server")

for token in (
    'configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/ota.js"',
    'configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/ota.css"',
    '"${CMAKE_CURRENT_BINARY_DIR}/ota.js"',
    '"${CMAKE_CURRENT_BINARY_DIR}/ota.css"',
):
    require(token in WEB_CMAKE, f"current composite bundle is missing OTA asset: {token}")
for getter in ("web_assets_ota_js", "web_assets_ota_css"):
    require(getter in ASSETS_H and getter in ASSETS_C and getter in SERVER,
            f"current bundle does not expose/serve {getter}")
require(SERVER.index("web_assets_ota_js") > SERVER.index("web_assets_commissioning_release_v3_js"),
        "OTA browser module must follow the existing product/system modules")
require(SERVER.index("web_assets_ota_css") > SERVER.index("web_assets_commissioning_release_v3_css"),
        "OTA CSS must follow the existing product/system styles")
require("var(--accent, #" not in CSS,
        "OTA CSS reintroduced a literal theme fallback")

for token in (
    "XMLHttpRequest",
    "xhr.upload.onprogress",
    "xhr.withCredentials = true",
    "application/octet-stream",
    "/api/ota/status",
    "/api/ota/upload",
    "/api/ota/reboot",
    "window.confirm",
    "The controller will not reboot automatically",
    "document.hidden",
):
    require(token in JS, f"OTA browser lifecycle safeguard missing: {token}")
require("setInterval(" not in JS, "OTA UI added unconditional interval polling")
require("NVS will be preserved" in JS,
        "OTA UI no longer tells the operator that commissioned NVS is preserved")

print("Current-dev rollback-safe secure OTA contract passed")
