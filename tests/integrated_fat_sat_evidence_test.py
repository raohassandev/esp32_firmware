#!/usr/bin/env python3
import importlib.util
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "integrated_fat_sat_evidence_verify",
    ROOT / "tools" / "integrated_fat_sat_evidence_verify.py",
)
MOD = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
sys.modules[SPEC.name] = MOD
SPEC.loader.exec_module(MOD)

FW = "a" * 40
DIGEST = "sha256:" + "b" * 64
CONFIG = "config-sha256-exact-release-123"


def scenario(sid: str):
    return {
        "id": sid,
        "status": "pass",
        "started_at": "2026-09-03T10:00:00+05:00",
        "ended_at": "2026-09-03T10:05:00+05:00",
        "step": f"physical scenario {sid}",
        "source_provenance": "commissioned breaker/ATS/run/sync evidence package",
        "meter_values_ref": "meter-capture-ref-01",
        "command_readback_ref": "command-readback-ref-01",
        "serial_runtime_log_ref": "serial-runtime-log-ref-01",
        "hmi_http_evidence_ref": "hmi-http-capture-ref-01",
        "expected_state": "fail-closed safety policy and scenario target",
        "observed_state": "observed state matched target with safety intact",
        "fail_closed_when_required": True,
        "expected_observed_match": True,
        "fatal_counts": {
            "wdt": 0,
            "panic": 0,
            "no_mem": 0,
            "unexpected_reset": 0,
            "resource_collapse": 0
        },
        "pass_reason": "Physical evidence matched expected behavior with no safety or resource failure."
    }


def prereq(identity: str):
    return {"passed": True, "evidence_ref": f"evidence-{identity}-01", "identity": identity}


def record():
    p = {
        "waveshare_final_acceptance": prereq("waveshare-exact-image"),
        "backend_parity_persistence_arm": prereq("waveshare-post-soak-exact-image"),
        "source_transition_physical": prereq("grid-generator-transition-matrix"),
        "site_source_commissioning": prereq("site-source-map-and-polarity"),
        "inverter_profiles_production_approved": prereq("approved-profile-set"),
        "ota_physical_qualification": prereq("ota-capable-release-identity"),
    }
    p["inverter_profiles_production_approved"]["profile_ids"] = ["Huawei-exact-profile", "GoodWe-exact-profile"]
    return {
        "schema": 1,
        "site_id": "SITE-A",
        "firmware_sha": FW,
        "artifact_digest": DIGEST,
        "config_identity": CONFIG,
        "all_evidence_same_release_identity": True,
        "simulator_or_ci_used_as_physical_substitute": False,
        "prerequisites": p,
        "matrices": {
            "grid": [scenario(x) for x in sorted(MOD.REQUIRED_GRID)],
            "generator": [scenario(x) for x in sorted(MOD.REQUIRED_GENERATOR)],
            "mixed": [scenario(x) for x in sorted(MOD.REQUIRED_MIXED)],
            "modbus": [scenario(x) for x in sorted(MOD.REQUIRED_MODBUS)],
            "ota": [scenario(x) for x in sorted(MOD.REQUIRED_OTA)],
        },
        "endurance_summary": {
            "passed": True,
            "duration_seconds": 28800,
            "sample_count": 480,
            "min_heap_free": 100000,
            "min_largest_block": 50000,
            "min_dma_free": 30000,
            "resource_trend_ref": "resource-trend-log-01",
            "socket_lwip_resource_ref": "lwip-socket-timewait-log-01",
            "multi_device_load_ref": "multi-device-endurance-log-01",
            "unrelated_control_web_responsive": True,
            "fatal_counts": {
                "wdt": 0,
                "panic": 0,
                "no_mem": 0,
                "unexpected_reset": 0,
                "resource_collapse": 0
            }
        },
        "sat": {
            "accepted": True,
            "site_representative": "Authorized site representative",
            "role": "Site acceptance authority",
            "signed_at": "2026-09-03T18:00:00+05:00",
            "signed_record_ref": "signed-sat-document-01",
            "firmware_sha": FW,
            "config_identity": CONFIG,
            "approved_profile_set_identity": "approved-profile-set",
            "no_behavior_change_after_evidence": True,
            "physical_executor_attestation": True,
            "site_acceptance_signature_present": True
        },
        "release_ready": True,
        "release_verdict_reason": "All prerequisite qualifications, integrated physical matrices, endurance evidence and signed SAT passed on one exact release identity."
    }


def assert_failure(rec, expected: str):
    result = MOD.evaluate(rec)
    assert not result.passed, "record unexpectedly passed"
    assert expected in result.failures, result.failures


def main() -> None:
    good = MOD.evaluate(record())
    assert good.passed, good.failures
    expected_count = sum(map(len, (MOD.REQUIRED_GRID, MOD.REQUIRED_GENERATOR, MOD.REQUIRED_MIXED, MOD.REQUIRED_MODBUS, MOD.REQUIRED_OTA)))
    assert good.scenario_count == expected_count

    r = record()
    r["prerequisites"]["waveshare_final_acceptance"]["passed"] = False
    assert_failure(r, "prerequisite:waveshare_final_acceptance:not_passed")

    r = record()
    r["matrices"]["grid"] = [x for x in r["matrices"]["grid"] if x["id"] != "grid_zero_export"]
    assert_failure(r, "matrix:grid:required_missing:grid_zero_export")

    r = record()
    r["matrices"]["generator"][0]["fatal_counts"]["panic"] = 1
    assert any("fatal_count_nonzero_or_invalid:panic" in x for x in MOD.evaluate(r).failures)

    r = record()
    sync = next(x for x in r["matrices"]["mixed"] if x["id"] == "mixed_synchronism")
    sync.clear()
    sync.update({
        "id": "mixed_synchronism",
        "status": "not_supported",
        "topology_ref": "approved ATS interlock drawing rev C",
        "reason": "Approved topology does not permit synchronized Grid+Generator operation."
    })
    assert MOD.evaluate(r).passed

    r = record()
    r["matrices"]["grid"][0]["status"] = "not_supported"
    r["matrices"]["grid"][0]["topology_ref"] = "ref"
    r["matrices"]["grid"][0]["reason"] = "not supported here"
    assert any("not_supported_only_allowed_for_synchronism" in x for x in MOD.evaluate(r).failures)

    r = record()
    r["endurance_summary"]["unrelated_control_web_responsive"] = False
    assert_failure(r, "endurance_summary:unrelated_services_not_proven")

    r = record()
    r["sat"]["firmware_sha"] = "c" * 40
    assert_failure(r, "sat:firmware_sha_mismatch")

    r = record()
    r["sat"]["site_acceptance_signature_present"] = False
    assert_failure(r, "sat:site_signature_missing")

    r = record()
    r["simulator_or_ci_used_as_physical_substitute"] = True
    assert_failure(r, "simulator_or_ci_physical_substitution_not_forbidden")

    r = record()
    r["release_ready"] = False
    assert_failure(r, "release_ready_not_true")

    print("Integrated FAT/SAT evidence tests passed")


if __name__ == "__main__":
    main()
