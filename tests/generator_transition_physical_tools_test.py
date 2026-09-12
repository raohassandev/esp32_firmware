#!/usr/bin/env python3
from __future__ import annotations

import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))

from generator_transition_physical_verify import REQUIRED_SCENARIOS, evaluate

SHA = "a1620789235d21b515f9f245f2329fab88b50558"
DIGEST = "sha256:" + "a" * 64
SITE = "bench-A"
CONFIG = "config-sha256-exact-001"
TOPOLOGY_DIGEST = "sha256:" + "b" * 64
SOURCE_MAP_DIGEST = "sha256:" + "c" * 64
METER_MAP_DIGEST = "sha256:" + "d" * 64
PACKAGE_DIGEST = "sha256:" + "e" * 64
SCENARIO_DIGEST = "sha256:" + "f" * 64
RAW_DIGEST = "sha256:" + "1" * 64
RECOVERY_DIGEST = "sha256:" + "2" * 64
COMMAND_DIGEST = "sha256:" + "3" * 64


def meter(meter_id: str, role: str, sign: str) -> dict:
    return {
        "meter_id": meter_id,
        "role": role,
        "ct_pt_polarity_ref": f"{meter_id}-commissioning-polarity-sheet",
        "data_type": "S32",
        "word_order": "ABCD",
        "raw": 123,
        "scale": 0.1,
        "scaled_kw": 12.3,
        "fresh": True,
        "sign_provenance": sign,
    }


def identity_fields() -> dict:
    return {
        "firmware_sha": SHA,
        "artifact_digest": DIGEST,
        "site_id": SITE,
        "config_identity": CONFIG,
        "topology_digest": TOPOLOGY_DIGEST,
        "source_map_digest": SOURCE_MAP_DIGEST,
        "meter_map_digest": METER_MAP_DIGEST,
    }


def scenario(scenario_id: str) -> dict:
    blocked = scenario_id in {"stale", "conflict", "transfer_asserted", "source_loss"}
    transition = scenario_id in {"grid_to_generator", "generator_to_grid", "recovery_dwell"}
    expected = ["blocked"] if blocked else (["blocked", "allowed"] if transition else ["allowed"])
    result = {
        "id": scenario_id,
        **identity_fields(),
        "outcome": "pass",
        "started_at": "2026-09-03T12:00:00+00:00",
        "ended_at": "2026-09-03T12:05:00+00:00",
        "evidence_ref": f"physical-package/{scenario_id}",
        "evidence_digest": SCENARIO_DIGEST,
        "raw_source_evidence": {"grid_breaker": 1, "generator_run": 0, "transfer": 0},
        "raw_source_evidence_ref": f"raw-source/{scenario_id}",
        "raw_source_evidence_digest": RAW_DIGEST,
        "detected_modes": ["grid_only"],
        "expected_authority_sequence": expected,
        "observed_authority_sequence": list(expected),
        "grid_meter": meter("M-GRID", "grid", "known grid import direction"),
        "generator_meter": meter("M-GEN", "generator", "known generator supply direction"),
        "command_path": {
            "qualified_inverter_path": False,
            "safe_pv_observation": "controller request observed fail-closed/safe",
            "evidence_ref": f"safe-pv/{scenario_id}",
            "evidence_digest": COMMAND_DIGEST,
        },
        "fatal_counts": {
            "wdt": 0,
            "panic": 0,
            "no_mem": 0,
            "unexpected_reset": 0,
            "resource_collapse": 0,
        },
        "references": {"serial_log_ref": "serial.log", "hmi_http_ref": "capture.json"},
        "evidence_note": f"physical observation for {scenario_id} matched expected source authority",
    }
    if blocked:
        result["expected_safe_pv_request"] = 0
        result["observed_safe_pv_request"] = 0
    if transition:
        result["authority_returned_early"] = False
        result["recovery_started_at"] = "2026-09-03T12:04:50+00:00"
        result["recovery_ended_at"] = "2026-09-03T12:05:00+00:00"
        result["recovery_dwell_ms"] = 5000
        result["recovery_evidence_ref"] = f"recovery/{scenario_id}"
        result["recovery_evidence_digest"] = RECOVERY_DIGEST
    if scenario_id == "generator_meter_sign":
        result["meter_sign_proof"] = {
            "known_physical_direction": "generator supplying the known bench load",
            "independent_reference": "clamp-meter-capture-001",
            "observed_generator_kw": 12.3,
            "sign_matches": True,
        }
    return result


def passing_record(supports_sync: bool = True) -> dict:
    scenarios = [scenario(item) for item in REQUIRED_SCENARIOS]
    if not supports_sync:
        sync = next(item for item in scenarios if item["id"] == "synchronized")
        sync.clear()
        sync.update({
            "id": "synchronized",
            **identity_fields(),
            "outcome": "not_supported",
            "not_supported_reason": "site SLD has mechanically interlocked sources with no sync path",
            "topology_ref": "SLD-001-revA",
            "evidence_ref": "topology/synchronism-not-supported",
            "evidence_digest": SCENARIO_DIGEST,
        })
    return {
        "schema": 2,
        "evidence_state": "EXECUTED_GENERATOR_TRANSITION_PHYSICAL_EVIDENCE",
        **identity_fields(),
        "source_map_ref": "source-map-rev-001",
        "meter_map_ref": "meter-map-rev-001",
        "evidence_package_ref": "generator-transition-package-001",
        "evidence_package_digest": PACKAGE_DIGEST,
        "topology": {
            "topology_ref": "SLD-001-revA",
            "supports_sync": supports_sync,
            "power_sign_used_as_source_authority": False,
        },
        "source_signal_refs": ["breaker-drawing-1", "ats-drawing-2"],
        "meter_refs": ["grid-meter-manual", "generator-meter-manual"],
        "manual_wiring_refs": ["source-controller-manual-rev1", "site-wiring-drawing-revA"],
        "scenarios": scenarios,
    }


def run(rec: dict, *, firmware=SHA, artifact=DIGEST, site=SITE, config=CONFIG,
        topology=TOPOLOGY_DIGEST, source_map=SOURCE_MAP_DIGEST, meter_map=METER_MAP_DIGEST):
    return evaluate(rec, firmware, artifact, site, config, topology, source_map, meter_map)


class GeneratorPhysicalEvidenceTests(unittest.TestCase):
    def assertFailure(self, rec: dict, expected: str, **kwargs) -> None:
        result = run(rec, **kwargs)
        self.assertFalse(result.passed)
        self.assertIn(expected, result.failures)

    def test_complete_supported_matrix_passes(self) -> None:
        self.assertTrue(run(passing_record(True)).passed)

    def test_sync_not_supported_requires_same_authoritative_topology(self) -> None:
        record = passing_record(False)
        self.assertTrue(run(record).passed)
        sync = next(item for item in record["scenarios"] if item["id"] == "synchronized")
        sync["topology_ref"] = "OTHER-SLD"
        self.assertFailure(record, "scenario:synchronized:topology_ref_mismatch")

    def test_unexecuted_state_fails(self) -> None:
        record = passing_record()
        record["evidence_state"] = "UNEXECUTED_TEMPLATE_NOT_PHYSICAL_EVIDENCE"
        self.assertFailure(record, "evidence_state_not_executed")

    def test_external_identity_locks_fail_closed(self) -> None:
        self.assertFailure(passing_record(), "firmware_sha_mismatch", firmware="1" * 40)
        self.assertFailure(passing_record(), "artifact_digest_mismatch", artifact="sha256:" + "4" * 64)
        self.assertFailure(passing_record(), "site_id_mismatch", site="bench-B")
        self.assertFailure(passing_record(), "config_identity_mismatch", config="other-config")
        self.assertFailure(passing_record(), "topology_digest_mismatch", topology="sha256:" + "5" * 64)
        self.assertFailure(passing_record(), "source_map_digest_mismatch", source_map="sha256:" + "6" * 64)
        self.assertFailure(passing_record(), "meter_map_digest_mismatch", meter_map="sha256:" + "7" * 64)

    def test_scenario_copied_from_wrong_identity_fails(self) -> None:
        record = passing_record()
        island = next(item for item in record["scenarios"] if item["id"] == "island")
        island["source_map_digest"] = "sha256:" + "8" * 64
        self.assertFailure(record, "scenario:island:source_map_digest_mismatch")

    def test_power_sign_cannot_be_source_authority(self) -> None:
        record = passing_record()
        record["topology"]["power_sign_used_as_source_authority"] = True
        self.assertFailure(record, "power_sign_source_authority_not_forbidden")

    def test_missing_scenario_fails(self) -> None:
        record = passing_record()
        record["scenarios"] = [item for item in record["scenarios"] if item["id"] != "conflict"]
        self.assertFailure(record, "scenario_missing:conflict")

    def test_invalid_evidence_must_remain_blocked(self) -> None:
        record = passing_record()
        stale = next(item for item in record["scenarios"] if item["id"] == "stale")
        stale["expected_authority_sequence"] = ["allowed"]
        stale["observed_authority_sequence"] = ["allowed"]
        self.assertFailure(record, "scenario:stale:invalid_state_must_remain_blocked")

    def test_transition_requires_block_then_allow_and_full_dwell(self) -> None:
        record = passing_record()
        transition = next(item for item in record["scenarios"] if item["id"] == "grid_to_generator")
        transition["expected_authority_sequence"] = ["allowed"]
        transition["observed_authority_sequence"] = ["allowed"]
        transition["authority_returned_early"] = True
        self.assertFailure(record, "scenario:grid_to_generator:transition_must_block_then_allow")
        self.assertIn("scenario:grid_to_generator:authority_returned_early_not_false", run(record).failures)

    def test_recovery_chronology_and_digest_are_mandatory(self) -> None:
        record = passing_record()
        transition = next(item for item in record["scenarios"] if item["id"] == "generator_to_grid")
        transition["recovery_ended_at"] = "2026-09-03T12:04:51+00:00"
        transition["recovery_dwell_ms"] = 5000
        transition["recovery_evidence_digest"] = "bad"
        result = run(record)
        self.assertFalse(result.passed)
        self.assertIn("scenario:generator_to_grid:recovery_timestamp_duration_shorter_than_claimed_dwell", result.failures)
        self.assertIn("scenario:generator_to_grid:recovery_evidence_digest_invalid", result.failures)

    def test_recovery_must_stay_inside_scenario(self) -> None:
        record = passing_record()
        transition = next(item for item in record["scenarios"] if item["id"] == "recovery_dwell")
        transition["recovery_started_at"] = "2026-09-03T11:59:59+00:00"
        self.assertFailure(record, "scenario:recovery_dwell:recovery_timestamps_outside_scenario")

    def test_meter_map_requires_identity_and_raw_scale_consistency(self) -> None:
        record = passing_record()
        island = next(item for item in record["scenarios"] if item["id"] == "island")
        island["grid_meter"]["scaled_kw"] = 999.0
        self.assertFailure(record, "scenario:island:grid:meter_raw_scale_mismatch")

        record = passing_record()
        island = next(item for item in record["scenarios"] if item["id"] == "island")
        island["generator_meter"]["ct_pt_polarity_ref"] = ""
        self.assertFailure(record, "scenario:island:generator:meter_field_missing:ct_pt_polarity_ref")

    def test_generator_meter_sign_must_match_recorded_meter_value(self) -> None:
        record = passing_record()
        sign_case = next(item for item in record["scenarios"] if item["id"] == "generator_meter_sign")
        sign_case["meter_sign_proof"]["observed_generator_kw"] = 99.0
        self.assertFailure(record, "scenario:generator_meter_sign:generator_meter_sign_value_mismatch")

    def test_nonzero_fatal_count_fails(self) -> None:
        record = passing_record()
        island = next(item for item in record["scenarios"] if item["id"] == "island")
        island["fatal_counts"]["wdt"] = 1
        self.assertFailure(record, "scenario:island:fatal_count_nonzero_or_missing:wdt")

    def test_safe_pv_mismatch_fails(self) -> None:
        record = passing_record()
        conflict = next(item for item in record["scenarios"] if item["id"] == "conflict")
        conflict["observed_safe_pv_request"] = 10
        self.assertFailure(record, "scenario:conflict:safe_pv_request_mismatch")

    def test_qualified_command_and_readback_must_be_numeric_and_have_digest(self) -> None:
        record = passing_record()
        island = next(item for item in record["scenarios"] if item["id"] == "island")
        island["command_path"] = {
            "qualified_inverter_path": True,
            "command": None,
            "readback": None,
            "evidence_ref": "capture-1",
            "evidence_digest": "bad",
        }
        result = run(record)
        self.assertFalse(result.passed)
        self.assertIn("scenario:island:qualified_command_readback_invalid", result.failures)
        self.assertIn("scenario:island:command_evidence_digest_invalid", result.failures)


if __name__ == "__main__":
    unittest.main(verbosity=2)
