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


def mapping(address: str, manual_ref: str, *, writable: bool = False):
    item = {
        "address": address,
        "data_type": "U16",
        "word_order": "ABCD",
        "units": "%",
        "scale": 0.1,
        "manual_ref": manual_ref,
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
        "schema": 1,
        "stage": "production_approved",
        "manufacturer": "ExactVendor",
        "model": "EXACT-100K",
        "inverter_firmware": "V1.2.3",
        "profile_source_sha": PROFILE_SHA,
        "controller_firmware_sha": FW_SHA,
        "controller_artifact_digest": ARTIFACT,
        "third_party_or_guessed_map_used": False,
        "automatic_production_write_allowed_before_approval": False,
        "manual": {
            "title": "ExactVendor EXACT-100K Modbus Interface Definitions",
            "manufacturer": "ExactVendor",
            "revision": "Rev 7",
            "publication_date": "2026-07-10",
            "official_manufacturer_source": True,
            "model_scope": ["EXACT-100K"],
            "document_sha256": MANUAL_DIGEST,
            "evidence_ref": "manufacturer-portal-download-record-01",
            "firmware_scope": {
                "applicability_confirmed": True,
                "basis": "Manufacturer applicability table explicitly includes inverter firmware V1.2.3"
            },
        },
        "connection": {
            "topology": "direct_tcp",
            "endpoint": "192.168.10.30:502",
            "unit_id": 1,
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
        },
        "physical_write": {
            "performed": True,
            "pass": True,
            "automatic_control_enabled_during_test": False,
            "evidence_ref": "controlled-write-test-01",
            "safe_start_state": "bench authorized, automatic control disabled, export risk isolated",
            "requested_engineering_value": "50 percent",
            "requested_raw_value": "500",
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
                "evidence_ref": "rollback-failure-test-01",
            },
            "reconnect": {
                "identity_revalidated_before_write_authority": True,
                "stale_identity_blocks_write": True,
                "evidence_ref": "reconnect-stale-identity-test-01",
            },
        },
        "production_approval": {
            "approved": True,
            "approver": "Authorized engineering approver",
            "role": "Product Owner / commissioning authority",
            "approved_at": "2026-09-03T15:00:00+05:00",
            "approval_record_ref": "signed-profile-approval-EXACT-100K-01",
            "manual_identity": "ExactVendor manual Rev 7 sha256 bound in evidence",
            "bench_evidence_identity": "controlled bench evidence package 01",
            "profile_source_sha_confirmed": True,
            "firmware_build_sha_confirmed": True,
            "manual_revision_confirmed": True,
            "write_permission_authorized": True,
        },
    }


def main() -> None:
    good = MOD.evaluate(record(), "production_approved")
    assert good.passed, good.failures

    guessed = record()
    guessed["third_party_or_guessed_map_used"] = True
    assert "third_party_or_guessed_map_not_forbidden" in MOD.evaluate(guessed).failures

    wrong_model = record()
    wrong_model["manual"]["model_scope"] = ["OTHER-MODEL"]
    assert "manual:exact_model_not_in_scope" in MOD.evaluate(wrong_model).failures

    unofficial = record()
    unofficial["manual"]["official_manufacturer_source"] = False
    assert "manual:not_official_manufacturer_source" in MOD.evaluate(unofficial).failures

    status_guess = record()
    status_guess["physical_read_only"]["status_register_physically_correlated"] = False
    assert "physical_read_only:status_not_physically_correlated" in MOD.evaluate(status_guess).failures

    no_readback = record()
    no_readback["physical_write"]["readback_within_tolerance"] = False
    assert "physical_write:readback_within_tolerance_not_true" in MOD.evaluate(no_readback).failures

    no_rollback = record()
    no_rollback["physical_write"]["rollback"]["readback_match"] = False
    assert "physical_write:rollback:readback_match_not_true" in MOD.evaluate(no_rollback).failures

    stale_write = record()
    stale_write["physical_write"]["reconnect"]["stale_identity_blocks_write"] = False
    assert "physical_write:stale_identity_did_not_block_write" in MOD.evaluate(stale_write).failures

    premature = record()
    premature["stage"] = "write_qualified"
    del premature["production_approval"]
    assert MOD.evaluate(premature, "write_qualified").passed
    assert not MOD.evaluate(premature, "production_approved").passed

    bad_fc = record()
    bad_fc["register_map"]["command"]["function_code"] = 3
    assert "register_map:command:function_code_invalid" in MOD.evaluate(bad_fc).failures

    bad_digest = record()
    bad_digest["manual"]["document_sha256"] = "not-a-digest"
    assert "manual:document_sha256_invalid" in MOD.evaluate(bad_digest).failures

    print("Inverter physical evidence validator tests passed")


if __name__ == "__main__":
    main()
