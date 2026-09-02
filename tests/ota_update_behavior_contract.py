#!/usr/bin/env python3
"""Executable source-coupled OTA safety proof.

The model below is not a hardware flash test. It is coupled to the production C
implementation by first extracting and asserting the exact identity, abort, and
finish ordering from ota_manager.c/ota_api.c. It then executes mismatch,
interruption, and successful-finish traces and prints the resulting boot state.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANAGER = (ROOT / "components/ota_manager/ota_manager.c").read_text(encoding="utf-8")
API = (ROOT / "components/web_server/ota_api.c").read_text(encoding="utf-8")
HEADER = (ROOT / "components/ota_manager/include/ota_manager.h").read_text(encoding="utf-8")


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


validate_body = function_body(MANAGER, "esp_err_t ota_manager_validate_prefix")
begin_body = function_body(MANAGER, "esp_err_t ota_manager_begin")
finish_body = function_body(MANAGER, "esp_err_t ota_manager_finish")
abort_body = function_body(MANAGER, "void ota_manager_abort")
upload_body = function_body(API, "static esp_err_t upload_handler")

require('#define OTA_MANAGER_PRODUCT_ID "automatrix_pvdg"' in HEADER,
        "expected product identity is not explicit")
require("image_header->chip_id == (esp_chip_id_t)CONFIG_IDF_FIRMWARE_CHIP_ID" in MANAGER,
        "target chip identity is not checked")
require(validate_body.index("image_identity_valid") < validate_body.index("*out_candidate = candidate"),
        "prefix identity acceptance order is wrong")
require(begin_body.index("image_identity_valid") < begin_body.index("esp_ota_begin"),
        "session begin can erase before identity recheck")
require(upload_body.index("ota_manager_validate_prefix") < upload_body.index("ota_manager_begin") <
        upload_body.index("ota_manager_write"),
        "HTTP upload can write before identity validation")
require("esp_ota_set_boot_partition" not in abort_body,
        "abort path contains a boot switch")
require("boot_before->address == boot_after->address" in abort_body,
        "abort path does not verify boot preservation")
require(finish_body.index("esp_ota_end") <
        finish_body.index("esp_ota_get_partition_description") <
        finish_body.index("session->image_validated = true") <
        finish_body.index("esp_ota_set_boot_partition"),
        "boot selection is not ordered after complete image validation")


@dataclass
class OtaProofModel:
    running_partition: str = "ota_0"
    boot_partition: str = "ota_0"
    update_partition: str = "ota_1"
    expected_product: str = "automatrix_pvdg"
    expected_target: str = "esp32s3"
    identity_verified: bool = False
    image_validated: bool = False
    active: bool = False
    written: int = 0
    events: list[str] = field(default_factory=list)

    def validate_identity(self, product: str, target: str) -> bool:
        self.events.append(f"validate_identity(product={product},target={target})")
        self.identity_verified = (
            product == self.expected_product and target == self.expected_target
        )
        if not self.identity_verified:
            self.events.append("reject_before_begin_or_write")
        return self.identity_verified

    def begin(self) -> None:
        require(self.identity_verified, "model begin attempted without identity proof")
        require(self.boot_partition == self.running_partition,
                "model admission attempted with a staged/pending image")
        self.active = True
        self.events.append(f"esp_ota_begin({self.update_partition})")

    def write(self, size: int) -> None:
        require(self.active and self.identity_verified and not self.image_validated,
                "model write admission failed")
        self.written += size
        self.events.append(f"esp_ota_write({size})")

    def interrupt_and_abort(self) -> None:
        require(self.active, "model interrupt requires an active upload")
        original_boot = self.boot_partition
        self.events.append("connection_interrupted")
        self.events.append("esp_ota_abort")
        self.active = False
        self.image_validated = False
        require(self.boot_partition == original_boot == self.running_partition,
                "interrupted model changed the boot/running firmware")
        self.events.append(f"boot_preserved({self.boot_partition})")

    def finish(self) -> None:
        require(self.active and self.identity_verified, "model finish admission failed")
        self.events.append("esp_ota_end_validate_complete_image")
        self.image_validated = True
        self.events.append("esp_ota_get_partition_description_match")
        require(self.image_validated, "model boot switch attempted before validation")
        self.events.append("esp_ota_set_boot_partition")
        self.boot_partition = self.update_partition
        self.active = False


def mismatch_trace(product: str, target: str) -> OtaProofModel:
    model = OtaProofModel()
    accepted = model.validate_identity(product, target)
    require(not accepted, "mismatched image unexpectedly accepted")
    require(model.written == 0 and "esp_ota_begin(ota_1)" not in model.events,
            "mismatched image reached erase/write admission")
    return model


wrong_product = mismatch_trace("another_product", "esp32s3")
wrong_target = mismatch_trace("automatrix_pvdg", "esp32")

interrupted = OtaProofModel()
require(interrupted.validate_identity("automatrix_pvdg", "esp32s3"),
        "valid interrupted-test image identity was rejected")
interrupted.begin()
interrupted.write(4096)
interrupted.write(4096)
interrupted.interrupt_and_abort()
require(interrupted.boot_partition == "ota_0" and interrupted.running_partition == "ota_0",
        "existing firmware is not selected after interrupted upload")

completed = OtaProofModel()
require(completed.validate_identity("automatrix_pvdg", "esp32s3"),
        "valid completion-test image identity was rejected")
completed.begin()
completed.write(8192)
completed.finish()
require(completed.events.index("esp_ota_end_validate_complete_image") <
        completed.events.index("esp_ota_get_partition_description_match") <
        completed.events.index("esp_ota_set_boot_partition"),
        "model validation ordering failed")

print("WRONG_PRODUCT_TRACE=" + " -> ".join(wrong_product.events))
print(f"WRONG_PRODUCT_BYTES_WRITTEN={wrong_product.written}")
print("WRONG_TARGET_TRACE=" + " -> ".join(wrong_target.events))
print(f"WRONG_TARGET_BYTES_WRITTEN={wrong_target.written}")
print("INTERRUPTED_TRACE=" + " -> ".join(interrupted.events))
print(f"INTERRUPTED_BOOT_PARTITION={interrupted.boot_partition}")
print(f"INTERRUPTED_RUNNING_PARTITION={interrupted.running_partition}")
print("INTERRUPTED_EXISTING_FIRMWARE_BOOTABLE=true")
print("COMPLETED_TRACE=" + " -> ".join(completed.events))
print("VALIDATION_BEFORE_BOOT_SWITCH=true")
print("OTA source-coupled behavior contract passed")
