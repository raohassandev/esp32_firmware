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
TREE = "1" * 40
DIGEST = "sha256:" + "b" * 64
CONFIG = "config-sha256-exact-release-123"
SITE = "SITE-A"
SITE_MAP = "sha256:" + "c" * 64
PROFILE_MANIFEST = "sha256:" + "d" * 64
SAT_DIGEST = "sha256:" + "e" * 64


def expected(**overrides):
    values = {
        "firmware_sha": FW,
        "firmware_tree_sha": TREE,
        "artifact_digest": DIGEST,
        "config_identity": CONFIG,
        "site_id": SITE,
        "site_map_digest": SITE_MAP,
        "profile_manifest_digest": PROFILE_MANIFEST,
    }
    values.update(overrides)
    return MOD.ReleaseIdentity(**values)


def scenario(sid: str):
    return {
        "id": sid,
        "status": "pass",
        "firmware_sha": FW,
        "firmware_tree_sha": TREE,
        "artifact_digest": DIGEST,
        "config_identity": CONFIG,
        "site_id": SITE,
        "site_map_digest": SITE_MAP,
        "profile_manifest_digest": PROFILE_MANIFEST,
        "started_at": "2026-09-03T10:00:00+05:00",
        "ended_at": "2026-09-03T10:05:00+05:00",
        "step": f"physical scenario {sid}",
        "source_provenance": "commissioned breaker/ATS/run/sync evidence package",
        "endpoint_unit_map_ref": "endpoint-unit-map-01",
        "meter_role_map_ref": "meter-role-map-01",
        "source_contact_map_ref": "source-contact-map-01",
        "meter_values_ref": "meter-capture-ref-01",
        "command_readback_ref": "command-readback-ref-01",
        "serial_runtime_log_ref": "serial-runtime-log-ref-01",
        "hmi_http_evidence_ref": "hmi-http-capture-ref-01",
        "measurements": {"grid_kw": 100.0, "load_kw": 125.0},
        "command_readback_values": {"solar_command_pct": 40.0, "solar_readback_pct": 40.0},
        "expected_state": "fail-closed safety policy and scenario target",
        "observed_state": "observed state matched target with safety intact",
        "fail_closed_when_required": True,
        "expected_observed_match": True,
        "fatal_counts": {
            "wdt": 0,
            "panic": 0,
            "no_mem": 0,
            "unexpected_reset": 0,
            "resource_collapse": 0,
        },
        "pass_reason": "Physical evidence matched expected behavior with no safety or resource failure.",
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
        "schema": 2,
        "evidence_state": MOD.EXECUTED_STATE,
        "site_id": SITE,
        "firmware_sha": FW,
        "firmware_tree_sha": TREE,
        "artifact_digest": DIGEST,
        "config_identity": CONFIG,
        "site_map_digest": SITE_MAP,
        "profile_manifest_digest": PROFILE_MANIFEST,
        "approved_profiles_ref": "approved-profile-manifest-01",
        "approved_profiles": [
            {
                "profile_id": "Huawei-exact-profile",
                "manufacturer": "Huawei",
                "model": "SUN2000-exact-model",
                "inverter_firmware": "exact-fw-1",
                "manual_revision": "official-rev-1",
                "approval_ref": "signed-approval-huawei-01",
            },
            {
                "profile_id": "GoodWe-exact-profile",
                "manufacturer": "GoodWe",
                "model": "GW-exact-model",
                "inverter_firmware": "exact-fw-2",
                "manual_revision": "official-rev-2",
                "approval_ref": "signed-approval-goodwe-01",
            },
        ],
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
                "resource_collapse": 0,
            },
        },
        "sat": {
            "accepted": True,
            "site_representative": "Authorized site representative",
            "role": "Site acceptance authority",
            "signed_at": "2026-09-03T18:00:00+05:00",
            "signed_record_ref": "signed-sat-document-01",
            "signed_record_digest": SAT_DIGEST,
            "firmware_sha": FW,
            "firmware_tree_sha": TREE,
            "artifact_digest": DIGEST,
            "config_identity": CONFIG,
            "site_id": SITE,
            "site_map_digest": SITE_MAP,
            "site_source_mapping_ref": "signed-site-source-map-01",
            "profile_manifest_digest": PROFILE_MANIFEST,
            "approved_profiles_ref": "approved-profile-manifest-01",
            "no_behavior_change_after_evidence": True,
            "physical_executor_attestation": True,
            "site_acceptance_signature_present": True,
        },
        "release_ready": True,
        "release_verdict_reason": "All prerequisite qualifications, integrated physical matrices, endurance evidence and signed SAT passed on one exact release identity.",
    }


def evaluate(rec, exp=None):
    return MOD.evaluate(rec, exp or expected())


def assert_failure(rec, expected_failure: str, exp=None):
    result = evaluate(rec, exp)
    assert not result.passed, "record unexpectedly passed"
    assert expected_failure in result.failures, result.failures


def main() -> None:
    good = evaluate(record())
    assert good.passed, good.failures
    expected_count = sum(map(len, (MOD.REQUIRED_GRID, MOD.REQUIRED_GENERATOR, MOD.REQUIRED_MIXED, MOD.REQUIRED_MODBUS, MOD.REQUIRED_OTA)))
    assert good.scenario_count == expected_count

    r = record()
    r["evidence_state"] = "UNEXECUTED_TEMPLATE_NOT_A_PHYSICAL_PASS"
    assert_failure(r, "evidence_state_not_executed")

    identity_mismatches = [
        ("firmware_sha", "f" * 40, "firmware_sha_expected_mismatch"),
        ("firmware_tree_sha", "2" * 40, "firmware_tree_sha_expected_mismatch"),
        ("artifact_digest", "sha256:" + "3" * 64, "artifact_digest_expected_mismatch"),
        ("config_identity", "different-config", "config_identity_expected_mismatch"),
        ("site_id", "SITE-B", "site_id_expected_mismatch"),
        ("site_map_digest", "sha256:" + "4" * 64, "site_map_digest_expected_mismatch"),
        ("profile_manifest_digest", "sha256:" + "5" * 64, "profile_manifest_digest_expected_mismatch"),
    ]
    for field, value, failure in identity_mismatches:
        kwargs = {field: value}
        assert_failure(record(), failure, expected(**kwargs))

    r = record()
    r["prerequisites"]["waveshare_final_acceptance"]["passed"] = False
    assert_failure(r, "prerequisite:waveshare_final_acceptance:not_passed")

    r = record()
    r["matrices"]["grid"] = [x for x in r["matrices"]["grid"] if x["id"] != "grid_zero_export"]
    assert_failure(r, "matrix:grid:required_missing:grid_zero_export")

    r = record()
    r["matrices"]["generator"][0]["fatal_counts"]["panic"] = 1
    assert any("fatal_count_nonzero_or_invalid:panic" in x for x in evaluate(r).failures)

    r = record()
    r["matrices"]["grid"][0]["firmware_tree_sha"] = "9" * 40
    assert any("firmware_tree_sha_mismatch" in x for x in evaluate(r).failures)

    r = record()
    r["matrices"]["grid"][0]["measurements"] = {}
    assert any("measurements_missing" in x for x in evaluate(r).failures)

    r = record()
    r["matrices"]["grid"][0]["command_readback_values"] = {}
    assert any("command_readback_values_missing" in x for x in evaluate(r).failures)

    r = record()
    r["matrices"]["grid"][0]["endpoint_unit_map_ref"] = ""
    assert any("endpoint_unit_map_ref_missing" in x for x in evaluate(r).failures)

    r = record()
    sync = next(x for x in r["matrices"]["mixed"] if x["id"] == "mixed_synchronism")
    identity_fields = {k: sync[k] for k in (
        "firmware_sha", "firmware_tree_sha", "artifact_digest", "config_identity", "site_id", "site_map_digest", "profile_manifest_digest"
    )}
    sync.clear()
    sync.update({
        "id": "mixed_synchronism",
        "status": "not_supported",
        "topology_ref": "approved ATS interlock drawing rev C",
        "reason": "Approved topology does not permit synchronized Grid+Generator operation.",
        **identity_fields,
    })
    assert evaluate(r).passed, evaluate(r).failures

    r = record()
    r["matrices"]["grid"][0]["status"] = "not_supported"
    r["matrices"]["grid"][0]["topology_ref"] = "ref"
    r["matrices"]["grid"][0]["reason"] = "not supported here"
    assert any("not_supported_only_allowed_for_synchronism" in x for x in evaluate(r).failures)

    r = record()
    r["endurance_summary"]["unrelated_control_web_responsive"] = False
    assert_failure(r, "endurance_summary:unrelated_services_not_proven")

    sat_mismatches = [
        ("firmware_sha", "c" * 40, "sat:firmware_sha_mismatch"),
        ("firmware_tree_sha", "6" * 40, "sat:firmware_tree_sha_mismatch"),
        ("artifact_digest", "sha256:" + "7" * 64, "sat:artifact_digest_mismatch"),
        ("config_identity", "different-config", "sat:config_identity_mismatch"),
        ("site_id", "SITE-Z", "sat:site_id_mismatch"),
        ("site_map_digest", "sha256:" + "8" * 64, "sat:site_map_digest_mismatch"),
        ("profile_manifest_digest", "sha256:" + "9" * 64, "sat:profile_manifest_digest_mismatch"),
    ]
    for field, value, failure in sat_mismatches:
        r = record()
        r["sat"][field] = value
        assert_failure(r, failure)

    r = record()
    r["sat"]["signed_record_digest"] = ""
    assert_failure(r, "sat:signed_record_digest_invalid")

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
