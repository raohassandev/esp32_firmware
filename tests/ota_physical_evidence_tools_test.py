#!/usr/bin/env python3
from __future__ import annotations

import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))

from ota_physical_evidence_verify import REQUIRED_SCENARIOS, evaluate


SOURCE = "release-source-sha"
TREE = "release-tree-sha"
ARTIFACT = "sha256:release-package"
APP = "sha256:release-app"
CONFIG = "config-sha256:release"
PREVIOUS = "ota_0"
TARGET = "ota_1"


def base_scenario(scenario_id: str) -> dict:
    result = {
        "id": scenario_id,
        "outcome": "pass",
        "started_at": "2026-09-03T12:00:00+00:00",
        "ended_at": "2026-09-03T12:02:00+00:00",
        "running_partition_before": PREVIOUS,
        "running_partition_after": PREVIOUS,
        "boot_partition_before": PREVIOUS,
        "boot_partition_after": PREVIOUS,
        "lifecycle_before": "idle",
        "lifecycle_after": "idle",
        "expected_control_state": "blocked",
        "observed_control_state": "blocked",
        "control_evidence_ref": "control-capture.json",
        "fatal_counts": {
            "wdt": 0,
            "panic": 0,
            "no_mem": 0,
            "unexpected_reset": 0,
            "resource_collapse": 0
        },
        "reboot": {"count": 0, "reason": "no reboot expected"},
        "references": {"serial_log_ref": "serial.log", "api_hmi_ref": "api-capture.json"},
        "evidence_note": f"physical evidence for {scenario_id} matched the expected OTA safety behavior"
    }

    if scenario_id == "authenticated_upload":
        result.update({
            "engineering_authenticated": True,
            "upload_accepted": True,
            "staged_app_digest": APP,
            "boot_partition_after": TARGET,
            "lifecycle_after": "staged"
        })
    elif scenario_id == "invalid_image_rejection":
        result.update({
            "engineering_authenticated": True,
            "upload_rejected": True,
            "rejected_before_boot_selection": True,
            "partial_or_invalid_selected": False
        })
    elif scenario_id == "interrupted_upload":
        result.update({
            "transport_interrupted": True,
            "upload_completed": False,
            "partial_or_invalid_selected": False
        })
    elif scenario_id == "power_loss_during_update":
        result.update({
            "controlled_power_loss": True,
            "partial_or_invalid_selected": False,
            "nvs_before": {"sentinel": 7, "mode": "safe"},
            "nvs_after": {"sentinel": 7, "mode": "safe"},
            "reboot": {"count": 1, "reason": "controlled bench power interruption"}
        })
    elif scenario_id == "partial_image_not_selected":
        result.update({
            "partial_image_present": True,
            "partial_or_invalid_selected": False
        })
    elif scenario_id == "previous_slot_boot":
        result.update({
            "previous_slot_boot_proven": True,
            "lifecycle_after": "previous_slot",
            "nvs_before": {"sentinel": 7},
            "nvs_after": {"sentinel": 7},
            "reboot": {"count": 1, "reason": "booted previous valid slot"}
        })
    elif scenario_id == "staged_explicit_reboot":
        result.update({
            "staged_image_validated": True,
            "explicit_authenticated_reboot": True,
            "running_partition_after": TARGET,
            "boot_partition_after": TARGET,
            "lifecycle_before": "staged",
            "lifecycle_after": "pending_verification",
            "nvs_before": {"sentinel": 7},
            "nvs_after": {"sentinel": 7},
            "reboot": {"count": 1, "reason": "explicit authenticated staged-image reboot"}
        })
    elif scenario_id == "pending_verification_first_boot":
        result.update({
            "running_partition_before": TARGET,
            "running_partition_after": TARGET,
            "boot_partition_before": TARGET,
            "boot_partition_after": TARGET,
            "lifecycle_before": "pending_verification",
            "lifecycle_after": "pending_verification",
            "marked_valid": False,
            "nvs_before": {"sentinel": 7},
            "nvs_after": {"sentinel": 7},
            "reboot": {"count": 1, "reason": "first boot into pending-verification image"}
        })
    elif scenario_id == "mark_valid_stabilization":
        result.update({
            "running_partition_before": TARGET,
            "running_partition_after": TARGET,
            "boot_partition_before": TARGET,
            "boot_partition_after": TARGET,
            "lifecycle_before": "pending_verification",
            "lifecycle_after": "valid",
            "stabilization_seconds": 30.0,
            "marked_valid": True,
            "nvs_before": {"sentinel": 7},
            "nvs_after": {"sentinel": 7}
        })
    elif scenario_id == "deliberate_rollback":
        result.update({
            "running_partition_before": TARGET,
            "running_partition_after": PREVIOUS,
            "boot_partition_before": TARGET,
            "boot_partition_after": PREVIOUS,
            "lifecycle_before": "pending_verification",
            "lifecycle_after": "previous_slot",
            "rollback_triggered": True,
            "previous_slot_recovered": True,
            "nvs_before": {"sentinel": 7},
            "nvs_after": {"sentinel": 7},
            "reboot": {"count": 1, "reason": "deliberate rollback to previous valid slot"}
        })
    elif scenario_id == "fail_closed_control":
        result.update({
            "uncertainty_states_exercised": True,
            "unsafe_command_observed": False
        })
    elif scenario_id == "nvs_persistence":
        result.update({
            "nvs_before": {"sentinel": 7, "settings": {"mode": "safe"}},
            "nvs_after": {"sentinel": 7, "settings": {"mode": "safe"}},
            "nvs_erase_used": False,
            "full_flash_erase_used": False,
            "authoritative_values_restored": True,
            "reboot": {"count": 1, "reason": "persistence readback after OTA reboot"}
        })
    return result


def passing_record() -> dict:
    return {
        "release_source_sha": SOURCE,
        "release_tree_sha": TREE,
        "release_artifact_digest": ARTIFACT,
        "release_app_digest": APP,
        "config_identity": CONFIG,
        "previous_valid_partition": PREVIOUS,
        "target_partition": TARGET,
        "partition_table_identity": "partition-table-sha256:abc",
        "bootloader_rollback_enabled": True,
        "engineering_auth_ref": "engineering-auth-session-capture",
        "fresh_ci_ref": "exact-head-ci-run",
        "package_identity_ref": "immutable-package-verification",
        "scenarios": [base_scenario(item) for item in REQUIRED_SCENARIOS]
    }


class OtaPhysicalEvidenceTests(unittest.TestCase):
    def test_complete_record_passes_validator_logic(self) -> None:
        self.assertTrue(evaluate(passing_record(), SOURCE, TREE, ARTIFACT, APP, CONFIG).passed)

    def test_wrong_release_identity_fails(self) -> None:
        result = evaluate(passing_record(), "wrong", TREE, ARTIFACT, APP, CONFIG)
        self.assertFalse(result.passed)
        self.assertIn("release_source_sha_mismatch", result.failures)

    def test_invalid_image_cannot_change_boot_target(self) -> None:
        record = passing_record()
        item = next(x for x in record["scenarios"] if x["id"] == "invalid_image_rejection")
        item["boot_partition_after"] = TARGET
        item["partial_or_invalid_selected"] = True
        result = evaluate(record, SOURCE, TREE, ARTIFACT, APP, CONFIG)
        self.assertFalse(result.passed)
        self.assertIn("scenario:invalid_image_rejection:incomplete_or_invalid_image_selected", result.failures)
        self.assertIn("scenario:invalid_image_rejection:partial_or_invalid_selected_not_false", result.failures)

    def test_interrupted_upload_cannot_be_marked_complete(self) -> None:
        record = passing_record()
        item = next(x for x in record["scenarios"] if x["id"] == "interrupted_upload")
        item["upload_completed"] = True
        result = evaluate(record, SOURCE, TREE, ARTIFACT, APP, CONFIG)
        self.assertFalse(result.passed)
        self.assertIn("scenario:interrupted_upload:interrupted_upload_marked_complete", result.failures)

    def test_power_loss_must_recover_previous_valid_partition_and_nvs(self) -> None:
        record = passing_record()
        item = next(x for x in record["scenarios"] if x["id"] == "power_loss_during_update")
        item["running_partition_after"] = TARGET
        item["nvs_after"] = {"sentinel": 999}
        result = evaluate(record, SOURCE, TREE, ARTIFACT, APP, CONFIG)
        self.assertFalse(result.passed)
        self.assertIn("scenario:power_loss_during_update:previous_valid_partition_not_running_after_power_loss", result.failures)
        self.assertIn("scenario:power_loss_during_update:nvs_continuity_failed", result.failures)

    def test_pending_first_boot_cannot_be_prematurely_valid(self) -> None:
        record = passing_record()
        item = next(x for x in record["scenarios"] if x["id"] == "pending_verification_first_boot")
        item["lifecycle_after"] = "valid"
        item["marked_valid"] = True
        result = evaluate(record, SOURCE, TREE, ARTIFACT, APP, CONFIG)
        self.assertFalse(result.passed)
        self.assertIn("scenario:pending_verification_first_boot:pending_verification_not_observed", result.failures)
        self.assertIn("scenario:pending_verification_first_boot:first_boot_prematurely_marked_valid", result.failures)

    def test_mark_valid_cannot_shorten_30_second_stabilization(self) -> None:
        record = passing_record()
        item = next(x for x in record["scenarios"] if x["id"] == "mark_valid_stabilization")
        item["stabilization_seconds"] = 29.999
        result = evaluate(record, SOURCE, TREE, ARTIFACT, APP, CONFIG)
        self.assertFalse(result.passed)
        self.assertIn("scenario:mark_valid_stabilization:stabilization_too_short", result.failures)

    def test_deliberate_rollback_must_restore_previous_slot(self) -> None:
        record = passing_record()
        item = next(x for x in record["scenarios"] if x["id"] == "deliberate_rollback")
        item["running_partition_after"] = TARGET
        item["previous_slot_recovered"] = False
        result = evaluate(record, SOURCE, TREE, ARTIFACT, APP, CONFIG)
        self.assertFalse(result.passed)
        self.assertIn("scenario:deliberate_rollback:previous_slot_recovery_not_proven", result.failures)
        self.assertIn("scenario:deliberate_rollback:rollback_did_not_restore_previous_partition", result.failures)

    def test_control_must_remain_blocked(self) -> None:
        record = passing_record()
        item = next(x for x in record["scenarios"] if x["id"] == "authenticated_upload")
        item["observed_control_state"] = "allowed"
        result = evaluate(record, SOURCE, TREE, ARTIFACT, APP, CONFIG)
        self.assertFalse(result.passed)
        self.assertIn("scenario:authenticated_upload:observed_control_not_blocked", result.failures)

    def test_nvs_or_full_flash_erase_is_failure(self) -> None:
        record = passing_record()
        item = next(x for x in record["scenarios"] if x["id"] == "nvs_persistence")
        item["nvs_erase_used"] = True
        item["full_flash_erase_used"] = True
        result = evaluate(record, SOURCE, TREE, ARTIFACT, APP, CONFIG)
        self.assertFalse(result.passed)
        self.assertIn("scenario:nvs_persistence:nvs_erase_not_explicitly_absent", result.failures)
        self.assertIn("scenario:nvs_persistence:full_flash_erase_not_explicitly_absent", result.failures)

    def test_missing_scenario_fails(self) -> None:
        record = passing_record()
        record["scenarios"] = [x for x in record["scenarios"] if x["id"] != "previous_slot_boot"]
        result = evaluate(record, SOURCE, TREE, ARTIFACT, APP, CONFIG)
        self.assertFalse(result.passed)
        self.assertIn("scenario_missing:previous_slot_boot", result.failures)


if __name__ == "__main__":
    unittest.main(verbosity=2)
