# AISH-OS Evidence Index

Evidence is scoped to the exact head named below. A green build is software evidence only; it is not physical acceptance.

| Lane | Exact head / merge | Evidence | State |
|---|---|---|---|
| Generator strong evidence | `fbafd290fd8c1277880fcf668d75d829014d2502` -> dev merge `ee296dbe62baed4b837f487898c6d50adf416d98` | Generator strong evidence `33619278241`; Firmware/Web + ESP-IDF `33619278180` | MERGED / software green |
| Browser lifecycle batch 1 | `42572c5717abe97e32cdba70aa31a7f14a95b221` -> dev merge `ef076d98daddee0ee9542066c69615ef9650bb01` | Browser lifecycle `33620764632`; Firmware/Web + ESP-IDF `33620764635` | MERGED / software green |
| Waveshare failed status-refresh candidate | `01c1c2724f896b481c055685e5577a2c1f30a1c3` | CI green but exact artifact physically still showed recurring sweep; Issue #27 records failure | PHYSICAL FAIL |
| Waveshare scanout root-cause candidate | PR #57 earlier accepted head recorded in Issue #27 | Physical A/B showed sweep eliminated after scanout isolation/fix | physical evidence applies only to that recorded head |
| Waveshare current candidate | `e7c6a027234e08ec33d06859b59e3518d918d717` | backend parity `33620770793` green; physical acceptance tools `33620770819` green; remaining exact-head workflows/physical current-artifact result must be rechecked live | EXECUTING |
| Secure OTA current baseline | PR #52 head `f36a302953f453ca968d2f0714e3ebd97432176a` | software implementation/CI recorded on PR; real interruption/rollback remains physical | BLOCKED physical |
| Operator continuity/verdict | PR #54 head `61f95a2ca87f659884f372cff6568b7e88e136d8` | focused `33617779040`; Firmware/Web `33617778825` | TESTED software / held baseline |

## Failed Waveshare candidates — never reuse

`ec4fb846...`, `02bc128a...`, `09c137f3...`, `01c1c272...`.

## Evidence rules

1. Exact current PR head must be checked immediately before merge.
2. If the source tree changes after physical acceptance, physical evidence does not transfer unless tree identity is explicitly proven and the governance gate permits transfer.
3. No hardware PASS may be inferred from CI, source inspection, simulator output or old candidate observations.
4. No production profile approval may be inferred from generic Modbus behavior.
