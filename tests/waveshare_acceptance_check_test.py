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

CORE_READY_MARKERS = """
I (5050) waveshare_product: Shared Product Core started
I (5100) waveshare_product: Espressif flash dispatcher ready; PSRAM-stacked HMI persistence is routed through internal RAM
"""
POST_SCREEN_READY_MARKERS = """
I (7100) waveshare_product: Local Engineering commissioning backend bound to touchscreen
I (7200) waveshare_product: Local source-evidence commissioning backend bound to touchscreen
I (7300) waveshare_product: Screen refresh task created in PSRAM
"""

PASS_LOG = """
I (1000) app: Before LCD DMA reservation: internal DMA free=140000 largest=130000
I (2000) app: After LCD DMA reservation: internal DMA free=105000 largest=90000
I (3000) app: Before Product Core init: internal DMA free=104000 largest=89000
I (5000) core: After Product Core init: internal DMA free=34000 largest=30000
""" + CORE_READY_MARKERS + """
I (6000) lcd: LVGL activation stage 1/6
I (6100) lcd: LVGL activation stage 2/6
I (6200) lcd: LVGL activation stage 3/6
I (6300) lcd: LVGL activation stage 4/6
I (6400) lcd: LVGL activation stage 5/6
I (6500) lcd: LVGL activation stage 6/6
I (6600) lcd: Native LCD/LVGL/touch ready
I (7000) app: After LVGL/UI activation: internal DMA free=28000 largest=24000
""" + POST_SCREEN_READY_MARKERS + """
I (61000) lcd: Screen soak: heap free=200000 min=180000 | PSRAM free=7000000 largest=6900000 | DMA free=27000 largest=23000 | screen stack hwm=7000
I (121000) lcd: Screen soak: heap free=199000 min=179000 | PSRAM free=6990000 largest=6890000 | DMA free=26000 largest=22000 | screen stack hwm=6900
I (361000) lcd: Screen soak: heap free=198000 min=178000 | PSRAM free=6980000 largest=6880000 | DMA free=25500 largest=21500 | screen stack hwm=6850
"""

FAILED_02BC = """
I (5000) app: After Product Core init: internal DMA free=1998 largest=1600
E (5100) core: ESP_ERR_NO_MEM (0x101)
W (5200) core: Startup completed with degraded subsystems
I (6000) lcd: LVGL activation stage 1/6
I (6100) lcd: LVGL activation stage 2/6
I (6200) lcd: LVGL activation stage 3/6
I (6300) lcd: LVGL activation stage 4/6
I (6400) lcd: LVGL activation stage 5/6
I (6500) lcd: LVGL activation stage 6/6
I (6600) lcd: Native LCD/LVGL/touch ready
I (7000) app: After LVGL/UI activation: internal DMA free=434 largest=400
I (122000) lcd: Screen soak: heap free=150000 min=140000 | PSRAM free=6900000 largest=6800000 | DMA free=9026 largest=7680 | screen stack hwm=7288
"""

WATCHDOG = PASS_LOG + "\nE (362000) task_wdt: Task watchdog got triggered. IDLE0\n"
FRAGMENTED = PASS_LOG.replace("DMA free=25500 largest=21500", "DMA free=25500 largest=4096")
RESET_LOG = PASS_LOG + "\nI (1000) lcd: Screen soak: heap free=198000 | PSRAM free=6980000 largest=6880000 | DMA free=25000 largest=21000 | screen stack hwm=6800\n"
REPEATED_ACTIVATION = PASS_LOG + "\nI (362000) lcd: LVGL activation stage 1/6\n"
SERVICE_FAILURE = PASS_LOG + "\nE (362000) waveshare_product: Screen backend provider initialization failed; UI stays unavailable\n"
MALFORMED_SOAK = PASS_LOG.replace(
    "I (121000) lcd: Screen soak: heap free=199000 min=179000 | PSRAM free=6990000 largest=6890000 | DMA free=26000 largest=22000 | screen stack hwm=6900",
    "I (121000) lcd: Screen soak: heap free=199000 min=179000 | PSRAM free=6990000 largest=6890000 | screen stack hwm=6900",
).replace(
    "I (361000) lcd: Screen soak: heap free=198000 min=178000 | PSRAM free=6980000 largest=6880000 | DMA free=25500 largest=21500 | screen stack hwm=6850",
    "I (361000) lcd: heartbeat only",
)
NO_LARGEST = """
I (1000) core: After Product Core init: internal DMA free=34000
I (1050) waveshare_product: Shared Product Core started
I (1100) waveshare_product: Espressif flash dispatcher ready; PSRAM-stacked HMI persistence is routed through internal RAM
I (2000) lcd: LVGL activation stage 1/6
I (2100) lcd: LVGL activation stage 2/6
I (2200) lcd: LVGL activation stage 3/6
I (2300) lcd: LVGL activation stage 4/6
I (2400) lcd: LVGL activation stage 5/6
I (2500) lcd: LVGL activation stage 6/6
I (2600) lcd: Native LCD/LVGL/touch ready
I (3000) app: After LVGL/UI activation: internal DMA free=28000
I (3100) waveshare_product: Local Engineering commissioning backend bound to touchscreen
I (3200) waveshare_product: Local source-evidence commissioning backend bound to touchscreen
I (3300) waveshare_product: Screen refresh task created in PSRAM
I (61000) lcd: Screen soak: DMA free=27000
I (121000) lcd: Screen soak: DMA free=26000
"""


def main() -> None:
    good = MOD.analyse(PASS_LOG)
    assert good.passed, good.failures
    assert good.minimum_checked_dma_free == 25500
    assert good.minimum_checked_dma_largest == 21500
    assert good.runtime_dma_free == [27000, 26000, 25500]
    assert good.runtime_dma_largest == [23000, 22000, 21500]
    assert good.soak_samples == 3
    assert good.timestamp_rollbacks == 0
    assert all(count == 1 for count in good.activation_counts.values())
    assert all(good.runtime_markers.values())

    formal = MOD.analyse(PASS_LOG, min_runtime_seconds=300, min_dma_largest=20_000)
    assert formal.passed, formal.failures
    assert formal.observed_runtime_seconds == 360.0

    stalled = MOD.analyse(FAILED_02BC, min_dma_free=0, min_soak_samples=1, min_runtime_seconds=300)
    assert not stalled.passed
    assert any(x.startswith("runtime=") for x in stalled.failures)
    assert any(x.startswith("runtime_marker_missing:") for x in stalled.failures)

    old = MOD.analyse(FAILED_02BC)
    assert not old.passed
    assert old.fatal_hits["no_mem"] == 1
    assert old.fatal_hits["degraded_startup"] == 1
    assert old.minimum_checked_dma_free == 434
    assert old.minimum_checked_dma_largest == 400
    assert any(x.startswith("dma_free=") for x in old.failures)
    assert any(x.startswith("screen_soak=") for x in old.failures)

    wdt = MOD.analyse(WATCHDOG)
    assert not wdt.passed
    assert wdt.fatal_hits["task_wdt"] >= 1

    relaxed = MOD.analyse(PASS_LOG, min_dma_free=25000, min_soak_samples=2)
    assert relaxed.passed

    fragmented = MOD.analyse(FRAGMENTED, min_dma_largest=8192)
    assert not fragmented.passed
    assert "dma_largest=4096<8192" in fragmented.failures

    no_largest = MOD.analyse(NO_LARGEST)
    assert not no_largest.passed
    assert "dma_largest_evidence_missing" in no_largest.failures
    assert "screen_soak_dma_largest=0<2" in no_largest.failures

    malformed_soak = MOD.analyse(MALFORMED_SOAK)
    assert not malformed_soak.passed
    assert "screen_soak=2<2" not in malformed_soak.failures
    assert "screen_soak_dma_free=1<2" in malformed_soak.failures
    assert "screen_soak_dma_largest=1<2" in malformed_soak.failures

    reset = MOD.analyse(RESET_LOG)
    assert not reset.passed
    assert reset.timestamp_rollbacks == 1
    assert "timestamp_rollback=1" in reset.failures

    repeated = MOD.analyse(REPEATED_ACTIVATION)
    assert not repeated.passed
    assert any(x.startswith("activation_repeated=") for x in repeated.failures)

    service_failure = MOD.analyse(SERVICE_FAILURE)
    assert not service_failure.passed
    assert service_failure.fatal_hits["screen_backend_failed"] == 1

    missing = MOD.analyse("LVGL activation stage 1/6\n", min_runtime_seconds=300)
    assert not missing.passed
    assert "native_ready_missing" in missing.failures
    assert "dma_free_evidence_missing" in missing.failures
    assert "dma_largest_evidence_missing" in missing.failures
    assert "runtime_timestamp_evidence_missing" in missing.failures
    assert any(x.startswith("runtime_marker_missing:") for x in missing.failures)

    print("Waveshare acceptance log gate tests passed")


if __name__ == "__main__":
    main()
