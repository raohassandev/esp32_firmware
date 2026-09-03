# AISH-OS Evidence Index v2

Master program: #79. Evidence is valid only for the exact source/head/artifact named. Software CI and validators never substitute for physical acceptance.

## Live integration baseline

Current `dev`: `ad651806edb95a749b7d65b61fe1f6b2cf2148db` after PR #152.

## Current merged software/tooling evidence chain

Earlier governed software evidence through PR #149 remains authoritative for its exact heads/merges. The current post-#149 chain is:

| Capability | Exact evidence | State |
|---|---|---|
| Governance through PR #148 | PR #149 head `ed48bd91ec3c6b669be0f0d33ca50e129be735fc`; OTA always-on `33779699849`; full `33779699784`; merge `eee505bc3fcb07640836fa79c6becfc629c6050b` | MERGED |
| Waveshare post-soak evidence validation | PR #150 head `93b798e5180df04d6587ded7e1aee0fa3a672e3b`; focused `33792231764`; OTA always-on `33792231776`; full `33792231708`; merge `184d7e658ac44496a4f9efe0fd5db5844ad7fa43` | MERGED TOOLING ONLY |
| Generator/source-transition physical evidence validation | PR #151 head `d3a70ec8dad791e6a5fd00ff65e9b351455db184`; physical tools `33793093720`; strong-evidence `33793093627`; OTA always-on `33793093852`; full `33793093622`; merge `892a5811160098a765df7895af943eadf0457d48` | MERGED TOOLING ONLY |
| Secure OTA real-controller physical evidence validation | PR #152 head `294ed08a8f703d33151fb2ec38ca76da20f6aa54`; physical tools `33793934963`; secure OTA current-dev `33793934980`; OTA always-on `33793934865`; full `33793934989`; merge `ad651806edb95a749b7d65b61fe1f6b2cf2148db` | MERGED TOOLING ONLY |

Key existing release-critical software remains:
- Modbus connection modes PR #99 and bounded endpoint admission PR #114.
- Inverter generic identity/command/write authority protections through PR #119, with production release blocked by PR #112 and pending transports truthfully unqualified by PR #113.
- Operator continuity/truthful Plant verdict PR #144.
- Rollback-safe OTA PR #145 and always-on OTA regression PR #148.
- Generic Waveshare final acceptance/package tooling PR #142.

## Held runtime / physical source identities

### Generator source transition
Draft PR #106 head `a1620789235d21b515f9f245f2329fab88b50558` is software-GREEN but remains physically gated by #80. PR #151 validates the eventual evidence record only. Its physical PASS cannot be inferred from CI. Because `dev` has advanced, any eventual production merge must replay the identical validated runtime slice onto current `dev`, earn fresh exact-head CI and be 0-behind.

### Waveshare exact physical candidate
- source `87841ecee727fe1d814d4186be8c8c26e4afafb4`
- tree `6ddd7900f9b4ece0fba9349b905e1c078fc3401e`
- package run `33622358267`
- artifact `9843536218` / `waveshare-800x480-87841ece`
- ZIP digest `sha256:89e621034d4c91096fc5d38dd57ac40eeeab34275e4af1fc0461b48575039096`
- app SHA256 `8be2a2aad5f223d8b9bca498db2e12c04f7f205feaa9908b7922c37421c46593`
- short physical display/touch/Alarms gate: PASS on this exact image
- first continuous soak: ~2 h / 121 consecutive one-minute samples plus 25 clean backend rounds; ended when USB dock/power path disappeared, with no recorded firmware crash
- required uninterrupted >=4 h / >=240-sample same-image gate: **INCOMPLETE**

PR #142 provides deterministic final-soak/package validation on current `dev`. PR #150 provides backend-parity/recovery and persistence/ARM evidence validators that may only be used after genuine final-soak PASS. Neither tooling merge transfers or creates a physical PASS.

### Secure OTA
Current OTA software baseline is PR #145, always-on regression is PR #148, and real-controller evidence validation is PR #152. #86 must still physically prove authenticated upload, invalid rejection before write, interruption, power loss, partial-image non-selection, previous-slot recovery, pending verification, mark-valid stabilization and deliberate rollback on one exact intended OTA-capable release artifact. The frozen `87841ece...` physical PASS does not transfer to the newer OTA-enabled integration image.

## Production inverter qualification boundary

Generic protections prevent guessed/unqualified positive writes. No Huawei/GoodWe/Solis/FoxESS/Knox pending entry is production-approved from generic core CI or simulator evidence.

Current official-source findings remain insufficient for production promotion:
- GoodWe GW100K-HT official public documentation confirms Modbus-RTU/SunSpec-compatible communications, but the exact HT production register/control protocol is not present in the public user manual and remains required for the deployed firmware/topology.
- Huawei official documentation confirms the SUN2000-115KTL-M2 product family, but an exact applicable official Modbus interface/control definition for the deployed model/firmware remains required; a different SUN2000 family definition is not accepted as substitute evidence.

#82 therefore still requires exact official model/firmware/manual identity, physical read-only identity/telemetry/status verification, write/readback/rollback evidence and signed production approval.

## Required remaining release evidence

1. #87/#27 uninterrupted >=4 h Waveshare same-image evidence, then #25/#26 on the exact accepted identity.
2. #80 source-transition physical matrix, with PR #151 record validation.
3. #81 real site source-contact/polarity/meter mapping.
4. #82 exact official-manual + bench record for each production inverter profile.
5. #86 OTA rollback/interruption physical record on one exact intended OTA-capable release identity, with PR #152 record validation.
6. #83 Modbus/network endurance, Grid/DG/mixed-source FAT and signed SAT.
7. Final release SHA/config/profile/artifact traceability with no critical blocker.

Partial physical intervals may not be combined to manufacture a continuous PASS, acceptance thresholds may not be lowered to manufacture PASS, and no physical PASS transfers across changed source/artifact/config/profile identities unless explicitly proven and allowed by the governing gate.
