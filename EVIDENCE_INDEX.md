# AISH-OS Evidence Index v2

Master program: #79. Evidence is valid only for the exact source/head/artifact named. Software CI never substitutes for physical acceptance.

## Live integration baseline

Current `dev`: `dd10809f4246713ab99b3ccc9c3b515ece94fd0d` after governed PR #138 merge.

## Current merged software evidence chain

| Capability | Exact evidence | State |
|---|---|---|
| AISH-OS v2 governance | PR #97 -> `430e9157eb82196501f896d9323da16c86f9255e`; full `33716216708` | MERGED |
| Modbus connection modes | PR #99 -> `3aa69162a6847a852e9b648ef8ec6988f5e3f296` | MERGED |
| Production Wi-Fi credential protection | PR #100 -> `7d9abdccb032b387525df9240b2943b74324a8e9` | MERGED |
| Inverter identity revalidation | PR #102 -> `b60bcb9474f4ed7dd0b0ed631410b54628a47d01` | MERGED |
| Requirements reconciliation | PR #103 -> `81b02a4b6188b9f7150d9161883f0465e14ba6f5` | MERGED |
| Engineering DOM/error stability | PR #105 -> `6569e36e019adf4b890d04f163091f04eec6020b` | MERGED |
| Web spinlock/nonblocking regression | PR #107 -> `6ae86b09294b3e3a2c8a8eaf085ce5c35c69cf74` | MERGED |
| Inverter command schema safety | PR #108 -> `b8996d5f834cc8edeb084f56c802ff5fc6ecd04d` | MERGED |
| Production profile release gate | PR #112 -> `7a683092696c6334870b5c1007e086f8556dbc88` | MERGED |
| Manufacturer transport truthfulness | PR #113 -> `4aa935dfcf944f83cbb96333be67f99878e32c30` | MERGED |
| Modbus cumulative-deadline endpoint admission | PR #114 -> `fa8ca9b17e08f2478e104942b9d6dbfad4f0ca7f` | MERGED |
| Profile assignment ordering interlock | PR #117 -> `1360c4a8356ff8acdc19878f65da311c0b0eccc6` | MERGED |
| Production inverter write authority | PR #119 -> `d9cd81bcf500a034d6cc88ea92e3bb74e42ed258` | MERGED |
| Legacy config migration OOM preservation | PR #122 -> `dfe93de50e2a5715f4d212ff3233d566d36e2cfd`; focused `33738503242`, full `33738503251` | MERGED |
| Atomic safety-alarm snapshot | PR #124 -> `3096f2bfa10e86b3163b99ae7622bffded6791ac`; focused `33739241779`, full `33739241807` | MERGED |
| Runtime config/mapping disable interlock | PR #127 -> `df282a3e8afee27dfc220694e4461e4ad49d2277`; focused `33741274303`, full `33741274300` | MERGED |
| Wi-Fi configuration runtime-disable interlock | PR #129 -> `5e5a63dba3e157d2658227ff691e9f975cedff96`; focused `33745056243`, full `33745056135` | MERGED |
| Source-detection runtime-disable interlock | PR #130 -> `0969a119e4fcb97405da26a59b55ec44a5a292f4`; focused `33745904234`, full `33745904243` | MERGED |
| Commissioning mutation interlock inventory | PR #131 -> `41eaf22f8b92057cdbe5427c590ccd84d7fbce9b`; focused `33749197443`, full `33749196739` | MERGED |
| Governance through PR #131 | PR #132 -> `2e0c946d30027419dfbd0723598ac34315cf6a86` | MERGED |
| Wi-Fi scan result lifetime cleanup | PR #133 head `9ba7ec20cc396a46fae015201ed06f1a5ccb9111`; full `33755126372`; merge `e918aef8465435d4af87eaa6c1f001767a9d2170` | MERGED |
| Schema-6 JSON depth guard | PR #135 head `71015e952fc8df5bcd40877027eb590ea9f8fb24`; focused `33756048306`, full `33756048441`; merge `ad6316b5e3dec6bca630d351a80c8e786fd61b69` | MERGED |
| Inverter profile store stack headroom | PR #136 head `9ac9ec4718920c4b8f9f829542d2b9c266911872`; focused `33756766924`, full `33756767088`; merge `321198bb5d970aa5f4842331a229ce63475e0776` | MERGED |
| Schema-6 config stack headroom | PR #137 head `2f34e60ab8e42b088babdeaa39e4c0c738f00c56`; focused `33761025944`, full `33761025927`; merge `43d4bd509ea07aadbd9ea18e9813e3ec11c60297` | MERGED |
| Project-wide app-config stack safety contract | PR #138 head `316a619608c6e83f7af9444ad0721b4300df7535`; focused `33762133658`, full `33762133649`; 0-behind expected-head merge `dd10809f4246713ab99b3ccc9c3b515ece94fd0d` | MERGED |

PR #134 is stale/superseded by #135 and is not current evidence.

## Held runtime / physical source identities

### Generator source transition
Draft PR #106 remains software-GREEN but physically gated by #80. CI does not establish physical PASS. Because `dev` has advanced, any eventual merge must replay only the identical validated slice onto current `dev`, obtain fresh exact-head CI and be 0-behind.

### Waveshare exact physical candidate
- source `87841ecee727fe1d814d4186be8c8c26e4afafb4`
- tree `6ddd7900f9b4ece0fba9349b905e1c078fc3401e`
- package run `33622358267`
- artifact `9843536218` / `waveshare-800x480-87841ece`
- ZIP digest `sha256:89e621034d4c91096fc5d38dd57ac40eeeab34275e4af1fc0461b48575039096`
- app SHA256 `8be2a2aad5f223d8b9bca498db2e12c04f7f205feaa9908b7922c37421c46593`
- short physical display/touch/Alarms gate: PASS on this exact image
- resources during short gate: internal DMA remained >20 kB with no collapse trend
- first continuous soak: ~2 h / 121 consecutive one-minute samples plus 25 clean backend rounds; ended when USB dock/power path disappeared with no recorded firmware crash
- required uninterrupted >=4 h / >=240-sample same-image gate: **INCOMPLETE**

PR #57/#20 remain unpromoted. After genuine final soak PASS, #25 backend parity and #26 persistence/ARM remain mandatory on the same accepted identity.

### Secure OTA
PR #52 is software-qualified on the frozen Phase-1 line. #86 must later reconcile it onto the intended accepted release baseline and physically prove invalid/interrupted upload, power loss, previous-slot boot, pending verification, mark-valid and rollback. No hardware PASS is inherited from source CI.

## Production inverter qualification boundary

Generic protections prevent guessed/unqualified positive writes; PR #112 keeps production release blocked when no actual production-approved non-simulator profile exists; PR #113 prevents pending profiles from claiming unproven transport. #82 still requires exact official model/firmware/manual identity, physical read-only identity/telemetry/status verification, write/readback/rollback evidence and signed production approval for each production model.

## Stale/superseded rule

Stale PRs #77/#101/#104/#109/#115/#116/#118/#121/#123/#126/#128/#134 and obsolete Waveshare promotion #46 are not current merge/evidence sources. Historical CI belongs only to its historical exact head.

## Required remaining release evidence

1. #87/#27 uninterrupted >=4 h Waveshare same-image evidence, then #25/#26.
2. #80 source-transition physical matrix.
3. #81 real site source-contact/polarity/meter mapping.
4. #82 exact official-manual + bench record for each production inverter profile.
5. #86 OTA rollback/interruption physical record on intended baseline.
6. #83 Modbus/network endurance, Grid/DG/mixed-source FAT and signed SAT.
7. Final release SHA/config/profile/artifact traceability with no critical blocker.

Partial physical intervals may not be combined to manufacture a required continuous PASS, and no physical PASS transfers across changed source/artifact/config/profile identities unless explicitly proven and allowed by the governing gate.
