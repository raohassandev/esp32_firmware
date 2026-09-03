# AISH-OS Evidence Index v2

Master program: #79. Evidence is valid only for the exact source/head/artifact named. Software CI never substitutes for physical acceptance.

## Live integration baseline

Current `dev`: `1a3b0f4ee73ae08588caee7b46f9ab87d1e5b491` after governed PR #148 merge.

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
| Inverter command schema safety | PR #108; merge `b8996d5f834cc8edeb084f56c802ff5fc6ecd04d` | MERGED |
| Production profile release gate | PR #112; merge `7a683092696c6334870b5c1007e086f8556dbc88` | MERGED |
| Manufacturer transport truthfulness | PR #113 -> `4aa935dfcf944f83cbb96333be67f99878e32c30` | MERGED |
| Modbus cumulative-deadline endpoint admission | PR #114; merge `fa8ca9b17e08f2478e104942b9d6dbfad4f0ca7f` | MERGED |
| Profile assignment ordering interlock | PR #117; merge `1360c4a8356ff8acdc19878f65da311c0b0eccc6` | MERGED |
| Production inverter write authority | PR #119; merge `d9cd81bcf500a034d6cc88ea92e3bb74e42ed258` | MERGED |
| Legacy config migration OOM preservation | PR #122; focused `33738503242`, full `33738503251`; merge `dfe93de50e2a5715f4d212ff3233d566d36e2cfd` | MERGED |
| Atomic safety-alarm snapshot | PR #124; focused `33739241779`, full `33739241807`; merge `3096f2bfa10e86b3163b99ae7622bffded6791ac` | MERGED |
| Runtime config/mapping disable interlock | PR #127; merge `df282a3e8afee27dfc220694e4461e4ad49d2277` | MERGED |
| Wi-Fi configuration runtime-disable interlock | PR #129; merge `5e5a63dba3e157d2658227ff691e9f975cedff96` | MERGED |
| Source-detection runtime-disable interlock | PR #130; merge `0969a119e4fcb97405da26a59b55ec44a5a292f4` | MERGED |
| Consolidated commissioning mutation interlock inventory | PR #131; focused `33749197443`, full `33749196739`; merge `41eaf22f8b92057cdbe5427c590ccd84d7fbce9b` | MERGED |
| Wi-Fi scan result lifetime cleanup | PR #133; full `33755126372`; merge `e918aef8465435d4af87eaa6c1f001767a9d2170` | MERGED |
| Schema-6 JSON depth guard | PR #135; focused `33756048306`, full `33756048441`; merge `ad6316b5e3dec6bca630d351a80c8e786fd61b69` | MERGED |
| Inverter profile store stack headroom | PR #136; focused `33756766924`, full `33756767088`; merge `321198bb5d970aa5f4842331a229ce63475e0776` | MERGED |
| Schema-6 config stack headroom | PR #137; focused `33761025944`, full `33761025927`; merge `43d4bd509ea07aadbd9ea18e9813e3ec11c60297` | MERGED |
| Runtime-component app-config stack safety contract | PR #138; focused `33762133658`, full `33762133649`; merge `dd10809f4246713ab99b3ccc9c3b515ece94fd0d` | MERGED |
| Project-wide app-config stack guard completion | PR #140; focused `33765114501`, full `33765114492`; merge `093954b5626c034e126fde3b773cedb1add92707` | MERGED |
| Governance through PR #140 | PR #141; full `33766240186`; merge `2272caefa87581f27e815ce4420a5880d2d16e38` | MERGED |
| Current-dev Waveshare final acceptance/package tooling | PR #142 head `a4a0a64210b5a05acd02cdb1016948d593c8d213`; focused `33768630723`; full `33768630667`; merge `d07dca2d2b20a2cf4e712df45fae9dfe7e3024c2` | MERGED TOOLING ONLY |
| Governance through PR #142 | PR #143; full `33769494156`; merge `353c0a50cb0243edd5a73d58313f13623048b273` | MERGED |
| Operator continuity / truthful Plant verdict / Theme menu bridge | PR #144 head `890f3cd66aa693a214d4edc012770b951307f259`; focused `33771513776`; full `33771513697`; merge `8b5fce29aaf0de7ec9a5531ad3ea66c78e4539ed` | MERGED; PR #54 SUPERSEDED |
| Rollback-safe secure OTA on current dev | PR #145 head `7ab58704c72cf61eca858e8004c12094a0d6bbe3`; Secure OTA `33773071036`; HTTP ownership `33773071278`; full `33773071302`; all triggered safety workflows GREEN; 0-behind expected-head merge `73bcb3e3a57dd482ac87b174906254ad60c8575b` | MERGED SOFTWARE; PHYSICAL #86 OPEN |
| Always-on secure OTA regression coverage | PR #148 head `9ed4ec82066ca1687a5a8ce4b4f2cf130281f44a`; always-on `33778878536`; full `33778878091`; 0-behind expected-head merge `1a3b0f4ee73ae08588caee7b46f9ab87d1e5b491` | MERGED REGRESSION GATE |

Historical PR #52 and #54 are closed/superseded by current-dev replays #145/#144. PR #134 is stale/superseded by #135. PR #139 was closed unmerged when #140 advanced `dev`; none is current evidence.

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

PR #142 puts the generic deterministic evidence parser, explicit human-observation final gate and immutable package verifier on current `dev`. Defaults require >=14,400 s timestamp span and >=240 Screen-soak samples plus resource/fatal-pattern criteria; they must not be lowered merely to obtain PASS. Tooling CI is not physical evidence.

PR #57/#20 remain unpromoted. Historical PR #67 remains tied to frozen-candidate/source-specific guards. After genuine final soak PASS, #25 backend parity and #26 persistence/ARM remain mandatory on the same accepted identity.

### Secure OTA
Current OTA software baseline is PR #145 / merge `73bcb3e3a57dd482ac87b174906254ad60c8575b`, with always-on regression coverage from PR #148 / merge `1a3b0f4ee73ae08588caee7b46f9ab87d1e5b491`. This is source/CI evidence only. #86 must still physically prove the OTA matrix on one exact intended OTA-capable release artifact after the Waveshare source graph is resolved. The frozen `87841ece...` physical PASS does not transfer to the newer OTA-enabled integration image.

## Production inverter qualification boundary

Generic protections merged through #119 prevent guessed/unqualified positive writes; PR #112 keeps production release blocked when no actual production-approved non-simulator profile exists; PR #113 prevents pending profiles from claiming unproven transport. These do not approve any Huawei/GoodWe/Solis/FoxESS/Knox production mapping. #82 still requires exact official model/firmware/manual identity, physical read-only identity/telemetry/status verification, write/readback/rollback evidence and signed production approval.

Current official-document findings do not change that verdict: GoodWe public HT documentation identifies GW100K-HT communication as Modbus-RTU/SunSpec-compatible but does not provide the exact production control map used here; Huawei public documentation confirms the SUN2000-115KTL-M2 family, while a separately available SUN2000MB Modbus definition belongs to a different family and is not accepted as 115KTL-M2 register evidence.

## Required remaining release evidence

1. #87/#27 uninterrupted >=4 h Waveshare same-image evidence, then #25/#26.
2. #80 source-transition physical matrix.
3. #81 real site source-contact/polarity/meter mapping.
4. #82 exact official-manual + bench record for each production inverter profile.
5. #86 OTA rollback/interruption physical record on one exact intended OTA-capable release identity.
6. #83 Modbus/network endurance, Grid/DG/mixed-source FAT and signed SAT.
7. Final release SHA/config/profile/artifact traceability with no critical blocker.

Partial physical intervals may not be combined to manufacture a required continuous PASS, final-acceptance thresholds may not be lowered to manufacture PASS, and no physical PASS transfers across changed source/artifact/config/profile identities unless explicitly proven and allowed by the governing gate.
