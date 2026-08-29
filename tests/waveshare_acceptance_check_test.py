#!/usr/bin/env python3
import importlib.util
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("waveshare_acceptance_check", ROOT / "tools" / "waveshare_acceptance_check.py")
MOD = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
sys.modules[SPEC.name] = MOD
SPEC.loader.exec_module(MOD)

PASS_LOG = """
I app: Before LCD DMA reservation: DMA free=140000 largest=130000
I app: After LCD DMA reservation: DMA free=105000 largest=90000
I app: Before Product Core init: DMA free=104000 largest=89000
I core: After Product Core init: DMA free=34000 largest=30000
I lcd: LVGL activation stage 1/6
I lcd: LVGL activation stage 2/6
I lcd: LVGL activation stage 3/6
I lcd: LVGL activation stage 4/6
I lcd: LVGL activation stage 5/6
I lcd: LVGL activation stage 6/6
I lcd: Native LCD/LVGL/touch ready
I app: After LVGL/UI activation: DMA free=28000 largest=24000
I lcd: Screen soak: heap=200000 DMA free=27000 largest=23000 stack hwm=7000
I lcd: Screen soak: heap=199000 DMA free=26000 largest=22000 stack hwm=6900
"""

FAILED_02BC = """
I app: After Product Core init: DMA free=1998 largest=1600
E core: ESP_ERR_NO_MEM (0x101)
W core: Startup completed with degraded subsystems
I lcd: LVGL activation stage 1/6
I lcd: LVGL activation stage 2/6
I lcd: LVGL activation stage 3/6
I lcd: LVGL activation stage 4/6
I lcd: LVGL activation stage 5/6
I lcd: LVGL activation stage 6/6
I lcd: Native LCD/LVGL/touch ready
I app: After LVGL/UI activation: DMA free=434 largest=400
I lcd: Screen soak: DMA free=9026 largest=7680 stack hwm=7288
"""

WATCHDOG = PASS_LOG + "\nE task_wdt: Task watchdog got triggered. IDLE0\n"


def main() -> None:
    good = MOD.analyse(PASS_LOG)
    assert good.passed, good.failures
    assert good.minimum_checked_dma_free == 26000
    assert good.soak_samples == 2

    old = MOD.analyse(FAILED_02BC)
    assert not old.passed
    assert old.fatal_hits["no_mem"] == 1
    assert old.fatal_hits["degraded_startup"] == 1
    assert any(x.startswith("dma_free=") for x in old.failures)
    assert any(x.startswith("screen_soak=") for x in old.failures)

    wdt = MOD.analyse(WATCHDOG)
    assert not wdt.passed
    assert wdt.fatal_hits["task_wdt"] >= 1

    relaxed = MOD.analyse(PASS_LOG, min_dma_free=25000, min_soak_samples=2)
    assert relaxed.passed

    missing = MOD.analyse("LVGL activation stage 1/6\n")
    assert not missing.passed
    assert "native_ready_missing" in missing.failures
    assert "dma_free_evidence_missing" in missing.failures

    print("Waveshare acceptance log gate tests passed")


if __name__ == "__main__":
    main()
