#!/usr/bin/env python3
import importlib.util
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))
SPEC = importlib.util.spec_from_file_location(
    "industrial_ui_physical_acceptance", TOOLS / "industrial_ui_physical_acceptance.py"
)
MOD = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
sys.modules[SPEC.name] = MOD
SPEC.loader.exec_module(MOD)

SHA = "a" * 40
TREE = "b" * 40
DIGEST = "sha256:" + "c" * 64
APP = "d" * 64


def serial_log(minutes=241):
    lines = [
        "I (1000) app: Before LCD DMA reservation: internal DMA free=192323 largest=180000",
        "I (2000) app: After LCD DMA reservation: internal DMA free=120000 largest=100000",
        "I (3000) app: Before Product Core init: internal DMA free=119000 largest=99000",
        "I (5000) core: After Product Core init: internal DMA free=60000 largest=50000",
        "I (5050) waveshare_product: Shared Product Core started",
        "I (5100) waveshare_product: Espressif flash dispatcher ready; PSRAM-stacked HMI persistence is routed through internal RAM",
    ]
    for stage in range(1, 7):
        lines.append(f"I ({6000 + stage * 100}) lcd: LVGL activation stage {stage}/6")
    lines += [
        "I (6700) lcd: Native LCD/LVGL/touch ready",
        "I (7000) app: After LVGL/UI activation: internal DMA free=44799 largest=34816",
        "I (7100) waveshare_product: Local Engineering commissioning backend bound to touchscreen",
        "I (7200) waveshare_product: Local source-evidence commissioning backend bound to touchscreen",
        "I (7300) waveshare_product: Screen refresh task created in PSRAM",
    ]
    for minute in range(1, minutes + 1):
        ms = 7000 + minute * 60000
        lines.append(
            f"I ({ms}) lcd: Screen soak: heap free=210000 min=180000 | "
            "PSRAM free=7000000 largest=6900000 | DMA free=43000 largest=33000 | "
            "screen stack hwm=6800"
        )
    return "\n".join(lines)


def observations(**overrides):
    data = {
        "candidate_sha": SHA,
        "tree_sha": TREE,
        "artifact_digest": DIGEST,
        "application_sha256": APP,
        "page_cycles": 20,
        "alarms_opened": True,
        "touch_responsive": True,
        "sweep_absent": True,
        "reload_absent": True,
        "tear_or_corruption_absent": True,
        "wifi_connected_rssi_dbm": -55,
        "source_transfer_configured": True,
        "source_transfer_mapping_roundtrip_ok": True,
        "source_transfer_not_configured_reason": "",
        "source_sync_configured": True,
        "source_sync_mapping_roundtrip_ok": True,
        "source_sync_not_configured_reason": "",
        "modbus_request_count_before": 10,
        "modbus_request_count_after": 30,
        "modbus_success_count_before": 9,
        "modbus_success_count_after": 28,
        "modbus_decoded_sample_count": 5,
        "roles_exercised": ["operator", "engineering"],
        "routes_exercised": [
            "dashboard", "meters", "inverters", "alarms", "readiness",
            "engineering", "commissioning", "network", "system",
        ],
    }
    groups = (
        MOD.REQUIRED_VISUAL_FLAGS,
        MOD.REQUIRED_TOUCH_FLAGS,
        MOD.REQUIRED_RUNTIME_FLAGS,
        MOD.REQUIRED_NETWORK_FLAGS,
        MOD.REQUIRED_SOURCE_COMMISSIONING_FLAGS,
        MOD.REQUIRED_ALARM_FLAGS,
        MOD.REQUIRED_MODBUS_FLAGS,
    )
    for group in groups:
        for key in group:
            data[key] = True
    data.update(overrides)
    return data


def evaluate(obs=None, log=None):
    return MOD.evaluate(
        serial_log() if log is None else log,
        observations() if obs is None else obs,
        expected_candidate_sha=SHA,
        expected_tree_sha=TREE,
        expected_artifact_digest=DIGEST,
        expected_application_sha256=APP,
    )


def main():
    good = evaluate()
    assert good.passed, good.failures
    assert good.base_waveshare["serial"]["observed_runtime_seconds"] >= 14400
    assert good.base_waveshare["serial"]["soak_samples"] >= 240
    assert good.modbus["passed"] is True

    wrong_tree = evaluate(observations(tree_sha="e" * 40))
    assert not wrong_tree.passed and "identity_mismatch:tree_sha" in wrong_tree.failures

    wrong_app = evaluate(observations(application_sha256="f" * 64))
    assert not wrong_app.passed and "identity_mismatch:application_sha256" in wrong_app.failures

    visual = evaluate(observations(engineering_hierarchy_ok=False))
    assert not visual.passed and "visual_not_passed:engineering_hierarchy_ok" in visual.failures

    runtime = evaluate(observations(browser_lockout_absent=False))
    assert not runtime.passed and "runtime_not_passed:browser_lockout_absent" in runtime.failures

    network = evaluate(observations(network_scan_ok=False))
    assert not network.passed and "network_not_passed:network_scan_ok" in network.failures

    invalid_rssi = evaluate(observations(wifi_connected_rssi_dbm=None))
    assert not invalid_rssi.passed and "network_invalid:wifi_connected_rssi_dbm" in invalid_rssi.failures

    source = evaluate(observations(source_gen3_mapping_roundtrip_ok=False))
    assert not source.passed
    assert "source_commissioning_not_passed:source_gen3_mapping_roundtrip_ok" in source.failures

    transfer_required = evaluate(observations(source_transfer_mapping_roundtrip_ok=False))
    assert not transfer_required.passed
    assert "source_commissioning_not_passed:source_transfer_mapping_roundtrip_ok" in transfer_required.failures

    transfer_absent = evaluate(observations(
        source_transfer_configured=False,
        source_transfer_mapping_roundtrip_ok=False,
        source_transfer_not_configured_reason="Bench topology has no Transfer/ATS evidence channel",
    ))
    assert transfer_absent.passed, transfer_absent.failures

    transfer_missing_reason = evaluate(observations(
        source_transfer_configured=False,
        source_transfer_mapping_roundtrip_ok=False,
        source_transfer_not_configured_reason="",
    ))
    assert not transfer_missing_reason.passed
    assert (
        "source_commissioning_missing_not_configured_reason:source_transfer_not_configured_reason"
        in transfer_missing_reason.failures
    )

    transfer_inconsistent = evaluate(observations(
        source_transfer_configured=False,
        source_transfer_mapping_roundtrip_ok=True,
        source_transfer_not_configured_reason="Bench topology has no Transfer/ATS evidence channel",
    ))
    assert not transfer_inconsistent.passed
    assert (
        "source_commissioning_inconsistent_not_configured:source_transfer_mapping_roundtrip_ok"
        in transfer_inconsistent.failures
    )

    sync_absent = evaluate(observations(
        source_sync_configured=False,
        source_sync_mapping_roundtrip_ok=False,
        source_sync_not_configured_reason="Bench topology does not support synchronized Grid+Generator operation",
    ))
    assert sync_absent.passed, sync_absent.failures

    invalid_optional_applicability = evaluate(observations(source_sync_configured=None))
    assert not invalid_optional_applicability.passed
    assert (
        "source_commissioning_invalid_applicability:source_sync_configured"
        in invalid_optional_applicability.failures
    )

    configured_with_skip_reason = evaluate(observations(
        source_sync_configured=True,
        source_sync_mapping_roundtrip_ok=True,
        source_sync_not_configured_reason="not really absent",
    ))
    assert not configured_with_skip_reason.passed
    assert (
        "source_commissioning_inconsistent_configured_reason:source_sync_not_configured_reason"
        in configured_with_skip_reason.failures
    )

    alarm = evaluate(observations(alarm_operator_ack_refused=False))
    assert not alarm.passed and "alarm_not_passed:alarm_operator_ack_refused" in alarm.failures

    modbus_flag = evaluate(observations(board_modbus_simulator_connected=False))
    assert not modbus_flag.passed
    assert "modbus:modbus_not_passed:board_modbus_simulator_connected" in modbus_flag.failures

    no_requests = evaluate(observations(modbus_request_count_before=10, modbus_request_count_after=10))
    assert not no_requests.passed and "modbus:modbus_request_count_did_not_increase" in no_requests.failures

    no_success = evaluate(observations(modbus_success_count_before=9, modbus_success_count_after=9))
    assert not no_success.passed and "modbus:modbus_success_count_did_not_increase" in no_success.failures

    impossible_success = evaluate(observations(modbus_request_count_after=20, modbus_success_count_after=21))
    assert not impossible_success.passed
    assert "modbus:modbus_success_count_exceeds_request_count" in impossible_success.failures

    low_samples = evaluate(observations(modbus_decoded_sample_count=2))
    assert not low_samples.passed and "modbus:modbus_decoded_sample_count_below_3" in low_samples.failures

    invalid_counter = evaluate(observations(modbus_request_count_after=True))
    assert not invalid_counter.passed
    assert "modbus:modbus_invalid_counter:modbus_request_count_after" in invalid_counter.failures

    roles = evaluate(observations(roles_exercised=["operator"]))
    assert not roles.passed and "roles_exercised_must_equal_operator_and_engineering" in roles.failures

    routes = observations()
    routes["routes_exercised"] = [x for x in routes["routes_exercised"] if x != "commissioning"]
    missing_route = evaluate(routes)
    assert not missing_route.passed
    assert "routes_missing:commissioning" in missing_route.failures

    network_route = observations()
    network_route["routes_exercised"] = [x for x in network_route["routes_exercised"] if x != "network"]
    missing_network = evaluate(network_route)
    assert not missing_network.passed
    assert "routes_missing:network" in missing_network.failures

    touch = evaluate(observations(touch_responsive=False))
    assert not touch.passed
    assert "waveshare:physical_not_passed:touch_responsive" in touch.failures

    short = evaluate(log=serial_log(minutes=10))
    assert not short.passed
    assert any(x.startswith("waveshare:serial:screen_soak=") or x.startswith("waveshare:serial:runtime=") for x in short.failures)

    wdt = evaluate(log=serial_log() + "\nE (14500000) task_wdt: Task watchdog got triggered")
    assert not wdt.passed
    assert any(x.startswith("waveshare:serial:fatal:task_wdt") for x in wdt.failures)

    # Legacy evidence that satisfied the original #175 validator must fail v3
    # until the new physical surfaces and optional-topology applicability are
    # actually exercised/declared.
    legacy = observations()
    for group in (
        MOD.REQUIRED_NETWORK_FLAGS,
        MOD.REQUIRED_SOURCE_COMMISSIONING_FLAGS,
        MOD.REQUIRED_ALARM_FLAGS,
        MOD.REQUIRED_MODBUS_FLAGS,
    ):
        for key in group:
            legacy.pop(key, None)
    for key in (
        "source_transfer_configured", "source_transfer_mapping_roundtrip_ok",
        "source_transfer_not_configured_reason", "source_sync_configured",
        "source_sync_mapping_roundtrip_ok", "source_sync_not_configured_reason",
        "wifi_connected_rssi_dbm", "modbus_request_count_before", "modbus_request_count_after",
        "modbus_success_count_before", "modbus_success_count_after", "modbus_decoded_sample_count",
    ):
        legacy.pop(key, None)
    legacy["routes_exercised"] = [x for x in legacy["routes_exercised"] if x != "network"]
    old_evidence = evaluate(legacy)
    assert not old_evidence.passed
    assert any(x.startswith("network_not_passed:") for x in old_evidence.failures)
    assert any(x.startswith("source_commissioning_not_passed:") for x in old_evidence.failures)
    assert any(x.startswith("source_commissioning_invalid_applicability:") for x in old_evidence.failures)
    assert any(x.startswith("alarm_not_passed:") for x in old_evidence.failures)
    assert any(x.startswith("modbus:") for x in old_evidence.failures)

    template = json.loads((ROOT / "evidence/templates/industrial_ui_physical_observations.json").read_text())
    assert template["candidate_sha"] == "FILL_EXACT_SOURCE_SHA"
    assert template["overview_layout_ok"] is False
    assert template["browser_lockout_absent"] is False
    assert template["network_scan_ok"] is False
    assert template["source_gen3_mapping_roundtrip_ok"] is False
    assert template["source_transfer_configured"] is None
    assert template["source_transfer_mapping_roundtrip_ok"] is False
    assert template["source_transfer_not_configured_reason"] == ""
    assert template["source_sync_configured"] is None
    assert template["source_sync_mapping_roundtrip_ok"] is False
    assert template["source_sync_not_configured_reason"] == ""
    assert template["alarm_sort_priority_ok"] is False
    assert template["modbus_request_count_after"] == 0
    assert template["modbus_decoded_sample_count"] == 0

    candidate = json.loads((ROOT / "evidence/candidates/industrial_ui_72a1a82_physical_observations.json").read_text())
    assert candidate["evidence_state"] == "UNEXECUTED_TEMPLATE_NOT_A_PHYSICAL_PASS"
    assert candidate["candidate_sha"] == "72a1a82a8fc5ad4406b5bd51fba1f80f9c182884"
    assert candidate["tree_sha"] == "3069c65b4234fcd2b6418f9bbbe7859f1cd9abce"
    assert candidate["artifact_id"] == 10293685030
    assert candidate["artifact_digest"] == "sha256:44dc05fe2c6e61d3a8b5fdfc7c936937da948691a2038358d5c0b3c1008de541"
    assert candidate["application_sha256"] == "0bbdb75be4ea7c0337f07e83dbdd3e34736ce8f667a42aa11abeb5c638f60734"
    assert candidate["bench_engineering_http_auth_bypass"] is False
    assert candidate["bench_network_page_auth_bypass"] is False
    for group in (
        MOD.REQUIRED_VISUAL_FLAGS, MOD.REQUIRED_TOUCH_FLAGS, MOD.REQUIRED_RUNTIME_FLAGS,
        MOD.REQUIRED_NETWORK_FLAGS, MOD.REQUIRED_SOURCE_COMMISSIONING_FLAGS,
        MOD.REQUIRED_ALARM_FLAGS, MOD.REQUIRED_MODBUS_FLAGS,
    ):
        for key in group:
            assert candidate[key] is False, key
    assert candidate["source_transfer_configured"] is None
    assert candidate["source_transfer_mapping_roundtrip_ok"] is False
    assert candidate["source_transfer_not_configured_reason"] == ""
    assert candidate["source_sync_configured"] is None
    assert candidate["source_sync_mapping_roundtrip_ok"] is False
    assert candidate["source_sync_not_configured_reason"] == ""

    print("Industrial UI physical acceptance v3 tool tests passed")


if __name__ == "__main__":
    main()
