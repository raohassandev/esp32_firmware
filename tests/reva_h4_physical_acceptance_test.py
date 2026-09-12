#!/usr/bin/env python3
from __future__ import annotations

import copy
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools import reva_h4_physical_acceptance_verify as h4  # noqa: E402


H2 = "a" * 40
PROVIDER = "sha256:" + "b" * 64
FIRMWARE = "c" * 40
FIRMWARE_DIGEST = "sha256:" + "d" * 64
DFM_DIGEST = "sha256:" + "e" * 64
PACKAGE_DIGEST = "sha256:" + "f" * 64


def pass_test(test_id: str) -> dict:
    return {
        "id": test_id,
        "status": "PASS",
        "started_at": "2026-09-12T10:00:00+05:00",
        "ended_at": "2026-09-12T10:01:00+05:00",
        "stimulus": "Apply the documented test stimulus for this H4 check.",
        "expected": "Expected safe physical behavior is explicitly defined.",
        "observed": "Observed safe physical behavior matched the expectation.",
        "measurements": {"sample": 1.0},
        "evidence_refs": [f"evidence/{test_id}.log"],
    }


def valid_record() -> dict:
    tests = []
    for test_id in h4.H4_TEST_IDS:
        if test_id == h4.OPTIONAL_FEATURE_TEST:
            tests.append(
                {
                    "id": test_id,
                    "status": "SKIPPED_NOT_POPULATED",
                    "populated_features": [],
                    "not_populated_features": ["DI", "RTC", "microSD", "RS232"],
                    "status_reason": "Optional features are not populated on this assembly variant.",
                    "started_at": "2026-09-12T10:02:00+05:00",
                    "ended_at": "2026-09-12T10:03:00+05:00",
                    "evidence_refs": ["evidence/assembly-population-photo.jpg"],
                }
            )
        elif test_id == h4.EXTERNAL_LAB_TEST:
            tests.append(
                {
                    "id": test_id,
                    "status": "DEFERRED_EXTERNAL_LAB",
                    "status_reason": "Certified environmental and EMC lab testing is tracked externally.",
                    "deferred_plan_ref": "LAB-PLAN-001",
                }
            )
        else:
            tests.append(pass_test(test_id))

    return {
        "evidence_state": "EXECUTED_PHYSICAL_EVIDENCE",
        "h2_checkpoint_sha": H2,
        "provider_artifact_ref": "artifact/10300571374",
        "provider_artifact_digest": PROVIDER,
        "fabricator": {
            "name": "Example PCB Fabricator",
            "dfm_accepted": True,
            "dfm_acceptance_ref": "DFM-ACCEPT-001",
            "dfm_acceptance_digest": DFM_DIGEST,
            "dfm_accepted_at": "2026-09-12T09:00:00+05:00",
            "accepted_minima_mm": {
                "drill": 0.20,
                "hole_clearance": 0.18,
                "copper_edge": 0.25,
            },
        },
        "board_identity": {
            "pcb_lot": "PCB-LOT-001",
            "pcba_lot": "PCBA-LOT-001",
            "board_serial": "REVA-0001",
            "bom_variant": "REVA-BOM-A",
            "assembly_variant": "REVA-ASM-A",
            "approved_substitutions": [],
        },
        "firmware": {
            "sha": FIRMWARE,
            "artifact_ref": "firmware/reva-0001.bin",
            "artifact_digest": FIRMWARE_DIGEST,
        },
        "bench": {
            "executor": "Authorized Engineer",
            "started_at": "2026-09-12T09:30:00+05:00",
            "ended_at": "2026-09-12T13:30:00+05:00",
            "supply_model": "BenchSupply-01",
            "current_limit_a": 2.0,
            "instrument_refs": ["DMM-001", "SCOPE-001"],
            "enclosure_revision": "ENC-REVA-01",
        },
        "runtime": {
            "fatal_counts": {
                "wdt": 0,
                "panic": 0,
                "no_mem": 0,
                "unexpected_reset": 0,
                "resource_collapse": 0,
            },
            "serial_log_refs": ["evidence/runtime.log"],
        },
        "tests": tests,
        "signoff": {
            "accepted": True,
            "authorized_by": "Hardware Acceptance Authority",
            "accepted_at": "2026-09-12T14:00:00+05:00",
            "evidence_package_ref": "H4-EVIDENCE-PKG-001",
            "evidence_package_digest": PACKAGE_DIGEST,
        },
    }


class H4PhysicalEvidenceTests(unittest.TestCase):
    def evaluate(self, record: dict, firmware_digest: str = FIRMWARE_DIGEST):
        return h4.evaluate(record, H2, PROVIDER, FIRMWARE, firmware_digest)

    def test_complete_evidence_passes(self):
        result = self.evaluate(valid_record())
        self.assertTrue(result.passed, result.failures)
        self.assertEqual(result.tests_seen, sorted(h4.H4_TEST_IDS))
        self.assertEqual(result.firmware_artifact_digest, FIRMWARE_DIGEST)

    def test_unexecuted_template_fails_closed(self):
        record = valid_record()
        record["evidence_state"] = "UNEXECUTED_TEMPLATE_NOT_A_PHYSICAL_PASS"
        result = self.evaluate(record)
        self.assertFalse(result.passed)
        self.assertIn("evidence_state_not_executed", result.failures)

    def test_identity_mismatch_fails(self):
        record = valid_record()
        record["h2_checkpoint_sha"] = "f" * 40
        record["provider_artifact_digest"] = "sha256:" + "e" * 64
        record["firmware"]["sha"] = "1" * 40
        result = self.evaluate(record)
        self.assertFalse(result.passed)
        self.assertIn("h2_checkpoint_sha_mismatch", result.failures)
        self.assertIn("provider_artifact_digest_mismatch", result.failures)
        self.assertIn("firmware_sha_mismatch", result.failures)

    def test_firmware_binary_digest_mismatch_fails(self):
        result = self.evaluate(valid_record(), "sha256:" + "0" * 64)
        self.assertFalse(result.passed)
        self.assertIn("firmware_artifact_digest_mismatch", result.failures)

    def test_fabricator_dfm_is_mandatory(self):
        record = valid_record()
        record["fabricator"]["dfm_accepted"] = False
        result = self.evaluate(record)
        self.assertFalse(result.passed)
        self.assertIn("fabricator_dfm_not_accepted", result.failures)

    def test_fabricator_dfm_document_digest_is_mandatory(self):
        record = valid_record()
        record["fabricator"]["dfm_acceptance_digest"] = ""
        result = self.evaluate(record)
        self.assertFalse(result.passed)
        self.assertIn("fabricator_dfm_acceptance_digest_invalid", result.failures)

    def test_fabricator_minimum_must_cover_h2_geometry(self):
        record = valid_record()
        record["fabricator"]["accepted_minima_mm"]["drill"] = 0.30
        result = self.evaluate(record)
        self.assertFalse(result.passed)
        self.assertIn("fabricator_minimum_does_not_accept_h2:drill", result.failures)

    def test_dfm_must_precede_bench_execution(self):
        record = valid_record()
        record["fabricator"]["dfm_accepted_at"] = "2026-09-12T10:30:00+05:00"
        result = self.evaluate(record)
        self.assertFalse(result.passed)
        self.assertIn("fabricator_dfm_accepted_after_bench_started", result.failures)

    def test_mandatory_test_cannot_be_skipped(self):
        record = valid_record()
        target = next(item for item in record["tests"] if item["id"] == "power_12v")
        target["status"] = "SKIPPED_NOT_POPULATED"
        result = self.evaluate(record)
        self.assertFalse(result.passed)
        self.assertIn("test:power_12v:mandatory_not_pass", result.failures)

    def test_pass_test_requires_stimulus(self):
        record = valid_record()
        target = next(item for item in record["tests"] if item["id"] == "power_12v")
        target["stimulus"] = ""
        result = self.evaluate(record)
        self.assertFalse(result.passed)
        self.assertIn("test:power_12v:stimulus_missing", result.failures)

    def test_pass_test_requires_nonempty_measurements(self):
        record = valid_record()
        target = next(item for item in record["tests"] if item["id"] == "power_12v")
        target["measurements"] = {}
        result = self.evaluate(record)
        self.assertFalse(result.passed)
        self.assertIn("test:power_12v:measurements_empty", result.failures)

    def test_bench_test_must_stay_inside_bench_window(self):
        record = valid_record()
        target = next(item for item in record["tests"] if item["id"] == "power_12v")
        target["started_at"] = "2026-09-12T09:00:00+05:00"
        result = self.evaluate(record)
        self.assertFalse(result.passed)
        self.assertIn("test:power_12v:started_before_bench", result.failures)

    def test_optional_skip_cannot_hide_populated_feature(self):
        record = valid_record()
        target = next(item for item in record["tests"] if item["id"] == h4.OPTIONAL_FEATURE_TEST)
        target["populated_features"] = ["RTC"]
        result = self.evaluate(record)
        self.assertFalse(result.passed)
        self.assertIn("test:optional_features:skip_with_populated_features", result.failures)

    def test_external_lab_deferral_requires_plan(self):
        record = valid_record()
        target = next(item for item in record["tests"] if item["id"] == h4.EXTERNAL_LAB_TEST)
        target["deferred_plan_ref"] = ""
        result = self.evaluate(record)
        self.assertFalse(result.passed)
        self.assertIn("test:industrial_environmental_emc:deferred_plan_ref_missing", result.failures)

    def test_external_lab_pass_requires_report_digest(self):
        record = valid_record()
        target = next(item for item in record["tests"] if item["id"] == h4.EXTERNAL_LAB_TEST)
        target.clear()
        target.update(pass_test(h4.EXTERNAL_LAB_TEST))
        target["lab_name"] = "Accredited Lab"
        target["report_ref"] = "REPORT-001"
        target["report_digest"] = ""
        result = self.evaluate(record)
        self.assertFalse(result.passed)
        self.assertIn("test:industrial_environmental_emc:report_digest_invalid", result.failures)

    def test_nonzero_runtime_fatal_counter_fails(self):
        record = valid_record()
        record["runtime"]["fatal_counts"]["panic"] = 1
        result = self.evaluate(record)
        self.assertFalse(result.passed)
        self.assertIn("runtime_fatal_count_nonzero_or_missing:panic", result.failures)

    def test_signoff_must_follow_testing(self):
        record = valid_record()
        record["signoff"]["accepted_at"] = "2026-09-12T13:00:00+05:00"
        result = self.evaluate(record)
        self.assertFalse(result.passed)
        self.assertIn("signoff_before_testing_completed", result.failures)

    def test_signoff_package_digest_is_mandatory(self):
        record = valid_record()
        record["signoff"]["evidence_package_digest"] = ""
        result = self.evaluate(record)
        self.assertFalse(result.passed)
        self.assertIn("signoff_evidence_package_digest_invalid", result.failures)

    def test_missing_test_fails(self):
        record = valid_record()
        record["tests"] = [item for item in record["tests"] if item["id"] != "rs485_b_tx_rx"]
        result = self.evaluate(record)
        self.assertFalse(result.passed)
        self.assertIn("test_missing:rs485_b_tx_rx", result.failures)


if __name__ == "__main__":
    unittest.main()
