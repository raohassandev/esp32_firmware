#!/usr/bin/env python3
from copy import deepcopy

from release_traceability_verify import evaluate

SHA = "1" * 40
TREE = "2" * 40
DIGEST = "sha256:" + "a" * 64
EVIDENCE = "sha256:" + "b" * 64
CONFIG = "sha256:" + "c" * 64
SITE = "sha256:" + "d" * 64
PROFILE = "sha256:" + "e" * 64
APP = "sha256:" + "f" * 64


def lane(source=SHA, mode="exact"):
    binding = {"mode": mode}
    if mode == "exact":
        binding["artifact_digest"] = DIGEST
    else:
        binding.update({
            "behavior_affecting_changes": False,
            "replay_ref": "run-1",
            "approval_ref": "approval-1",
            "final_release_checks_passed": True,
        })
    return {
        "status": "PASS",
        "source_sha": source,
        "evidence_ref": "evidence-1",
        "evidence_digest": EVIDENCE,
        "physical_or_authorized_evidence": True,
        "validator_only": False,
        "final_release_binding": binding,
    }


def valid_record():
    rec = {
        "record_status": "EXECUTED_FINAL_RELEASE_EVIDENCE",
        "release": {
            "source_sha": SHA,
            "tree_sha": TREE,
            "artifact_name": "release.bin",
            "artifact_digest": DIGEST,
            "application_digest": APP,
            "config_digest": CONFIG,
            "site_map_digest": SITE,
            "profile_manifest_digest": PROFILE,
            "build_container": "espressif/idf:v6.0.1",
            "rollback_enabled": True,
            "compiled_sta_credentials_absent": True,
            "compiled_engineering_prefill_absent": True,
            "bench_auth_bypasses_disabled": True,
        },
        "scope": {
            "industrial_ui": True,
            "generator_transition": True,
            "site_commissioning": True,
            "inverter_profiles": True,
            "ota": True,
            "fat_sat": True,
            "rev_a_hardware_coupled": False,
        },
        "site": {
            "site_id": "SITE-1",
            "configuration_identity": "cfg-1",
            "site_map_digest": SITE,
            "config_digest": CONFIG,
            "power_sign_used_as_source_authority": False,
        },
        "evidence": {
            "industrial_ui": lane(source="3" * 40, mode="governed_replay"),
            "generator_transition": lane(),
            "site_commissioning": lane(),
            "inverter_profiles": lane(),
            "ota": lane(),
            "fat_sat": lane(),
        },
        "rev_a_hardware": {"status": "NOT_COUPLED_TO_THIS_RELEASE"},
        "release_signoff": {
            "zero_critical_blockers": True,
            "fat_sat_signed": True,
            "authorized_representative": "Owner",
            "signed_sat_ref": "sat-1",
            "signed_sat_digest": EVIDENCE,
            "release_evidence_package_ref": "pkg-1",
            "release_evidence_package_digest": DIGEST,
            "unexecuted_template": False,
        },
    }
    inv = rec["evidence"]["inverter_profiles"]
    inv["profile_manifest_digest"] = PROFILE
    inv["profiles"] = [{
        "profile_id": "goodwe.site1",
        "manufacturer": "GoodWe",
        "model": "GW100K-HT",
        "firmware": "FW-1",
        "manual_revision": "R1",
        "manual_ref": "manual-1",
        "production_approved": True,
        "bench_read_write_rollback_passed": True,
        "signed_approval_ref": "approval-1",
        "approval_digest": EVIDENCE,
    }]
    return rec


def evaluate_valid(rec):
    return evaluate(
        rec,
        expected_release_sha=SHA,
        expected_release_tree=TREE,
        expected_artifact_digest=DIGEST,
    )


def assert_fail(mutator, expected, *, expected_sha=SHA, expected_tree=TREE, expected_digest=DIGEST):
    rec = valid_record()
    mutator(rec)
    result = evaluate(
        rec,
        expected_release_sha=expected_sha,
        expected_release_tree=expected_tree,
        expected_artifact_digest=expected_digest,
    )
    assert not result.passed
    assert expected in result.failures, result.failures


def main():
    assert evaluate_valid(valid_record()).passed

    assert_fail(lambda r: r.update({"record_status": "UNEXECUTED_TEMPLATE_NOT_A_RELEASE_PASS"}),
                "record_status_not_executed")
    assert_fail(lambda r: None,
                "release:source_sha_expected_mismatch", expected_sha="9" * 40)
    assert_fail(lambda r: None,
                "release:tree_sha_expected_mismatch", expected_tree="8" * 40)
    assert_fail(lambda r: None,
                "release:artifact_digest_expected_mismatch",
                expected_digest="sha256:" + "7" * 64)
    assert_fail(lambda r: r["evidence"]["ota"].update({"source_sha": "4" * 40}),
                "lane:ota:exact_source_sha_mismatch")
    assert_fail(lambda r: r["evidence"]["fat_sat"].update({"status": "NOT_RUN"}),
                "lane:fat_sat:status_not_pass")
    assert_fail(lambda r: r["evidence"]["industrial_ui"]["final_release_binding"].update({"behavior_affecting_changes": True}),
                "lane:industrial_ui:governed_replay_behavior_change_not_false")
    assert_fail(lambda r: r["evidence"]["generator_transition"]["final_release_binding"].update({"mode": "governed_replay"}),
                "lane:generator_transition:final_lane_must_bind_exact")
    assert_fail(lambda r: r["site"].update({"power_sign_used_as_source_authority": True}),
                "site:power_sign_source_authority_not_forbidden")
    assert_fail(lambda r: r["evidence"]["inverter_profiles"]["profiles"][0].update({"production_approved": False}),
                "lane:inverter_profiles:goodwe.site1:production_approved_not_true")
    assert_fail(lambda r: r["release_signoff"].update({"zero_critical_blockers": False}),
                "signoff:zero_critical_blockers_not_true")

    def couple_reva_without_h4(r):
        r["scope"]["rev_a_hardware_coupled"] = True
        r["rev_a_hardware"] = {
            "status": "PASS",
            "fabricator_dfm_accepted": True,
            "h4_physical_passed": False,
            "h2_checkpoint_sha": "5" * 40,
            "provider_package_digest": DIGEST,
            "h4_evidence_ref": "h4-1",
            "h4_evidence_digest": EVIDENCE,
        }
    assert_fail(couple_reva_without_h4, "rev_a_hardware:h4_physical_not_passed")

    print("release traceability tests: PASS")


if __name__ == "__main__":
    main()
