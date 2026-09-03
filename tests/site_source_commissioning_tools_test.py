#!/usr/bin/env python3
import copy
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
    }


def proof(before, after, before_state: str, after_state: str):
    return {
        "started_at": "2026-09-03T10:00:00+05:00",
        "ended_at": "2026-09-03T10:03:00+05:00",
        "physical_before": before_state,
        "physical_after": after_state,
        "raw_before": before,
        "raw_after": after,
        "runtime_before": before_state,
        "runtime_after": after_state,
        "toggle_evidence_ref": "video/photo-log-ref-01",
        "hmi_api_observation_ref": "api-hmi-capture-ref-01",
        "semantic_match": True,
        "stale_test": {
            "performed": True,
            "fail_closed": True,
            "evidence_ref": "stale-evidence-log-ref-01"
        },
        "recovery_test": {
            "performed": True,
            "authority_returned_early": False,
            "observed_dwell_ms": 5000,
            "evidence_ref": "recovery-dwell-log-ref-01"
        },
        "persistence": {
            "written": True,
            "readback_match": True,
            "evidence_ref": "config-readback-ref-01"
        },
        "pass": True,
        "pass_reason": "Physical state, raw evidence and runtime semantic matched with fail-closed stale recovery proof."
    }


def base_channel(channel_id: str, semantic: str):
    return {
        "id": channel_id,
        "semantic": semantic,
        "required": True,
        "qualification_status": "pass",
        "provenance": {
            "signal_source": "breaker auxiliary or controller source",
            "manufacturer": "Qualified Manufacturer",
            "model": "Qualified Model",
            "manual_revision": "Manual Rev 1.0 page 12",
            "wiring_drawing_ref": "Site drawing WD-01 Rev B",
            "site_sld_ref": "SLD-01 Rev C"
        },
        "timing": {
            "debounce_ms": 250,
            "stale_ms": 3000,
            "recovery_ms": 5000,
            "authority_ref": "manufacturer/site commissioning evidence ref"
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
        "not_supported_reason": "Approved SLD and ATS scheme do not permit synchronized grid-generator operation.",
        "topology_ref": "SLD-01 Rev C / ATS interlock drawing WD-04"
    }

    return {
        "schema": 1,
        "site_id": "SITE-A",
        "firmware_sha": FW,
        "artifact_digest": DIGEST,
        "config_identity": "config-sha256-123456",
        "site_sld_ref": "SLD-01 Rev C",
        "power_sign_used_as_source_authority": False,
        "automatic_control_enabled_during_commissioning": False,
        "required_channel_ids": ["grid_breaker", "generator_run"],
        "channels": [grid, gen, sync],
        "configuration_acceptance": {
            "readback_exact_match": True,
            "unqualified_blocks_automatic_control": True,
            "stale_missing_conflict_fail_closed": True,
            "physical_toggle_maps_expected_mode": True,
            "contradictory_power_sign_cannot_override_contacts": True,
            "evidence_ref": "commissioning-gate-capture-ref-01"
        }
    }


def main() -> None:
    good = MOD.evaluate(pass_record(), FW, DIGEST)
    assert good.passed, good.failures
    assert good.channels_seen == ["generator_run", "grid_breaker", "synchronism"]

    wrong_identity = MOD.evaluate(pass_record(), "c" * 40, DIGEST)
    assert not wrong_identity.passed
    assert "firmware_sha_mismatch" in wrong_identity.failures

    power_sign = pass_record()
    power_sign["power_sign_used_as_source_authority"] = True
    assert "power_sign_source_authority_not_forbidden" in MOD.evaluate(power_sign, FW, DIGEST).failures

    unqualified = pass_record()
    unqualified["channels"][0]["qualification_status"] = "unqualified"
    result = MOD.evaluate(unqualified, FW, DIGEST)
    assert not result.passed
    assert "channel:grid_breaker:required_channel_unqualified" in result.failures
    assert "required_channel_not_pass:grid_breaker" in result.failures

    guessed_manual = pass_record()
    guessed_manual["channels"][1]["provenance"]["manual_revision"] = ""
    result = MOD.evaluate(guessed_manual, FW, DIGEST)
    assert not result.passed
    assert "channel:generator_run:provenance_field_missing:manual_revision" in result.failures

    early = pass_record()
    early["channels"][0]["commissioning"]["recovery_test"]["authority_returned_early"] = True
    result = MOD.evaluate(early, FW, DIGEST)
    assert not result.passed
    assert "channel:grid_breaker:authority_returned_early_not_false" in result.failures

    modbus_missing = pass_record()
    del modbus_missing["channels"][1]["mapping"]["address"]
    result = MOD.evaluate(modbus_missing, FW, DIGEST)
    assert not result.passed
    assert "channel:generator_run:address_missing" in result.failures

    required_not_supported = pass_record()
    required_not_supported["channels"][2]["required"] = True
    required_not_supported["required_channel_ids"].append("synchronism")
    result = MOD.evaluate(required_not_supported, FW, DIGEST)
    assert not result.passed
    assert "channel:synchronism:required_channel_not_supported" in result.failures

    no_readback = pass_record()
    no_readback["configuration_acceptance"]["readback_exact_match"] = False
    result = MOD.evaluate(no_readback, FW, DIGEST)
    assert not result.passed
    assert "configuration_acceptance_not_proven:readback_exact_match" in result.failures

    print("Site source commissioning evidence tests passed")


if __name__ == "__main__":
    main()
