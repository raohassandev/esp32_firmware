#!/usr/bin/env python3
import importlib.util
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "site_source_commissioning_verify", ROOT / "tools" / "site_source_commissioning_verify.py"
)
MOD = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
sys.modules[SPEC.name] = MOD
SPEC.loader.exec_module(MOD)

FW = "a" * 40
DIGEST = "sha256:" + "b" * 64
SITE = "SITE-A"
CONFIG = "config-sha256-123456"
SLD_DIGEST = "sha256:" + "c" * 64
MAP_DIGEST = "sha256:" + "d" * 64
PACKAGE_DIGEST = "sha256:" + "e" * 64
TOGGLE_DIGEST = "sha256:" + "f" * 64
STALE_DIGEST = "sha256:" + "1" * 64
RECOVERY_DIGEST = "sha256:" + "2" * 64
PERSIST_DIGEST = "sha256:" + "3" * 64
CONFIG_GATE_DIGEST = "sha256:" + "4" * 64


def evaluate(rec, site=SITE, config=CONFIG, sld_digest=SLD_DIGEST, map_digest=MAP_DIGEST):
    return MOD.evaluate(rec, FW, DIGEST, site, config, sld_digest, map_digest)


def meter(meter_id: str, role: str):
    return {
        "applicable": True,
        "meter_id": meter_id,
        "role": role,
        "ct_pt_polarity_ref": "commissioning-sheet-M1",
        "data_type": "S32",
        "word_order": "ABCD",
        "scale": 0.001,
        "raw": 125000,
        "scaled_kw": 125.0,
        "sign_convention": "positive means import/source-to-bus for this commissioned role",
        "known_physical_direction": "known positive load supplied toward bus",
        "independent_reference": "portable-meter/photo-ref-01",
        "sign_proven": True,
        "connection": {
            "applicable": True,
            "endpoint": "192.168.10.10:502",
            "unit_id": 1,
        },
    }


def proof(before, after, before_state: str, after_state: str):
    return {
        "site_id": SITE,
        "config_identity": CONFIG,
        "channel_map_digest": MAP_DIGEST,
        "started_at": "2026-09-03T10:00:00+05:00",
        "ended_at": "2026-09-03T10:03:00+05:00",
        "physical_before": before_state,
        "physical_after": after_state,
        "raw_before": before,
        "raw_after": after,
        "runtime_before": before_state,
        "runtime_after": after_state,
        "toggle_evidence_ref": "video/photo-log-ref-01",
        "toggle_evidence_digest": TOGGLE_DIGEST,
        "hmi_api_observation_ref": "api-hmi-capture-ref-01",
        "semantic_match": True,
        "stale_test": {
            "performed": True,
            "fail_closed": True,
            "observed_fail_closed_state": "source authority removed and control held fail closed",
            "evidence_ref": "stale-evidence-log-ref-01",
            "evidence_digest": STALE_DIGEST,
        },
        "recovery_test": {
            "performed": True,
            "authority_returned_early": False,
            "observed_dwell_ms": 5000,
            "evidence_ref": "recovery-dwell-log-ref-01",
            "evidence_digest": RECOVERY_DIGEST,
        },
        "persistence": {
            "written": True,
            "readback_match": True,
            "config_identity_readback": CONFIG,
            "channel_map_digest_readback": MAP_DIGEST,
            "evidence_ref": "config-readback-ref-01",
            "evidence_digest": PERSIST_DIGEST,
        },
        "pass": True,
        "pass_reason": "Physical state, raw evidence and runtime semantic matched with fail-closed stale recovery proof.",
    }


def base_channel(channel_id: str, semantic: str):
    return {
        "id": channel_id,
        "semantic": semantic,
        "required": True,
        "qualification_status": "pass",
        "site_id": SITE,
        "config_identity": CONFIG,
        "channel_map_digest": MAP_DIGEST,
        "provenance": {
            "signal_source": "breaker auxiliary or controller source",
            "manufacturer": "Qualified Manufacturer",
            "model": "Qualified Model",
            "manual_revision": "Manual Rev 1.0 page 12",
            "wiring_drawing_ref": "Site drawing WD-01 Rev B",
            "site_sld_ref": "SLD-01 Rev C",
        },
        "timing": {
            "debounce_ms": 250,
            "stale_ms": 3000,
            "recovery_ms": 5000,
            "authority_ref": "manufacturer/site commissioning evidence ref",
        },
        "power_sign_used_as_state_authority": False,
    }


def pass_record():
    grid = base_channel("grid_breaker", "grid breaker carrying-state evidence")
    grid["mapping"] = {
        "kind": "hardwired",
        "terminal_wire_ref": "TB1-01 wire GCB-AUX-13",
        "input_channel": "DI1",
        "active_raw": 1,
        "inactive_raw": 0,
    }
    grid["meter"] = meter("M-GRID", "grid")
    grid["commissioning"] = proof(0, 1, "grid breaker open", "grid breaker closed")

    gen = base_channel("generator_run", "generator run/carrying-state evidence")
    gen["mapping"] = {
        "kind": "modbus",
        "endpoint": "192.168.10.20:502",
        "unit_id": 1,
        "function_code": 3,
        "address": "40100",
        "bit_mask_or_value": "0x0001",
        "active_raw": 1,
        "inactive_raw": 0,
    }
    gen["meter"] = meter("M-GEN", "generator")
    gen["commissioning"] = proof(0, 1, "generator stopped", "generator running and carrying")

    sync = {
        "id": "synchronism",
        "semantic": "grid-generator synchronism evidence",
        "required": False,
        "qualification_status": "not_supported",
        "site_id": SITE,
        "config_identity": CONFIG,
        "channel_map_digest": MAP_DIGEST,
        "not_supported_reason": "Approved SLD and ATS scheme do not permit synchronized grid-generator operation.",
        "topology_ref": "SLD-01 Rev C / ATS interlock drawing WD-04",
    }

    return {
        "schema": 2,
        "evidence_state": "EXECUTED_SITE_SOURCE_COMMISSIONING_EVIDENCE",
        "site_id": SITE,
        "firmware_sha": FW,
        "artifact_digest": DIGEST,
        "config_identity": CONFIG,
        "site_sld_ref": "SLD-01 Rev C",
        "site_sld_digest": SLD_DIGEST,
        "channel_map_ref": "site-source-channel-map-rev-01",
        "channel_map_digest": MAP_DIGEST,
        "evidence_package_ref": "site-source-commissioning-package-01",
        "evidence_package_digest": PACKAGE_DIGEST,
        "power_sign_used_as_source_authority": False,
        "automatic_control_enabled_during_commissioning": False,
        "required_channel_ids": ["grid_breaker", "generator_run"],
        "channels": [grid, gen, sync],
        "configuration_acceptance": {
            "site_id": SITE,
            "config_identity": CONFIG,
            "channel_map_digest": MAP_DIGEST,
            "readback_exact_match": True,
            "unqualified_blocks_automatic_control": True,
            "stale_missing_conflict_fail_closed": True,
            "physical_toggle_maps_expected_mode": True,
            "contradictory_power_sign_cannot_override_contacts": True,
            "evidence_ref": "commissioning-gate-capture-ref-01",
            "evidence_digest": CONFIG_GATE_DIGEST,
        },
    }


def assert_failure(rec, expected: str, **kwargs):
    result = evaluate(rec, **kwargs)
    assert not result.passed, "record unexpectedly passed"
    assert expected in result.failures, result.failures


def main() -> None:
    good = evaluate(pass_record())
    assert good.passed, good.failures
    assert good.channels_seen == ["generator_run", "grid_breaker", "synchronism"]

    r = pass_record()
    r["evidence_state"] = "UNEXECUTED_TEMPLATE_NOT_PHYSICAL_EVIDENCE"
    assert_failure(r, "evidence_state_not_executed")

    assert_failure(pass_record(), "site_id_mismatch", site="SITE-B")
    assert_failure(pass_record(), "config_identity_mismatch", config="other-config")
    assert_failure(pass_record(), "site_sld_digest_mismatch", sld_digest="sha256:" + "5" * 64)
    assert_failure(pass_record(), "channel_map_digest_mismatch", map_digest="sha256:" + "6" * 64)

    wrong_fw = MOD.evaluate(pass_record(), "7" * 40, DIGEST, SITE, CONFIG, SLD_DIGEST, MAP_DIGEST)
    assert not wrong_fw.passed
    assert "firmware_sha_mismatch" in wrong_fw.failures

    pasted = pass_record()
    pasted["channels"][0]["site_id"] = "SITE-B"
    assert_failure(pasted, "channel:grid_breaker:site_id_mismatch")

    pasted_proof = pass_record()
    pasted_proof["channels"][1]["commissioning"]["channel_map_digest"] = "sha256:" + "7" * 64
    assert_failure(pasted_proof, "channel:generator_run:commissioning:channel_map_digest_mismatch")

    power_sign = pass_record()
    power_sign["power_sign_used_as_source_authority"] = True
    assert_failure(power_sign, "power_sign_source_authority_not_forbidden")

    unqualified = pass_record()
    unqualified["channels"][0]["qualification_status"] = "unqualified"
    result = evaluate(unqualified)
    assert not result.passed
    assert "channel:grid_breaker:required_channel_unqualified" in result.failures
    assert "required_channel_not_pass:grid_breaker" in result.failures

    guessed_manual = pass_record()
    guessed_manual["channels"][1]["provenance"]["manual_revision"] = ""
    assert_failure(guessed_manual, "channel:generator_run:provenance_field_missing:manual_revision")

    wrong_sld_ref = pass_record()
    wrong_sld_ref["channels"][0]["provenance"]["site_sld_ref"] = "OTHER-SLD"
    assert_failure(wrong_sld_ref, "channel:grid_breaker:provenance_site_sld_ref_mismatch")

    no_physical_change = pass_record()
    no_physical_change["channels"][0]["commissioning"]["physical_after"] = "grid breaker open"
    assert_failure(no_physical_change, "channel:grid_breaker:physical_toggle_no_state_change")

    no_runtime_change = pass_record()
    no_runtime_change["channels"][0]["commissioning"]["runtime_after"] = "grid breaker open"
    assert_failure(no_runtime_change, "channel:grid_breaker:runtime_toggle_no_state_change")

    bad_meter_scale = pass_record()
    bad_meter_scale["channels"][0]["meter"]["scaled_kw"] = 999.0
    assert_failure(bad_meter_scale, "channel:grid_breaker:meter_raw_scale_mismatch")

    early = pass_record()
    early["channels"][0]["commissioning"]["recovery_test"]["authority_returned_early"] = True
    assert_failure(early, "channel:grid_breaker:authority_returned_early_not_false")

    short_dwell = pass_record()
    short_dwell["channels"][0]["commissioning"]["recovery_test"]["observed_dwell_ms"] = 4999
    assert_failure(short_dwell, "channel:grid_breaker:recovery_dwell_below_configured_authority")

    bad_readback = pass_record()
    bad_readback["channels"][0]["commissioning"]["persistence"]["config_identity_readback"] = "other-config"
    assert_failure(bad_readback, "channel:grid_breaker:config_identity_readback_mismatch")

    modbus_missing = pass_record()
    del modbus_missing["channels"][1]["mapping"]["address"]
    assert_failure(modbus_missing, "channel:generator_run:address_missing")

    bad_unit = pass_record()
    bad_unit["channels"][1]["mapping"]["unit_id"] = 248
    assert_failure(bad_unit, "channel:generator_run:unit_id_invalid")

    required_not_supported = pass_record()
    required_not_supported["channels"][2]["required"] = True
    required_not_supported["required_channel_ids"].append("synchronism")
    result = evaluate(required_not_supported)
    assert not result.passed
    assert "channel:synchronism:required_channel_not_supported" in result.failures

    no_readback = pass_record()
    no_readback["configuration_acceptance"]["readback_exact_match"] = False
    assert_failure(no_readback, "configuration_acceptance_not_proven:readback_exact_match")

    bad_gate_binding = pass_record()
    bad_gate_binding["configuration_acceptance"]["channel_map_digest"] = "sha256:" + "8" * 64
    assert_failure(bad_gate_binding, "configuration_acceptance_channel_map_digest_mismatch")

    print("Site source commissioning evidence tests passed")


if __name__ == "__main__":
    main()
