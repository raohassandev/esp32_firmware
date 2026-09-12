#!/usr/bin/env python3
import copy
import importlib.util
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "inverter_profile_physical_evidence_verify",
    ROOT / "tools" / "inverter_profile_physical_evidence_verify.py",
)
MOD = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
sys.modules[SPEC.name] = MOD
SPEC.loader.exec_module(MOD)

PROFILE_SHA = "a" * 40
FW_SHA = "b" * 40
ARTIFACT = "sha256:" + "c" * 64
MANUAL_DIGEST = "sha256:" + "d" * 64
READ_ONLY_DIGEST = "sha256:" + "e" * 64
WRITE_DIGEST = "sha256:" + "f" * 64
ROLLBACK_DIGEST = "sha256:" + "1" * 64
RECONNECT_DIGEST = "sha256:" + "2" * 64
APPROVAL_DIGEST = "sha256:" + "3" * 64
MANUFACTURER = "ExactVendor"
MODEL = "EXACT-100K"
INVERTER_FW = "V1.2.3"
MANUAL_REV = "Rev 7"
TOPOLOGY = "direct_tcp"
ENDPOINT = "192.168.10.30:502"
UNIT_ID = 1


def expected_identity():
    return {
        "manufacturer": MANUFACTURER,
        "model": MODEL,
        "inverter_firmware": INVERTER_FW,
        "profile_source_sha": PROFILE_SHA,
        "controller_firmware_sha": FW_SHA,
        "controller_artifact_digest": ARTIFACT,
        "manual_revision": MANUAL_REV,
        "manual_document_sha256": MANUAL_DIGEST,
        "topology": TOPOLOGY,
        "endpoint": ENDPOINT,
        "unit_id": UNIT_ID,
    }


def mapping(address: str, manual_ref: str, *, writable: bool = False):
    item = {
        "address": address,
        "data_type": "U16",
        "word_order": "ABCD",
        "units": "%",
        "scale": 0.1,
        "manual_ref": manual_ref,
        "manual_revision": MANUAL_REV,
        "manual_document_sha256": MANUAL_DIGEST,
        "manual_backed": True,
    }
    if writable:
        item.update(
            {
                "function_code": 6,
                "raw_min": 0,
                "raw_max": 1000,
                "enable_semantics": "command enabled only after qualified identity and write authority",
                "disable_semantics": "zero/disable semantics exactly as documented by manufacturer",
            }
        )
    return item


def record():
    return {
        "schema": 2,
        "stage": "production_approved",
        "evidence_state": "EXECUTED_PRODUCTION_APPROVAL_EVIDENCE",
        "manufacturer": MANUFACTURER,
        "model": MODEL,
        "inverter_firmware": INVERTER_FW,
        "profile_source_sha": PROFILE_SHA,
        "controller_firmware_sha": FW_SHA,
        "controller_artifact_digest": ARTIFACT,
        "third_party_or_guessed_map_used": False,
        "automatic_production_write_allowed_before_approval": False,
        "manual": {
            "title": "ExactVendor EXACT-100K Modbus Interface Definitions",
            "manufacturer": MANUFACTURER,
            "revision": MANUAL_REV,
            "publication_date": "2026-07-10",
            "official_manufacturer_source": True,
            "model_scope": [MODEL],
            "document_sha256": MANUAL_DIGEST,
            "evidence_ref": "manufacturer-portal-download-record-01",
            "firmware_scope": {
                "applicability_confirmed": True,
                "exact_inverter_firmware": INVERTER_FW,
                "basis": "Manufacturer applicability table explicitly includes inverter firmware V1.2.3",
            },
        },
        "connection": {
            "topology": TOPOLOGY,
            "endpoint": ENDPOINT,
            "unit_id": UNIT_ID,
            "identity_probe_ref": "bench-identity-probe-log-01",
        },
        "register_map": {
            "identity": mapping("30000", "manual p10 identity"),
            "active_power": mapping("32080", "manual p18 active power"),
            "status": mapping("32089", "manual p20 status"),
            "fault": mapping("32090", "manual p21 fault"),
            "telemetry": [
                mapping("32064", "manual p17 voltage"),
                mapping("32080", "manual p18 active power"),
            ],
            "command": mapping("40100", "manual p35 power limit command", writable=True),
            "readback": {
                **mapping("40101", "manual p35 power limit readback"),
                "tolerance": 1.0,
            },
        },
        "physical_read_only": {
            "performed": True,
            "pass": True,
            "started_at": "2026-09-03T10:00:00+05:00",
            "ended_at": "2026-09-03T10:10:00+05:00",
            "identity_raw": "raw identity bytes/log",
            "identity_decoded": "ExactVendor EXACT-100K V1.2.3",
            "identity_matches_exact_model_firmware": True,
            "telemetry_evidence_ref": "bench-telemetry-crosscheck-01",
            "status_evidence_ref": "bench-status-toggle-correlation-01",
            "status_register_physically_correlated": True,
            "write_attempted": False,
            "evidence_package_ref": "read-only-package-01",
            "evidence_package_digest": READ_ONLY_DIGEST,
        },
        "physical_write": {
            "performed": True,
            "pass": True,
            "started_at": "2026-09-03T10:20:00+05:00",
            "ended_at": "2026-09-03T10:40:00+05:00",
            "automatic_control_enabled_during_test": False,
            "evidence_ref": "controlled-write-test-01",
            "evidence_digest": WRITE_DIGEST,
            "safe_start_state": "bench authorized, automatic control disabled, export risk isolated",
            "requested_engineering_value": 50.0,
            "requested_raw_value": 500,
            "observed_readback_engineering_value": 50.2,
            "observed_readback_raw_value": 502,
            "command_transmitted_ref": "command-wire-capture-01",
            "readback_ref": "readback-wire-capture-01",
            "safe_zero_evidence_ref": "safe-zero-test-01",
            "timeout_evidence_ref": "timeout-test-01",
            "exception_evidence_ref": "exception-test-01",
            "readback_within_tolerance": True,
            "observed_response_matches_command": True,
            "safe_zero_proven": True,
            "timeout_fail_safe_proven": True,
            "exception_fail_safe_proven": True,
            "rollback": {
                "performed": True,
                "original_value_restored": True,
                "readback_match": True,
                "failure_path_exercised": True,
                "safe_fallback_observed": True,
                "original_engineering_value": 75.0,
                "restored_engineering_value": 75.1,
                "evidence_ref": "rollback-failure-test-01",
                "evidence_digest": ROLLBACK_DIGEST,
            },
            "reconnect": {
                "identity_revalidated_before_write_authority": True,
                "stale_identity_blocks_write": True,
                "evidence_ref": "reconnect-stale-identity-test-01",
                "evidence_digest": RECONNECT_DIGEST,
            },
        },
        "production_approval": {
            "approved": True,
            "approver": "Authorized engineering approver",
            "role": "Product Owner / commissioning authority",
            "approved_at": "2026-09-03T15:00:00+05:00",
            "approval_record_ref": "signed-profile-approval-EXACT-100K-01",
            "approval_record_digest": APPROVAL_DIGEST,
            "signature_present": True,
            "manual_identity": "ExactVendor manual Rev 7 sha256 bound in evidence",
            "bench_evidence_identity": "controlled bench evidence package 01",
            "manufacturer": MANUFACTURER,
            "model": MODEL,
            "inverter_firmware": INVERTER_FW,
            "profile_source_sha": PROFILE_SHA,
            "controller_firmware_sha": FW_SHA,
            "controller_artifact_digest": ARTIFACT,
            "manual_revision": MANUAL_REV,
            "manual_document_sha256": MANUAL_DIGEST,
            "topology": TOPOLOGY,
            "endpoint": ENDPOINT,
            "unit_id": UNIT_ID,
            "profile_source_sha_confirmed": True,
            "firmware_build_sha_confirmed": True,
            "manual_revision_confirmed": True,
            "write_permission_authorized": True,
            "no_identity_or_mapping_change_after_bench": True,
        },
    }


def assert_failure(rec, expected: str, expected_stage: str = "production_approved", identity=None):
    result = MOD.evaluate(rec, expected_stage, identity or expected_identity())
    assert not result.passed, "record unexpectedly passed"
    assert expected in result.failures, result.failures


def main() -> None:
    good = MOD.evaluate(record(), "production_approved", expected_identity())
    assert good.passed, good.failures

    no_external_lock = MOD.evaluate(record(), "production_approved", None)
    assert "expected_identity_missing" in no_external_lock.failures

    external_mutations = {
        "manufacturer": "OtherVendor",
        "model": "OTHER-100K",
        "inverter_firmware": "V9.9.9",
        "profile_source_sha": "4" * 40,
        "controller_firmware_sha": "5" * 40,
        "controller_artifact_digest": "sha256:" + "6" * 64,
        "manual_revision": "Rev 99",
        "manual_document_sha256": "sha256:" + "7" * 64,
        "topology": "manufacturer_logger",
        "endpoint": "192.168.99.99:502",
        "unit_id": 7,
    }
    for key, value in external_mutations.items():
        identity = expected_identity()
        identity[key] = value
        assert_failure(record(), f"expected_identity:{key}_mismatch", identity=identity)

    bad_state = record()
    bad_state["evidence_state"] = "DOCUMENTED_MANUFACTURER_EVIDENCE"
    assert_failure(bad_state, "evidence_state_mismatch")

    guessed = record()
    guessed["third_party_or_guessed_map_used"] = True
    assert_failure(guessed, "third_party_or_guessed_map_not_forbidden")

    wrong_model = record()
    wrong_model["manual"]["model_scope"] = ["OTHER-MODEL"]
    assert_failure(wrong_model, "manual:exact_model_not_in_scope")

    wrong_fw_scope = record()
    wrong_fw_scope["manual"]["firmware_scope"]["exact_inverter_firmware"] = "V1.2.2"
    assert_failure(wrong_fw_scope, "manual:exact_inverter_firmware_mismatch")

    unofficial = record()
    unofficial["manual"]["official_manufacturer_source"] = False
    assert_failure(unofficial, "manual:not_official_manufacturer_source")

    mixed_manual = record()
    mixed_manual["register_map"]["command"]["manual_document_sha256"] = "sha256:" + "8" * 64
    assert_failure(mixed_manual, "register_map:command:manual_document_sha256_mismatch")

    status_guess = record()
    status_guess["physical_read_only"]["status_register_physically_correlated"] = False
    assert_failure(status_guess, "physical_read_only:status_not_physically_correlated")

    no_ro_digest = record()
    no_ro_digest["physical_read_only"]["evidence_package_digest"] = "bad"
    assert_failure(no_ro_digest, "physical_read_only:evidence_package_digest_invalid")

    overlap = record()
    overlap["physical_write"]["started_at"] = "2026-09-03T10:05:00+05:00"
    assert_failure(overlap, "physical_write:started_before_read_only_evidence_completed")

    no_readback = record()
    no_readback["physical_write"]["readback_within_tolerance"] = False
    assert_failure(no_readback, "physical_write:readback_within_tolerance_not_true")

    measured_outside = record()
    measured_outside["physical_write"]["observed_readback_engineering_value"] = 55.0
    assert_failure(measured_outside, "physical_write:measured_readback_outside_tolerance")

    restored_outside = record()
    restored_outside["physical_write"]["rollback"]["restored_engineering_value"] = 70.0
    assert_failure(restored_outside, "physical_write:rollback:measured_restore_outside_tolerance")

    stale_write = record()
    stale_write["physical_write"]["reconnect"]["stale_identity_blocks_write"] = False
    assert_failure(stale_write, "physical_write:stale_identity_did_not_block_write")

    early_approval = record()
    early_approval["production_approval"]["approved_at"] = "2026-09-03T10:30:00+05:00"
    assert_failure(early_approval, "production_approval:approved_before_write_evidence_completed")

    unsigned = record()
    unsigned["production_approval"]["signature_present"] = False
    assert_failure(unsigned, "production_approval:signature_missing")

    approval_wrong_profile = record()
    approval_wrong_profile["production_approval"]["profile_source_sha"] = "9" * 40
    assert_failure(approval_wrong_profile, "production_approval:profile_source_sha_mismatch")

    premature = record()
    premature["stage"] = "write_qualified"
    premature["evidence_state"] = "EXECUTED_PHYSICAL_WRITE_EVIDENCE"
    del premature["production_approval"]
    assert MOD.evaluate(premature, "write_qualified", expected_identity()).passed
    assert not MOD.evaluate(premature, "production_approved", expected_identity()).passed

    bad_fc = record()
    bad_fc["register_map"]["command"]["function_code"] = 3
    assert_failure(bad_fc, "register_map:command:function_code_invalid")

    bad_digest = record()
    bad_digest["manual"]["document_sha256"] = "not-a-digest"
    result = MOD.evaluate(bad_digest, "production_approved", expected_identity())
    assert "manual:document_sha256_invalid" in result.failures

    print("Inverter physical evidence validator tests passed")


if __name__ == "__main__":
    main()
