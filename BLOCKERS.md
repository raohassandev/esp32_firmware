# AISH-OS Blocker Ledger

## B-001 — Waveshare current-head physical acceptance

**State:** BLOCKED_EXTERNAL / active execution lane  
**PR:** #57  
**Current head at snapshot:** `e7c6a027234e08ec33d06859b59e3518d918d717`

Earlier PR #57 hardware work physically eliminated the recurring scanout sweep, but the current head additionally fixes the Alarms-page LVGL wedge. Physical acceptance must therefore be tied to the current exact artifact/source head; it cannot be inherited from an earlier head. PR #46 and PR #20 remain blocked.

## B-002 — Waveshare DMA headroom

The 12-line bounce-buffer scanout fix reported roughly 9530 B internal DMA free / 7680 B largest block on the physically observed candidate, below the prior >=20 kB target. Do not hide or waive this. Recover headroom by proven task-stack right-sizing or another measured design change, then requalify the exact changed candidate.

## B-003 — Production inverter profiles

Production writes remain blocked per model until exact manufacturer documentation, identity/telemetry/command/readback mapping, simulator evidence, bench test, physical readback/rollback evidence and signed approval exist. No third-party guessed register map is acceptable.

## B-004 — Site source evidence

Strong generator/grid evidence support exists in software, but real register addresses, masks, active polarity and site topology evidence must be commissioned from actual documentation/site wiring. Defaults remain disabled and fail closed.

## B-005 — Physical endurance/FAT/SAT

Software CI cannot close TCP PCB/TIME_WAIT endurance, long hardware soak, grid/generator operating FAT, source-transfer FAT or signed SAT. Record only observed evidence tied to an exact firmware SHA.
