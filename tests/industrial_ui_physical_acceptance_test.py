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
        "roles_exercised": ["operator", "engineering"],
        "routes_exercised": [
            "dashboard", "meters", "inverters", "alarms", "readiness",
            "engineering", "commissioning", "system",
        ],
    }
    for key in MOD.REQUIRED_VISUAL_FLAGS + MOD.REQUIRED_TOUCH_FLAGS + MOD.REQUIRED_RUNTIME_FLAGS:
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

    wrong_tree = evaluate(observations(tree_sha="e" * 40))
    assert not wrong_tree.passed and "identity_mismatch:tree_sha" in wrong_tree.failures

    wrong_app = evaluate(observations(application_sha256="f" * 64))
    assert not wrong_app.passed and "identity_mismatch:application_sha256" in wrong_app.failures

    visual = evaluate(observations(engineering_hierarchy_ok=False))
    assert not visual.passed and "visual_not_passed:engineering_hierarchy_ok" in visual.failures

    runtime = evaluate(observations(browser_lockout_absent=False))
    assert not runtime.passed and "runtime_not_passed:browser_lockout_absent" in runtime.failures

    roles = evaluate(observations(roles_exercised=["operator"]))
    assert not roles.passed and "roles_exercised_must_equal_operator_and_engineering" in roles.failures

    routes = observations()
    routes["routes_exercised"] = [x for x in routes["routes_exercised"] if x != "commissioning"]
    missing_route = evaluate(routes)
    assert not missing_route.passed
    assert "routes_missing:commissioning" in missing_route.failures

    touch = evaluate(observations(touch_responsive=False))
    assert not touch.passed
    assert "waveshare:physical_not_passed:touch_responsive" in touch.failures

    short = evaluate(log=serial_log(minutes=10))
    assert not short.passed
    assert any(x.startswith("waveshare:serial:screen_soak=") or x.startswith("waveshare:serial:runtime=") for x in short.failures)

    wdt = evaluate(log=serial_log() + "\nE (14500000) task_wdt: Task watchdog got triggered")
    assert not wdt.passed
    assert any(x.startswith("waveshare:serial:fatal:task_wdt") for x in wdt.failures)

    template = json.loads((ROOT / "evidence/templates/industrial_ui_physical_observations.json").read_text())
    assert template["candidate_sha"] == "FILL_EXACT_SOURCE_SHA"
    assert template["overview_layout_ok"] is False
    assert template["browser_lockout_absent"] is False

    print("Industrial UI physical acceptance tool tests passed")


if __name__ == "__main__":
    main()
