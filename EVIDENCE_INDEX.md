# AISH-OS Evidence Index v2

Master program: #79. Evidence is valid only for the exact source/head/artifact named. Software CI never substitutes for physical acceptance.

## Live integration baseline

Current `dev`: `d9cd81bcf500a034d6cc88ea92e3bb74e42ed258` after governed PR #119 merge.

## Current merged software evidence chain

| Capability | Exact evidence | State |
|---|---|---|
| AISH-OS v2 governance | PR #97 -> `430e9157eb82196501f896d9323da16c86f9255e`; full run `33716216708` | MERGED |
| Modbus connection modes | PR #99 -> `3aa69162a6847a852e9b648ef8ec6988f5e3f296` | MERGED |
| Production Wi-Fi build credential protection | PR #100 -> `7d9abdccb032b387525df9240b2943b74324a8e9` | MERGED |
| Inverter identity revalidation | PR #102 -> `b60bcb9474f4ed7dd0b0ed631410b54628a47d01` | MERGED |
| Requirements reconciliation | PR #103 -> `81b02a4b6188b9f7150d9161883f0465e14ba6f5` | MERGED |
| Engineering DOM/error stability | PR #105 -> `6569e36e019adf4b890d04f163091f04eec6020b` | MERGED |
| Web spinlock/nonblocking regression | PR #107 -> `6ae86b09294b3e3a2c8a8eaf085ce5c35c69cf74`; focused `33722607313`, full `33722607276` | MERGED |
| Inverter command width/scale/range/FC06/FC16 safety | PR #108 head `0807c735375de6d749c1f9eda0eeb252336453a4`; focused `33723570913`, full `33723570919`; merge `b8996d5f834cc8edeb084f56c802ff5fc6ecd04d` | MERGED |
| Modbus cumulative-deadline endpoint admission | PR #114 head `ecfbd77a58fc0ec052058938304b37e8ad850beb`; mode `33728128805`, endpoint `33728128876`, full `33728128866`; merge `fa8ca9b17e08f2478e104942b9d6dbfad4f0ca7f` | MERGED |
| Profile assignment ordering interlock | PR #117 head `115ec2fbe84cddc0b898027e07c25e0028f5003b`; full `33729168281` plus focused regressions GREEN; merge `1360c4a8356ff8acdc19878f65da311c0b0eccc6` | MERGED |
| Production inverter write authority | PR #119 head `eef24d355f8cc8f01955cd2ff743aab171738af0`; authority `33729644794`, release gate `33729644834`, identity `33729644804`, command schema `33729644806`, full `33729644800`; merge `d9cd81bcf500a034d6cc88ea92e3bb74e42ed258` | MERGED |

Earlier merged generator/browser/auth/acquisition work remains valid historical evidence where its exact source identity is cited; current release claims must be checked against live `dev`.

## Held runtime / physical source identities

### Generator source transition
- Draft PR #106 head `a1620789235d21b515f9f245f2329fab88b50558`.
- Software CI is GREEN.
- #80 physical bench matrix is mandatory before merge; if `dev` has advanced, replay identical validated runtime content onto live `dev` only after physical PASS and prove equivalence with fresh CI.

### Waveshare exact physical candidate
- PR #57 source `87841ecee727fe1d814d4186be8c8c26e4afafb4`.
- tree `6ddd7900f9b4ece0fba9349b905e1c078fc3401e`.
- package run `33622358267`.
- artifact `9843536218` / `waveshare-800x480-87841ece`.
- ZIP digest `sha256:89e621034d4c91096fc5d38dd57ac40eeeab34275e4af1fc0461b48575039096`.
- application SHA256 `8be2a2aad5f223d8b9bca498db2e12c04f7f205feaa9908b7922c37421c46593`.
- package checksums 16/16 PASS.
- short physical display/touch/Alarms gate PASS on this exact image: no recurring ~5 s sweep/reload/flicker/corruption, Alarms opens, touch responsive, zero WDT/panic/NO_MEM/unexpected reset, healthy DMA/resources and fail-closed commissioning gate.
- first continuous soak: ~2 h / 121 consecutive one-minute samples clean with 25 backend health rounds; terminated by disappearance of the USB dock/power path, not by recorded firmware crash.
- final required uninterrupted >=4 h / >=240-sample same-image soak: **INCOMPLETE**. Partial runs must not be added together.

Therefore PR #57/#20 remain unpromoted. #87 owns the remaining Waveshare sequence; after final soak PASS, #25 backend parity and #26 persistence/ARM remain required before promotion.

### Secure OTA
PR #52 is software-qualified on the frozen Phase-1 line. Physical interrupted-upload, power-loss, previous-slot, pending-verification, mark-valid and rollback evidence must be executed later against the intended accepted release baseline under #86; current source CI cannot be inherited as physical PASS.

## Production inverter qualification boundary

PRs #102/#108/#117/#119 harden the generic engine and fail closed, but they do **not** approve Huawei/GoodWe/Solis/FoxESS/Knox production register maps. #82 still requires exact official manual/model/firmware identity plus physical identity/telemetry/status/write/readback/rollback evidence and signed approval. Positive production commands are now additionally gated by complete profile evidence and fresh mapped ON_GRID fleet status; fail-safe zero remains available.

## Stale/superseded rule

Closed stale replay PRs #115/#116/#118 are not release evidence. Historical stale PRs #77/#101/#104/#109 and obsolete Waveshare promotion PR #46 are not merge sources. A current-base replay becomes authoritative only after its own exact-head CI and governed merge.

## Required remaining release evidence

1. #87/#27 uninterrupted >=4 h Waveshare same-image evidence, then #25 and #26.
2. #80 source-transition physical matrix tied to exact runtime source.
3. #81 real site source-contact/polarity/meter mapping.
4. #82 each production inverter profile official manual + bench qualification record.
5. #86 OTA rollback/interruption physical record on intended baseline.
6. #83 all three Modbus modes/network endurance, Grid/DG/mixed-source FAT and signed SAT.
7. Final release SHA/config/profile/artifact traceability and no critical blocker.

## Evidence rules

1. Re-fetch exact current PR head and live target immediately before merge.
2. Physical PASS never transfers to a changed source/artifact/config/profile identity unless equivalence is explicitly proven and the governing gate permits transfer.
3. CI/source/simulator evidence cannot manufacture a hardware PASS.
4. Generic Modbus or inverter-engine behavior cannot approve a manufacturer profile.
5. Partial soak intervals cannot be added together to fake one required uninterrupted duration.
6. Failed/superseded candidates remain historical evidence only.
