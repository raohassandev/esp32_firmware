# AISH-OS Evidence Index v2

Master program: #79. Evidence is valid only for the exact source/head/artifact named. Software CI never substitutes for physical acceptance.

## Live integration baseline

Current `dev`: `3096f2bfa10e86b3163b99ae7622bffded6791ac` after governed PR #124 merge.

## Current merged software evidence chain

| Capability | Exact evidence | State |
|---|---|---|
| AISH-OS v2 governance | PR #97 -> `430e9157eb82196501f896d9323da16c86f9255e`; full `33716216708` | MERGED |
| Modbus connection modes | PR #99 -> `3aa69162a6847a852e9b648ef8ec6988f5e3f296` | MERGED |
| Production Wi-Fi build credential protection | PR #100 -> `7d9abdccb032b387525df9240b2943b74324a8e9` | MERGED |
| Inverter identity revalidation | PR #102 -> `b60bcb9474f4ed7dd0b0ed631410b54628a47d01` | MERGED |
| Requirements reconciliation | PR #103 -> `81b02a4b6188b9f7150d9161883f0465e14ba6f5` | MERGED |
| Engineering DOM/error stability | PR #105 -> `6569e36e019adf4b890d04f163091f04eec6020b` | MERGED |
| Web spinlock/nonblocking regression | PR #107 -> `6ae86b09294b3e3a2c8a8eaf085ce5c35c69cf74`; focused `33722607313`, full `33722607276` | MERGED |
| Inverter command schema safety | PR #108 head `0807c735...`; focused `33723570913`, full `33723570919`; merge `b8996d5f834cc8edeb084f56c802ff5fc6ecd04d` | MERGED |
| Modbus cumulative-deadline endpoint admission | PR #114 head `ecfbd77...`; mode `33728128805`, endpoint `33728128876`, full `33728128866`; merge `fa8ca9b17e08f2478e104942b9d6dbfad4f0ca7f` | MERGED |
| Profile assignment ordering interlock | PR #117 head `115ec2f...`; full `33729168281`; merge `1360c4a8356ff8acdc19878f65da311c0b0eccc6` | MERGED |
| Production inverter write authority | PR #119 head `eef24d...`; authority `33729644794`, release `33729644834`, identity `33729644804`, schema `33729644806`, full `33729644800`; merge `d9cd81bcf500a034d6cc88ea92e3bb74e42ed258` | MERGED |
| Governance through PR119 | PR #120 head `18087bb...`; full `33730424946`; merge `1b7cfe57f4c236516e2b5595f544c3524dcead0c` | MERGED |
| Legacy config migration OOM preservation | PR #122 head `2ca2933664d9146a0fe59a693074d47ecc2872aa`; config `33738503242`, Wi-Fi `33738503441`, Modbus `33738503220`, full `33738503251`; merge `dfe93de50e2a5715f4d212ff3233d566d36e2cfd` | MERGED |
| Atomic safety-alarm snapshot | PR #124 head `269404aade57caf1e4e7ef3a877e786580cc691e`; focused `33739241779`, full `33739241807`; merge `3096f2bfa10e86b3163b99ae7622bffded6791ac` | MERGED |

## Held runtime / physical source identities

### Generator source transition
Draft PR #106 head `a1620789235d21b515f9f245f2329fab88b50558` is software-GREEN but remains physically gated by #80. Its physical PASS cannot be inferred from CI and, after dev advances, any eventual merge must use an exact current-base replay plus fresh CI.

### Waveshare exact physical candidate
- source `87841ecee727fe1d814d4186be8c8c26e4afafb4`
- tree `6ddd7900f9b4ece0fba9349b905e1c078fc3401e`
- package run `33622358267`
- artifact `9843536218` / `waveshare-800x480-87841ece`
- ZIP digest `sha256:89e621034d4c91096fc5d38dd57ac40eeeab34275e4af1fc0461b48575039096`
- app SHA256 `8be2a2aad5f223d8b9bca498db2e12c04f7f205feaa9908b7922c37421c46593`
- short physical display/touch/Alarms gate: PASS on this exact image
- first continuous soak: ~2 h / 121 consecutive one-minute samples plus 25 clean backend health rounds; ended when USB dock/power path disappeared, with no recorded firmware crash
- required uninterrupted >=4 h / >=240-sample same-image gate: **INCOMPLETE**

PR #57/#20 remain unpromoted. After genuine final soak PASS, #25 backend parity and #26 persistence/ARM remain mandatory on the same accepted identity.

### Secure OTA
PR #52 is software-qualified on the frozen Phase-1 line. #86 must later reconcile it onto the intended accepted release baseline and physically prove invalid/interrupted upload, power loss, previous-slot boot, pending verification, mark-valid and rollback. No hardware PASS is inherited from source CI.

## Production inverter qualification boundary

Generic protections merged through #119 prevent guessed/unqualified positive writes, but they do not approve any Huawei/GoodWe/Solis/FoxESS/Knox production mapping. #82 still requires official model/firmware/manual identity, physical read-only identity/telemetry/status verification, write/readback/rollback evidence and signed production approval.

## Stale/superseded rule

Stale PRs #77/#101/#104/#109/#115/#116/#118/#121/#123 and obsolete Waveshare promotion #46 are not current merge/evidence sources. Historical CI belongs only to its historical exact head.

## Required remaining release evidence

1. #87/#27 uninterrupted >=4 h Waveshare same-image evidence, then #25/#26.
2. #80 source-transition physical matrix.
3. #81 real site source-contact/polarity/meter mapping.
4. #82 official-manual + bench record for each production inverter profile.
5. #86 OTA rollback/interruption physical record on intended baseline.
6. #83 Modbus/network endurance, Grid/DG/mixed-source FAT and signed SAT.
7. Final release SHA/config/profile/artifact traceability with no critical blocker.

Partial physical intervals may not be combined to manufacture a required continuous PASS, and no physical PASS transfers across changed source/artifact/config/profile identities unless explicitly proven and allowed by the governing gate.
