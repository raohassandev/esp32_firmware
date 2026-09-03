# AISH-OS Evidence Index v2

Master program: #79. Evidence is valid only for the exact source/head/artifact named. Software CI never substitutes for physical acceptance.

## Live integration baseline

Current `dev`: `d07dca2d2b20a2cf4e712df45fae9dfe7e3024c2` after governed PR #142 merge.

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
| Production profile release gate | PR #112 head `ccae3f2c4cfb24a5b8add6ddd6e175334736c10f`; merge `7a683092696c6334870b5c1007e086f8556dbc88` | MERGED |
| Manufacturer transport truthfulness | PR #113 -> `4aa935dfcf944f83cbb96333be67f99878e32c30`; pending profiles remain unqualified instead of guessed | MERGED |
| Modbus cumulative-deadline endpoint admission | PR #114 head `ecfbd77...`; mode `33728128805`, endpoint `33728128876`, full `33728128866`; merge `fa8ca9b17e08f2478e104942b9d6dbfad4f0ca7f` | MERGED |
| Profile assignment ordering interlock | PR #117 head `115ec2f...`; full `33729168281`; merge `1360c4a8356ff8acdc19878f65da311c0b0eccc6` | MERGED |
| Production inverter write authority | PR #119 head `eef24d...`; authority `33729644794`, release `33729644834`, identity `33729644804`, schema `33729644806`, full `33729644800`; merge `d9cd81bcf500a034d6cc88ea92e3bb74e42ed258` | MERGED |
| Governance through PR119 | PR #120 head `18087bb...`; full `33730424946`; merge `1b7cfe57f4c236516e2b5595f544c3524dcead0c` | MERGED |
| Legacy config migration OOM preservation | PR #122 head `2ca2933664d9146a0fe59a693074d47ecc2872aa`; config `33738503242`, full `33738503251`; merge `dfe93de50e2a5715f4d212ff3233d566d36e2cfd` | MERGED |
| Atomic safety-alarm snapshot | PR #124 head `269404aade57caf1e4e7ef3a877e786580cc691e`; focused `33739241779`, full `33739241807`; merge `3096f2bfa10e86b3163b99ae7622bffded6791ac` | MERGED |
| Governance/stale-checklist reconciliation | PR #125 head `9cf90f5e80fa7a2a10660065ef4b75a701ce1aa6`; full `33740228534`; merge `f32d9ba1581350aeb178e3b94ad30303ef1b8a5d` | MERGED |
| Runtime config/mapping disable interlock | PR #127 head `4f6ebc03c12e9653cb73f7103e1be68ad7e3f4c0`; focused `33741274303`, full `33741274300`; merge `df282a3e8afee27dfc220694e4461e4ad49d2277` | MERGED |
| Wi-Fi configuration runtime-disable interlock | PR #129 head `955380e21ddefb606c4f1f8c3db82944401b50ea`; focused `33745056243`, full `33745056135`; merge `5e5a63dba3e157d2658227ff691e9f975cedff96` | MERGED |
| Source-detection runtime-disable interlock | PR #130 head `665a02555d1f0f2cedb40bcc782258dc2a608d49`; focused `33745904234`, full `33745904243`; merge `0969a119e4fcb97405da26a59b55ec44a5a292f4` | MERGED |
| Consolidated commissioning mutation interlock inventory | PR #131 head `d165dba03146eee75e5c0bd7adfeee7058e47d87`; focused `33749197443`, full `33749196739`; 0-behind expected-head merge `41eaf22f8b92057cdbe5427c590ccd84d7fbce9b` | MERGED |
| Governance through PR #131 | PR #132 -> `2e0c946d30027419dfbd0723598ac34315cf6a86` | MERGED |
| Wi-Fi scan result lifetime cleanup | PR #133 head `9ba7ec20cc396a46fae015201ed06f1a5ccb9111`; full `33755126372`; merge `e918aef8465435d4af87eaa6c1f001767a9d2170` | MERGED |
| Schema-6 JSON depth guard | PR #135 head `71015e952fc8df5bcd40877027eb590ea9f8fb24`; focused `33756048306`, full `33756048441`; merge `ad6316b5e3dec6bca630d351a80c8e786fd61b69` | MERGED |
| Inverter profile store stack headroom | PR #136 head `9ac9ec4718920c4b8f9f829542d2b9c266911872`; focused `33756766924`, full `33756767088`; merge `321198bb5d970aa5f4842331a229ce63475e0776` | MERGED |
| Schema-6 config stack headroom | PR #137 head `2f34e60ab8e42b088babdeaa39e4c0c738f00c56`; focused `33761025944`, full `33761025927`; merge `43d4bd509ea07aadbd9ea18e9813e3ec11c60297` | MERGED |
| Runtime-component app-config stack safety contract | PR #138 head `316a619608c6e83f7af9444ad0721b4300df7535`; focused `33762133658`, full `33762133649`; 0-behind expected-head merge `dd10809f4246713ab99b3ccc9c3b515ece94fd0d` | MERGED |
| Project-wide app-config stack guard completion | PR #140 head `1bb64b117971709a394674eb73e11d10c5f4f30d`; focused `33765114501`, full `33765114492`; 0-behind expected-head merge `093954b5626c034e126fde3b773cedb1add92707` | MERGED |
| Governance through PR #140 | PR #141 head `9623ce519c40ef760e77f7b9ec0cf69cf42f62c5`; full `33766240186`; 0-behind expected-head merge `2272caefa87581f27e815ce4420a5880d2d16e38` | MERGED |
| Current-dev Waveshare final acceptance/package tooling | PR #142 head `a4a0a64210b5a05acd02cdb1016948d593c8d213`; focused `33768630723`; full Firmware/Web/ESP32-S3 `33768630667`; 0-behind expected-head merge `d07dca2d2b20a2cf4e712df45fae9dfe7e3024c2` | MERGED TOOLING ONLY |

PR #134 is stale/superseded by #135. PR #139 was closed unmerged when #140 advanced `dev`; neither is current evidence.

## Held runtime / physical source identities

### Generator source transition
Draft PR #106 head `a1620789235d21b515f9f245f2329fab88b50558` is software-GREEN but remains physically gated by #80. Its physical PASS cannot be inferred from CI and, after `dev` advances, any eventual merge must use an exact current-base replay plus fresh CI.

### Waveshare exact physical candidate
- source `87841ecee727fe1d814d4186be8c8c26e4afafb4`
- tree `6ddd7900f9b4ece0fba9349b905e1c078fc3401e`
- package run `33622358267`
- artifact `9843536218` / `waveshare-800x480-87841ece`
- ZIP digest `sha256:89e621034d4c91096fc5d38dd57ac40eeeab34275e4af1fc0461b48575039096`
- app SHA256 `8be2a2aad5f223d8b9bca498db2e12c04f7f205feaa9908b7922c37421c46593`
- short physical display/touch/Alarms gate: PASS on this exact image
- resources during short gate: internal DMA remained >20 kB with no collapse trend
- first continuous soak: ~2 h / 121 consecutive one-minute samples plus 25 clean backend health rounds; ended when USB dock/power path disappeared, with no recorded firmware crash
- required uninterrupted >=4 h / >=240-sample same-image gate: **INCOMPLETE**

PR #142 puts the generic deterministic evidence parser, explicit human-observation final gate and immutable package verifier on current `dev`. Its defaults require >=14,400 s timestamp span, >=240 Screen-soak samples and the configured DMA/resource evidence; those defaults must not be lowered merely to obtain PASS. Tooling CI is not physical evidence.

PR #57/#20 remain unpromoted. Historical PR #67 remains tied to frozen-candidate/source-specific guards. After genuine final soak PASS, #25 backend parity and #26 persistence/ARM remain mandatory on the same accepted identity.

### Secure OTA
PR #52 is software-qualified on the frozen Phase-1 line. #86 must later reconcile it onto the intended accepted release baseline and physically prove invalid/interrupted upload, power loss, previous-slot boot, pending verification, mark-valid and rollback. No hardware PASS is inherited from source CI.

## Production inverter qualification boundary

Generic protections merged through #119 prevent guessed/unqualified positive writes; PR #112 keeps production release blocked when no actual production-approved non-simulator profile exists; PR #113 prevents pending profiles from claiming unproven transport. These do not approve any Huawei/GoodWe/Solis/FoxESS/Knox production mapping. #82 still requires exact official model/firmware/manual identity, physical read-only identity/telemetry/status verification, write/readback/rollback evidence and signed production approval.

Current official-document findings do not change that verdict: GoodWe public HT documentation identifies GW100K-HT communication as Modbus-RTU/SunSpec-compatible but does not provide the exact production control map used here; Huawei public documentation confirms the SUN2000-115KTL-M2 family, while a separately available SUN2000MB Modbus definition belongs to a different family and is not accepted as 115KTL-M2 register evidence.

## Stale/superseded rule

Stale PRs #77/#101/#104/#109/#115/#116/#118/#121/#123/#126/#128/#134/#139 and obsolete Waveshare promotion #46 are not current merge/evidence sources. Historical CI belongs only to its historical exact head.

## Required remaining release evidence

1. #87/#27 uninterrupted >=4 h Waveshare same-image evidence, then #25/#26.
2. #80 source-transition physical matrix.
3. #81 real site source-contact/polarity/meter mapping.
4. #82 exact official-manual + bench record for each production inverter profile.
5. #86 OTA rollback/interruption physical record on intended baseline.
6. #83 Modbus/network endurance, Grid/DG/mixed-source FAT and signed SAT.
7. Final release SHA/config/profile/artifact traceability with no critical blocker.

Partial physical intervals may not be combined to manufacture a required continuous PASS, final-acceptance thresholds may not be lowered to manufacture PASS, and no physical PASS transfers across changed source/artifact/config/profile identities unless explicitly proven and allowed by the governing gate.
