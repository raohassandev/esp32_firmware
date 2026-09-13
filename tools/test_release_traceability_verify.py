#!/usr/bin/env python3
from copy import deepcopy

from release_traceability_verify import evaluate

SHA = "1" * 40
TREE = "2" * 40
ARTIFACT = "sha256:" + "a" * 64
APP = "sha256:" + "b" * 64
CONFIG = "sha256:" + "c" * 64
SITE_MAP = "sha256:" + "d" * 64
PROFILE = "sha256:" + "e" * 64
UI_EVIDENCE = "sha256:" + "1" * 64
GEN_EVIDENCE = "sha256:" + "2" * 64
SITE_EVIDENCE = "sha256:" + "3" * 64
INV_EVIDENCE = "sha256:" + "4" * 64
OTA_EVIDENCE = "sha256:" + "5" * 64
FAT_EVIDENCE = "sha256:" + "6" * 64
SAT_DIGEST = "sha256:" + "7" * 64
APPROVAL = "sha256:" + "8" * 64
MANUAL = "sha256:" + "9" * 64
QUALIFICATION = "sha256:" + "0" * 64
PACKAGE = "sha256:" + "f" * 64
SITE_ID = "SITE-1"
CONFIG_ID = "cfg-1"

LANE_DIGESTS = {
    "industrial_ui": UI_EVIDENCE,
    "generator_transition": GEN_EVIDENCE,
    "site_commissioning": SITE_EVIDENCE,
    "inverter_profiles": INV_EVIDENCE,
    "ota": OTA_EVIDENCE,
    "fat_sat": FAT_EVIDENCE,
}


def identity():
    return {
        "source_sha": SHA,
        "tree_sha": TREE,
        "artifact_digest": ARTIFACT,
        "application_digest": APP,
        "config_digest": CONFIG,
        "site_map_digest": SITE_MAP,
        "profile_manifest_digest": PROFILE,
    }


def lane(lane_id, source=SHA, mode="exact"):
    if mode == "exact":
        binding = {"mode": "exact", **identity()}
    else:
        binding = {
            "mode": "governed_replay",
            "behavior_affecting_changes": False,
            "replay_ref": "run-1",
            "approval_ref": "approval-1",
            "final_release_checks_passed": True,
            "final_release_identity": identity(),
        }
    return {
        "status": "PASS",
        "source_sha": source,
        "evidence_ref": f"{lane_id}-evidence-1",
        "evidence_digest": LANE_DIGESTS[lane_id],
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
            "artifact_digest": ARTIFACT,
            "application_digest": APP,
            "config_digest": CONFIG,
            "site_map_digest": SITE_MAP,
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
            "site_id": SITE_ID,
            "configuration_identity": CONFIG_ID,
            "site_map_digest": SITE_MAP,
            "config_digest": CONFIG,
            "power_sign_used_as_source_authority": False,
        },
        "evidence": {
            "industrial_ui": lane("industrial_ui", source="3" * 40, mode="governed_replay"),
            "generator_transition": lane("generator_transition"),
            "site_commissioning": lane("site_commissioning"),
            "inverter_profiles": lane("inverter_profiles"),
            "ota": lane("ota"),
            "fat_sat": lane("fat_sat"),
        },
        "rev_a_hardware": {"status": "NOT_COUPLED_TO_THIS_RELEASE"},
        "release_signoff": {
            "zero_critical_blockers": True,
            "fat_sat_signed": True,
            "authorized_representative": "Owner",
            "signed_sat_ref": "sat-1",
            "signed_sat_digest": SAT_DIGEST,
            "release_evidence_package_ref": "pkg-1",
            "release_evidence_package_digest": PACKAGE,
            "signed_release_identity": identity(),
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
        "manual_digest": MANUAL,
        "production_approved": True,
        "bench_read_write_rollback_passed": True,
        "signed_approval_ref": "approval-1",
        "approval_digest": APPROVAL,
        "qualification_evidence_digest": QUALIFICATION,
    }]
    return rec


def evaluate_record(
    rec,
    *,
    expected_sha=SHA,
    expected_tree=TREE,
    artifact=ARTIFACT,
    application=APP,
    config=CONFIG,
    site_map=SITE_MAP,
    profile=PROFILE,
    site_id=SITE_ID,
    config_id=CONFIG_ID,
    lane_digests=None,
    sat=SAT_DIGEST,
):
    return evaluate(
        rec,
        expected_release_sha=expected_sha,
        expected_release_tree=expected_tree,
        expected_artifact_digest=artifact,
        expected_application_digest=application,
        expected_config_digest=config,
        expected_site_map_digest=site_map,
        expected_profile_manifest_digest=profile,
        expected_site_id=site_id,
        expected_configuration_identity=config_id,
        expected_lane_evidence_digests=LANE_DIGESTS if lane_digests is None else lane_digests,
        expected_signed_sat_digest=sat,
    )


def assert_fail(mutator, expected, **kwargs):
    rec = valid_record()
    mutator(rec)
    result = evaluate_record(rec, **kwargs)
    assert not result.passed
    assert expected in result.failures, result.failures


def main():
    assert evaluate_record(valid_record()).passed

    assert_fail(
        lambda r: r.update({"record_status": "UNEXECUTED_TEMPLATE_NOT_A_RELEASE_PASS"}),
        "record_status_not_executed",
    )
    assert_fail(lambda r: None, "release:source_sha_expected_mismatch", expected_sha="9" * 40)
    assert_fail(lambda r: None, "release:tree_sha_expected_mismatch", expected_tree="8" * 40)
    assert_fail(
        lambda r: None,
        "release:artifact_digest_expected_mismatch",
        artifact="sha256:" + "7" * 64,
    )
    assert_fail(
        lambda r: None,
        "release:application_digest_expected_mismatch",
        application="sha256:" + "6" * 64,
    )
    assert_fail(
        lambda r: None,
        "release:config_digest_expected_mismatch",
        config="sha256:" + "5" * 64,
    )
    assert_fail(
        lambda r: None,
        "release:site_map_digest_expected_mismatch",
        site_map="sha256:" + "4" * 64,
    )
    assert_fail(
        lambda r: None,
        "release:profile_manifest_digest_expected_mismatch",
        profile="sha256:" + "3" * 64,
    )
    assert_fail(lambda r: None, "site:site_id_expected_mismatch", site_id="SITE-OTHER")
    assert_fail(
        lambda r: None,
        "site:configuration_identity_expected_mismatch",
        config_id="cfg-other",
    )

    wrong_lanes = dict(LANE_DIGESTS)
    wrong_lanes["ota"] = "sha256:" + "a" * 64
    assert_fail(
        lambda r: None,
        "lane:ota:evidence_digest_expected_mismatch",
        lane_digests=wrong_lanes,
    )

    assert_fail(
        lambda r: r["evidence"]["ota"]["final_release_binding"].update({"tree_sha": "4" * 40}),
        "lane:ota:exact_binding:tree_sha_mismatch",
    )
    assert_fail(
        lambda r: r["evidence"]["site_commissioning"]["final_release_binding"].update(
            {"config_digest": "sha256:" + "9" * 64}
        ),
        "lane:site_commissioning:exact_binding:config_digest_mismatch",
    )
    assert_fail(
        lambda r: r["evidence"]["inverter_profiles"]["final_release_binding"].update(
            {"profile_manifest_digest": "sha256:" + "8" * 64}
        ),
        "lane:inverter_profiles:exact_binding:profile_manifest_digest_mismatch",
    )
    assert_fail(
        lambda r: r["evidence"]["industrial_ui"]["final_release_binding"]["final_release_identity"].update(
            {"site_map_digest": "sha256:" + "7" * 64}
        ),
        "lane:industrial_ui:governed_replay_identity:site_map_digest_mismatch",
    )
    assert_fail(
        lambda r: r["evidence"]["generator_transition"]["final_release_binding"].update(
            {"mode": "governed_replay"}
        ),
        "lane:generator_transition:final_lane_must_bind_exact",
    )
    assert_fail(
        lambda r: r["evidence"]["fat_sat"].update({"status": "NOT_RUN"}),
        "lane:fat_sat:status_not_pass",
    )

    assert_fail(
        lambda r: r["evidence"]["inverter_profiles"]["profiles"][0].update(
            {"production_approved": False}
        ),
        "lane:inverter_profiles:goodwe.site1:production_approved_not_true",
    )
    assert_fail(
        lambda r: r["evidence"]["inverter_profiles"]["profiles"][0].update(
            {"manual_digest": ""}
        ),
        "lane:inverter_profiles:goodwe.site1:manual_digest_invalid",
    )
    assert_fail(
        lambda r: r["evidence"]["inverter_profiles"]["profiles"][0].update(
            {"qualification_evidence_digest": ""}
        ),
        "lane:inverter_profiles:goodwe.site1:qualification_evidence_digest_invalid",
    )

    assert_fail(
        lambda r: None,
        "signoff:signed_sat_digest_expected_mismatch",
        sat="sha256:" + "1" * 64,
    )
    assert_fail(
        lambda r: r["release_signoff"]["signed_release_identity"].update(
            {"application_digest": "sha256:" + "2" * 64}
        ),
        "signoff:signed_release_identity:application_digest_mismatch",
    )
    assert_fail(
        lambda r: r["release_signoff"].update({"zero_critical_blockers": False}),
        "signoff:zero_critical_blockers_not_true",
    )

    assert_fail(
        lambda r: r["site"].update({"power_sign_used_as_source_authority": True}),
        "site:power_sign_source_authority_not_forbidden",
    )

    def couple_reva_without_h4(r):
        r["scope"]["rev_a_hardware_coupled"] = True
        r["rev_a_hardware"] = {
            "status": "PASS",
            "fabricator_dfm_accepted": True,
            "h4_physical_passed": False,
            "h2_checkpoint_sha": "5" * 40,
            "provider_package_digest": ARTIFACT,
            "h4_evidence_ref": "h4-1",
            "h4_evidence_digest": UI_EVIDENCE,
        }

    assert_fail(couple_reva_without_h4, "rev_a_hardware:h4_physical_not_passed")

    print("release traceability tests: PASS")


if __name__ == "__main__":
    main()
