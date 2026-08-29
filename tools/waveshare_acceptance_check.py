#!/usr/bin/env python3
"""Deterministic Waveshare physical-smoke log validator.

Consumes a captured serial log from the exact flashed candidate and returns a
machine-readable PASS/FAIL result. This never replaces physical observation of
the LCD/touch; it gates the serial/resource evidence that can be checked
truthfully from the log.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, asdict
from pathlib import Path

FATAL_PATTERNS = {
    "no_mem": re.compile(r"ESP_ERR_NO_MEM", re.I),
    "degraded_startup": re.compile(r"Startup completed with degraded subsystems", re.I),
    "task_wdt": re.compile(r"task[_ ]?wdt|Task watchdog", re.I),
    "panic": re.compile(r"Guru Meditation|panic'ed|\bpanic\b|\babort\b", re.I),
}

STAGES = (
    "Before LCD DMA reservation",
    "After LCD DMA reservation",
    "Before Product Core init",
    "After Product Core init",
    "After LVGL/UI activation",
)

# Be deliberately strict: Screen-soak lines also contain heap and PSRAM
# "free/largest" fields before the DMA fields. Matching a generic free= value
# would silently gate the wrong memory pool.
DMA_FREE_RE = re.compile(r"(?:internal\s+)?DMA(?:-capable)?\s+free\s*[=:]\s*(\d+)", re.I)
DMA_LARGEST_RE = re.compile(
    r"(?:internal\s+)?DMA(?:-capable)?\s+free\s*[=:]\s*\d+\s+largest(?:\s+block)?\s*[=:]\s*(\d+)",
    re.I,
)
SOAK_RE = re.compile(r"Screen soak", re.I)
ACTIVATION_RE = re.compile(r"LVGL activation stage\s+(\d)\s*/\s*6", re.I)
READY_RE = re.compile(r"Native LCD/LVGL/touch ready", re.I)
UPTIME_RE = re.compile(r"^[VDIWE]\s+\((\d+)\)")


@dataclass
class Result:
    passed: bool
    fatal_hits: dict[str, int]
    activation_stages: list[int]
    activation_counts: dict[int, int]
    native_ready: bool
    soak_samples: int
    stage_dma_free: dict[str, int | None]
    stage_dma_largest: dict[str, int | None]
    runtime_dma_free: list[int]
    runtime_dma_largest: list[int]
    minimum_checked_dma_free: int | None
    minimum_checked_dma_largest: int | None
    observed_runtime_seconds: float | None
    timestamp_rollbacks: int
    required_min_dma_free: int
    required_min_dma_largest: int
    required_soak_samples: int
    required_min_runtime_seconds: int
    failures: list[str]


def _metric(line: str, regex: re.Pattern[str]) -> int | None:
    m = regex.search(line)
    return int(m.group(1)) if m else None


def analyse(
    text: str,
    min_dma_free: int = 20_000,
    min_soak_samples: int = 2,
    min_runtime_seconds: int = 0,
    min_dma_largest: int = 0,
) -> Result:
    lines = text.splitlines()
    fatal_hits = {name: sum(1 for line in lines if rx.search(line)) for name, rx in FATAL_PATTERNS.items()}

    activation_hits = [int(m.group(1)) for line in lines for m in [ACTIVATION_RE.search(line)] if m]
    activation = sorted(set(activation_hits))
    activation_counts = {stage: activation_hits.count(stage) for stage in range(1, 7)}
    native_ready = any(READY_RE.search(line) for line in lines)

    uptimes_ms = [int(m.group(1)) for line in lines for m in [UPTIME_RE.search(line)] if m]
    timestamp_rollbacks = sum(1 for previous, current in zip(uptimes_ms, uptimes_ms[1:]) if current < previous)
    observed_runtime = (uptimes_ms[-1] - uptimes_ms[0]) / 1000.0 if len(uptimes_ms) >= 2 and not timestamp_rollbacks else None

    stage_free: dict[str, int | None] = {s: None for s in STAGES}
    stage_largest: dict[str, int | None] = {s: None for s in STAGES}
    runtime_free: list[int] = []
    runtime_largest: list[int] = []
    soak_samples = 0

    for line in lines:
        for stage in STAGES:
            if stage in line:
                f = _metric(line, DMA_FREE_RE)
                l = _metric(line, DMA_LARGEST_RE)
                if f is not None:
                    stage_free[stage] = f
                if l is not None:
                    stage_largest[stage] = l
        if SOAK_RE.search(line):
            soak_samples += 1
            f = _metric(line, DMA_FREE_RE)
            l = _metric(line, DMA_LARGEST_RE)
            if f is not None:
                runtime_free.append(f)
            if l is not None:
                runtime_largest.append(l)

    checked_free = [v for key, v in stage_free.items() if key in ("After Product Core init", "After LVGL/UI activation") and v is not None]
    checked_free += runtime_free
    checked_largest = [v for key, v in stage_largest.items() if key in ("After Product Core init", "After LVGL/UI activation") and v is not None]
    checked_largest += runtime_largest

    min_free = min(checked_free) if checked_free else None
    min_largest = min(checked_largest) if checked_largest else None

    failures: list[str] = []
    for name, count in fatal_hits.items():
        if count:
            failures.append(f"fatal:{name}={count}")
    if activation != [1, 2, 3, 4, 5, 6]:
        failures.append(f"activation_stages={activation}")
    repeated_activation = {stage: count for stage, count in activation_counts.items() if count > 1}
    if repeated_activation:
        failures.append(f"activation_repeated={repeated_activation}")
    if not native_ready:
        failures.append("native_ready_missing")
    if soak_samples < min_soak_samples:
        failures.append(f"screen_soak={soak_samples}<{min_soak_samples}")
    if min_free is None:
        failures.append("dma_free_evidence_missing")
    elif min_free < min_dma_free:
        failures.append(f"dma_free={min_free}<{min_dma_free}")
    if min_largest is None:
        failures.append("dma_largest_evidence_missing")
    elif min_dma_largest > 0 and min_largest < min_dma_largest:
        failures.append(f"dma_largest={min_largest}<{min_dma_largest}")
    if timestamp_rollbacks:
        failures.append(f"timestamp_rollback={timestamp_rollbacks}")
    if min_runtime_seconds > 0:
        if observed_runtime is None:
            failures.append("runtime_timestamp_evidence_missing")
        elif observed_runtime < min_runtime_seconds:
            failures.append(f"runtime={observed_runtime:.1f}s<{min_runtime_seconds}s")

    return Result(
        passed=not failures,
        fatal_hits=fatal_hits,
        activation_stages=activation,
        activation_counts=activation_counts,
        native_ready=native_ready,
        soak_samples=soak_samples,
        stage_dma_free=stage_free,
        stage_dma_largest=stage_largest,
        runtime_dma_free=runtime_free,
        runtime_dma_largest=runtime_largest,
        minimum_checked_dma_free=min_free,
        minimum_checked_dma_largest=min_largest,
        observed_runtime_seconds=observed_runtime,
        timestamp_rollbacks=timestamp_rollbacks,
        required_min_dma_free=min_dma_free,
        required_min_dma_largest=min_dma_largest,
        required_soak_samples=min_soak_samples,
        required_min_runtime_seconds=min_runtime_seconds,
        failures=failures,
    )


def main() -> int:
    p = argparse.ArgumentParser(description="Validate Waveshare physical-smoke serial evidence")
    p.add_argument("log", type=Path)
    p.add_argument("--min-dma-free", type=int, default=20_000)
    p.add_argument("--min-dma-largest", type=int, default=0,
                   help="Optional minimum largest contiguous DMA-capable block; 0 requires evidence without inventing a threshold")
    p.add_argument("--min-soak-samples", type=int, default=2)
    p.add_argument("--min-runtime-seconds", type=int, default=0,
                   help="Require ESP-IDF timestamp span; use 300 for a 5-minute smoke capture")
    p.add_argument("--json", action="store_true")
    args = p.parse_args()

    result = analyse(
        args.log.read_text(errors="replace"),
        min_dma_free=args.min_dma_free,
        min_soak_samples=args.min_soak_samples,
        min_runtime_seconds=args.min_runtime_seconds,
        min_dma_largest=args.min_dma_largest,
    )
    payload = asdict(result)
    if args.json:
        print(json.dumps(payload, indent=2, sort_keys=True))
    else:
        print("SERIAL SMOKE PASS" if result.passed else "SERIAL SMOKE FAIL")
        for failure in result.failures:
            print(f"- {failure}")
        print(f"- soak_samples={result.soak_samples}")
        print(f"- min_dma_free={result.minimum_checked_dma_free}")
        print(f"- min_dma_largest={result.minimum_checked_dma_largest}")
        print(f"- observed_runtime_seconds={result.observed_runtime_seconds}")
        print(f"- timestamp_rollbacks={result.timestamp_rollbacks}")
    return 0 if result.passed else 1


if __name__ == "__main__":
    sys.exit(main())
