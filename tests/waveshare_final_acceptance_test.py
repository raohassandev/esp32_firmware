#!/usr/bin/env python3
import importlib.util
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))
SPEC = importlib.util.spec_from_file_location("waveshare_final_acceptance", TOOLS / "waveshare_final_acceptance.py")
MOD = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
sys.modules[SPEC.name] = MOD
SPEC.loader.exec_module(MOD)

SHA = "87841ecee727fe1d814d4186be8c8c26e4afafb4"
DIGEST = "sha256:89e621034d4c91096fc5d38dd57ac40eeeab34275e4af1fc0461b48575039096"


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
            f"PSRAM free=7000000 largest=6900000 | DMA free=43000 largest=33000 | "
            f"screen stack hwm=6800"
        )
    return "\n".join(lines)


def observations(**overrides):
    data = {
        "candidate_sha": SHA,
        "artifact_digest": DIGEST,
        "page_cycles": 20,
        "alarms_opened": True,
        "touch_responsive": True,
        "sweep_absent": True,
        "reload_absent": True,
        "tear_or_corruption_absent": True,
    }
    data.update(overrides)
    return data


def main():
    good = MOD.evaluate(serial_log(), observations(), SHA, DIGEST)
    assert good.passed, good.failures
    assert good.serial["observed_runtime_seconds"] >= 14400
    assert good.serial["soak_samples"] >= 240

    wrong_image = MOD.evaluate(serial_log(), observations(candidate_sha="deadbeef"), SHA, DIGEST)
    assert not wrong_image.passed
    assert "candidate_sha_mismatch" in wrong_image.failures

    too_few_cycles = MOD.evaluate(serial_log(), observations(page_cycles=19), SHA, DIGEST)
    assert not too_few_cycles.passed
    assert "page_cycles=19<20" in too_few_cycles.failures

    touch_failed = MOD.evaluate(serial_log(), observations(touch_responsive=False), SHA, DIGEST)
    assert not touch_failed.passed
    assert "physical_not_passed:touch_responsive" in touch_failed.failures

    short_soak = MOD.evaluate(serial_log(minutes=10), observations(), SHA, DIGEST)
    assert not short_soak.passed
    assert any(item.startswith("serial:screen_soak=") or item.startswith("serial:runtime=") for item in short_soak.failures)

    wdt = MOD.evaluate(serial_log() + "\nE (14500000) task_wdt: Task watchdog got triggered", observations(), SHA, DIGEST)
    assert not wdt.passed
    assert any(item.startswith("serial:fatal:task_wdt") for item in wdt.failures)

    print("Waveshare final acceptance gate tests passed")


if __name__ == "__main__":
    main()
