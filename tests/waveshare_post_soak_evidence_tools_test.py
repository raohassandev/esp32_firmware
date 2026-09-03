#!/usr/bin/env python3
from __future__ import annotations

import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))

from waveshare_backend_parity_verify import REQUIRED_CHECKS as BACKEND_CHECKS
from waveshare_backend_parity_verify import evaluate as evaluate_backend
from waveshare_persistence_arm_verify import REQUIRED_CHECKS as PERSISTENCE_CHECKS
from waveshare_persistence_arm_verify import evaluate as evaluate_persistence


CANDIDATE = "87841ecee727fe1d814d4186be8c8c26e4afafb4"
DIGEST = "sha256:89e621034d4c91096fc5d38dd57ac40eeeab34275e4af1fc0461b48575039096"


def passing_backend() -> dict:
    return {
        "candidate_sha": CANDIDATE,
        "artifact_digest": DIGEST,
        "final_soak_passed": True,
        "started_at": "2026-09-03T10:00:00+00:00",
        "ended_at": "2026-09-03T10:35:00+00:00",
        "observation_minutes": 30,
        "checks": {key: True for key in BACKEND_CHECKS},
        "evidence": {key: f"observed evidence for {key}" for key in BACKEND_CHECKS},
        "references": {
            "serial_log_ref": "waveshare_backend_serial.log",
            "hmi_http_capture_ref": "backend_parity_capture.json",
        },
    }


def passing_persistence() -> dict:
    return {
        "candidate_sha": CANDIDATE,
        "artifact_digest": DIGEST,
        "final_soak_passed": True,
        "checks": {key: True for key in PERSISTENCE_CHECKS},
        "evidence": {key: f"observed evidence for {key}" for key in PERSISTENCE_CHECKS},
        "steps": [
            {"operation": "save_readback", "observed_at": "2026-09-03T11:00:00Z", "before": {"x": 1}, "after": {"x": 2}},
            {"operation": "reboot_restore", "observed_at": "2026-09-03T11:05:00Z", "before": {"x": 2}, "after": {"x": 2}},
            {"operation": "interrupted_save", "observed_at": "2026-09-03T11:10:00Z", "before": {"armed": False}, "after": {"armed": False}},
            {"operation": "arm_gate", "observed_at": "2026-09-03T11:15:00Z", "before": {"qualified": False}, "after": {"armed": False}},
        ],
        "references": {
            "serial_log_ref": "waveshare_persistence_serial.log",
            "persistence_capture_ref": "persistence_matrix.json",
        },
    }


class BackendParityTests(unittest.TestCase):
    def test_complete_record_passes(self) -> None:
        self.assertTrue(evaluate_backend(passing_backend(), CANDIDATE, DIGEST).passed)

    def test_final_soak_is_hard_prerequisite(self) -> None:
        record = passing_backend()
        record["final_soak_passed"] = False
        result = evaluate_backend(record, CANDIDATE, DIGEST)
        self.assertFalse(result.passed)
        self.assertIn("final_soak_not_passed", result.failures)

    def test_wrong_identity_fails(self) -> None:
        result = evaluate_backend(passing_backend(), "wrong", DIGEST)
        self.assertFalse(result.passed)
        self.assertIn("candidate_sha_mismatch", result.failures)

    def test_short_or_unsubstantiated_observation_fails(self) -> None:
        record = passing_backend()
        record["observation_minutes"] = 10
        record["checks"]["network_recovery_clean"] = False
        record["evidence"]["network_recovery_clean"] = ""
        result = evaluate_backend(record, CANDIDATE, DIGEST)
        self.assertFalse(result.passed)
        self.assertTrue(any(item.startswith("observation_minutes=") for item in result.failures))
        self.assertIn("check_not_passed:network_recovery_clean", result.failures)
        self.assertIn("evidence_missing:network_recovery_clean", result.failures)

    def test_cli_threshold_cannot_lower_release_minimum(self) -> None:
        record = passing_backend()
        record["observation_minutes"] = 20
        result = evaluate_backend(record, CANDIDATE, DIGEST, min_observation_minutes=1)
        self.assertFalse(result.passed)
        self.assertIn("observation_minutes=20<30", result.failures)

    def test_mixed_timezone_timestamps_fail_instead_of_crashing(self) -> None:
        record = passing_backend()
        record["ended_at"] = "2026-09-03T10:35:00"
        result = evaluate_backend(record, CANDIDATE, DIGEST)
        self.assertFalse(result.passed)
        self.assertIn("timestamp_timezone_mismatch", result.failures)


class PersistenceArmTests(unittest.TestCase):
    def test_complete_record_passes(self) -> None:
        self.assertTrue(evaluate_persistence(passing_persistence(), CANDIDATE, DIGEST).passed)

    def test_final_soak_is_hard_prerequisite(self) -> None:
        record = passing_persistence()
        record["final_soak_passed"] = False
        result = evaluate_persistence(record, CANDIDATE, DIGEST)
        self.assertFalse(result.passed)
        self.assertIn("final_soak_not_passed", result.failures)

    def test_missing_required_operation_fails(self) -> None:
        record = passing_persistence()
        record["steps"] = [step for step in record["steps"] if step["operation"] != "interrupted_save"]
        result = evaluate_persistence(record, CANDIDATE, DIGEST)
        self.assertFalse(result.passed)
        self.assertIn("operation_missing:interrupted_save", result.failures)

    def test_null_before_after_is_not_evidence(self) -> None:
        record = passing_persistence()
        record["steps"][0]["before"] = None
        record["steps"][0]["after"] = None
        result = evaluate_persistence(record, CANDIDATE, DIGEST)
        self.assertFalse(result.passed)
        self.assertIn("step_before_missing:0", result.failures)
        self.assertIn("step_after_missing:0", result.failures)

    def test_erase_or_arm_claim_must_be_explicitly_safe(self) -> None:
        record = passing_persistence()
        record["checks"]["nvs_erase_absent"] = False
        record["checks"]["arm_only_after_qualified"] = False
        result = evaluate_persistence(record, CANDIDATE, DIGEST)
        self.assertFalse(result.passed)
        self.assertIn("check_not_passed:nvs_erase_absent", result.failures)
        self.assertIn("check_not_passed:arm_only_after_qualified", result.failures)


if __name__ == "__main__":
    unittest.main(verbosity=2)
