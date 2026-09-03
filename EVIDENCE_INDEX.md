# AISH-OS Evidence Index v2

Master program: #79. Evidence is valid only for the exact source/head/artifact named. Software CI never substitutes for physical acceptance.

## Live integration baseline

- `dev` snapshot at governance-v2 start: `a7d547f2f1909527538ec529d1f44b4ad2733861`
- merge message: PR #76 main app status polling lifecycle

## Merged software evidence

| Capability | Evidence | State |
|---|---|---|
| Generator strong evidence | PR #58 / merge `ee296dbe62baed4b837f487898c6d50adf416d98`; focused `33619278241`; full `33619278180` | MERGED |
| Generator 1–3 persisted config/schema migration | PR #63 | MERGED |
| Generator 1–3 runtime/fleet aggregation | PR #64 | MERGED |
| Browser lifecycle batch | PR #59; head `42572c5717abe97e32cdba70aa31a7f14a95b221`; focused `33620764632`; full `33620764635` | MERGED |
| Operator history/alarm lifecycle | PR #62 | MERGED |
| Device diagnostics lifecycle | PR #65 | MERGED |
| Active Engineering auth deadlines | PR #69 | MERGED |
| EM500 lifecycle | PR #71 | MERGED |
| Operator product-view lifecycle | PR #73 | MERGED |
| Dead Engineering asset cleanup | PR #75 | MERGED |
| Main app status lifecycle | PR #76 -> dev `a7d547f2...` | MERGED |

## Software ready but physical/baseline gated

| Lane | Exact head | Software evidence/state | Physical/baseline state |
|---|---|---|---|
| Generator source-transition admission | PR #77 `b36cfa40b9cc3bf389199663ceaf0855aeecb18e` | Generator checks `33647531871` GREEN; full `33647531955` GREEN | #80 bench FAT required before merge |
| Secure OTA | PR #52 `f36a302953f453ca968d2f0714e3ebd97432176a` | Secure OTA `33615987142` GREEN; full `33615987013` GREEN | #86/#50 physical rollback qualification; baseline held by Waveshare |
| Operator continuity/verdict | PR #54 `61f95a2ca87f659884f372cff6568b7e88e136d8` | focused `33617779040` GREEN; full `33617778825` GREEN | replay/reconcile after Waveshare source graph closes |

## Waveshare current physical candidate

- PR #57 source: `87841ecee727fe1d814d4186be8c8c26e4afafb4`
- tree: `6ddd7900f9b4ece0fba9349b905e1c078fc3401e`
- package run: `33622358267`
- artifact: `9843536218` / `waveshare-800x480-87841ece`
- ZIP digest: `sha256:89e621034d4c91096fc5d38dd57ac40eeeab34275e4af1fc0461b48575039096`
- app SHA256: `8be2a2aad5f223d8b9bca498db2e12c04f7f205feaa9908b7922c37421c46593`
- package checksums: 16/16 PASS
- short physical display/touch/Alarms gate: PASS
- short-gate runtime: zero WDT/panic/NO_MEM/unexpected reboot; DMA/resources healthy above >=20 kB floor
- continuous soak attempt: about 2 h / 121 consecutive one-minute samples clean; ended when USB hub/power path disappeared, with no firmware crash evidence
- final continuous >=4 h / >=240-sample gate: INCOMPLETE

Therefore PR #57 and PR #20 remain unpromoted. Issue #87 coordinates final soak/parity/persistence/promotion.

## Active WIP

- L1 #88/#78 `work/modbus/connection-modes`: implementation in progress; do not treat branch as completed evidence until safe config migration, API exposure, tests, exact-head full CI and governed merge finish.
- L10 #90: final served-browser poller audit pending closure.
- L12 #92: requirements closure matrix pending.

## Superseded/failed Waveshare candidates — never reuse for release evidence

`ec4fb846...`, `02bc128a...`, `09c137f3...`, `01c1c272...`.

## Evidence rules

1. Re-fetch the exact current PR head immediately before merge.
2. A physical PASS does not transfer across a changed source tree unless identity is explicitly proven and the governing gate allows it.
3. CI/source/simulator evidence cannot manufacture a hardware PASS.
4. Generic Modbus behavior cannot approve a production inverter profile.
5. Partial soak intervals are evidence of stability but cannot be added together to fake one required uninterrupted duration.
6. Failed/superseded candidates remain historical evidence only and must never become active promotion sources.
