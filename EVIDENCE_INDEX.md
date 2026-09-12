# AISH-OS Evidence Index v8

Master program: #79. Evidence is valid only for the exact source/head/tree/artifact/config/profile/site identity named. Software CI and validators never substitute for physical acceptance.

## Integration reconciliation parent

Known parent before this revision: `dev` `c333db0752be89bdcb9bfbda01e2f19a1f05709f` after governed PR #188. The post-revision live `dev` head is deliberately not hard-coded here because a PR cannot know its own future merge SHA; fetch live repository state before every execution decision.

## Current software/evidence chain

| Capability | Exact evidence | State |
|---|---|---|
| Core runtime/config/Modbus safety | PR #99/#102/#108/#114/#117/#119 etc. | MERGED SOFTWARE |
| Rollback-safe OTA + always-on regression | PR #145/#148 | MERGED SOFTWARE |
| Site/inverter/generator/OTA/FAT physical validators | PR #151/#152/#156/#158/#160 | MERGED TOOLING ONLY |
| Industrial UI shell/operator/engineering/nav/browser chain | PR #165/#167/#169/#172/#173/#176 | MERGED SOFTWARE |
| Exact current Industrial UI Waveshare integration | PR #179 head `72a1a82a8fc5ad4406b5bd51fba1f80f9c182884` | SOFTWARE GREEN / DRAFT PHYSICAL-GATED |
| Industrial UI physical evidence authority | PR #181/#183 | MERGED TOOLING ONLY |
| Historical Waveshare graph retirement | PR #184 -> `2433d15b47777e038a1a34a5287ea412202761b5` | MERGED GOVERNANCE; HISTORICAL EVIDENCE PRESERVED |
| Rev-A H2 edge/routing repair | PR #185 + authoritative run `34702074827` | H2 CAD/ROUTING REQUALIFIED |
| Rev-A H4 fail-closed physical evidence tooling | PR #187 -> `1e7e75039ebf839ca0bc298beead03cc7fcb6e8c` | MERGED TOOLING; NO H4 PASS |
| Final release traceability manifest gate | PR #188 -> `c333db0752be89bdcb9bfbda01e2f19a1f05709f` | MERGED TOOLING; FINAL REAL EVIDENCE PENDING |

## Exact Industrial UI v1 physical candidate — #164/#174

The immutable candidate remains:

- PR #179, intentionally Draft until genuine #174 physical PASS
- source `72a1a82a8fc5ad4406b5bd51fba1f80f9c182884`
- tree `3069c65b4234fcd2b6418f9bbbe7859f1cd9abce`
- artifact `10293685030` / `industrial-ui-waveshare-800x480-candidate`
- artifact digest `sha256:44dc05fe2c6e61d3a8b5fdfc7c936937da948691a2038358d5c0b3c1008de541`
- application SHA256 `0bbdb75be4ea7c0337f07e83dbdd3e34736ce8f667a42aa11abeb5c638f60734`
- UF2 SHA256 `199feec247563130f800d25e2a7024ebbaf64a65d3d8b6c6c5c9ec4c242b2500`
- dependencies lock SHA256 `fd82d3c69b9507f0b4966e1d7a9479a9ee49e7cc58b8199966db75d4af86eb91`
- effective sdkconfig SHA256 `4df1cfd44cd617b35cf3700daf70fc8633ae05132b4d334b68675757f6fed822`
- build container `espressif/idf:v6.0.1`
- rollback enabled; compiled STA credentials absent; compiled Engineering prefill absent; both bench auth bypasses disabled; minimum native touch target 44 px.

PR #181 established the complete Network/source/alarm/Modbus physical evidence contract and exact starter `evidence/candidates/industrial_ui_72a1a82_physical_observations.json`. PR #183 corrected Transfer/ATS and synchronism to explicit topology-dependent applicability. No physical PASS exists yet for this candidate.

A genuine #174 PASS must physically prove native visual/touch/roles, Network workflow, Grid+Gen1..3 mapping, explicit optional Transfer/Sync applicability, alarms/sorts/role-gated ACK, real board↔bench simulator Modbus activity, browser/API/resource stability and one uninterrupted >=4 h / >=240-sample run.

## Retired historical Waveshare evidence — source `87841ece...`

- source `87841ecee727fe1d814d4186be8c8c26e4afafb4`
- tree `6ddd7900f9b4ece0fba9349b905e1c078fc3401e`
- artifact `9843536218`
- digest `sha256:89e621034d4c91096fc5d38dd57ac40eeeab34275e4af1fc0461b48575039096`
- application SHA256 `8be2a2aad5f223d8b9bca498db2e12c04f7f205feaa9908b7922c37421c46593`
- short display/touch/Alarms gate: PASS on this exact old identity only
- first continuous attempt: ~2 h / 121 samples before external interruption
- uninterrupted >=4 h / >=240 run: NOT COMPLETED
- backend parity/recovery and persistence/ARM final matrices: NOT COMPLETED
- issues #87/#24/#25/#26/#27: CLOSED `not_planned` as superseded
- PRs #20/#57/#67: CLOSED UNMERGED as superseded

This retirement preserves historical truth and removes a duplicate active release path. It never qualifies #174 or PR #179.

## Other held release physical authorities

### Generator source transition — #80
Draft PR #106 head `a1620789235d21b515f9f245f2329fab88b50558` remains software-GREEN and frozen for physical disposition. PR #151 validates supplied evidence. Genuine bench PASS is mandatory before replay/promotion.

### Site source commissioning — #81
PR #156 validates exact site-specific breaker/run/ATS/sync provenance, address/contact/mask/polarity, physical toggle, stale/recovery and meter CT/PT/type/word-order/scale/sign. Another site/model or kW-sign inference is not evidence.

### Production inverter profiles — #82
PR #158 validates staged exact-model qualification. Pending profiles remain write-locked. GoodWe GW100K-HT still needs exact official applicable HT control protocol; Huawei SUN2000-115KTL-M2 needs exact official applicable ME definitions; Solis exact S6 family is known but the available compiled map is non-authoritative for writes; Growatt/Knox/FoxESS exact installed model/protocol is unresolved. Every deployed profile still needs physical write/readback/rollback and signed approval.

### Secure OTA — #86
PR #152 validates the real-controller interruption/power-loss/rollback record on one exact intended final release identity. Build/CI evidence is insufficient.

### Integrated FAT/SAT — #83
PR #160 is the final physical evidence contract: genuine prerequisite references, complete Grid/Generator/mixed-source FAT, all three Modbus modes and degraded-peer/network/resource endurance, exact UI/OTA references and authorized signed SAT.

## Rev-A H2/H4 evidence

### Current clean H2 engineering checkpoint

- controlled input `15219f62ab354a75d4a24e8a8f85b6163ae05aa8`
- authoritative KiCad 10.0.5 run `34702074827` / run #224 — SUCCESS
- ERC 0
- critical preroute DRC 0
- routed connectivity 0 unconnected
- final DRC 0 violations / 0 unconnected pads / 0 footprint errors
- SI geometry PASS
- STEP/mechanical/enclosure/manufacturing exports PASS
- immutable H2 freeze `a877e5d844af114a6e4386f6294f514288ca5df6`
- tree `782189312aec046338d472858d65d8cb397bd473`
- engineering artifact `10300950516`, digest `sha256:771ae830fa908b444d0fbce74da6fc35596bee4debaf7004447436df9eba0501`
- RFQ/DFM candidate artifact `10300571374`, digest `sha256:27b1709537715f08e928e65137262553791a95f957c19703da8dc3a104db0d30`
- provider ZIP `Automatrix_PVDG_RevA_PROVIDER_RFQ_a877e5d844.zip`
- prototype CAD minima: 0.20 mm drill / 0.18 mm hole clearance / 0.25 mm copper-edge.

PR #185 repaired the edge/routing geometry without relaxing these committed minima. The current H2 is reproducible CAD/routing evidence and a controlled RFQ/DFM input. It is **not** intended-fabricator written acceptance.

### H4 evidence tooling

PR #187 merged:
- `tools/reva_h4_physical_acceptance_verify.py`
- `evidence/templates/reva_h4_physical_observations.json`
- regression tests and CI proving the unexecuted starter cannot pass.

#162 remains open. Intended-fabricator DFM, controlled fabrication/board identity and actual H4 physical execution are still required.

## Final release traceability tooling — #91

PR #188 merged `tools/release_traceability_verify.py`, regression tests, CI and deliberately unexecuted starter `evidence/candidates/final_release_traceability_observations.json`.

The final manifest requires exact final release SHA/tree/artifact/application/config/site-map/profile-manifest identity plus genuine accepted evidence for Industrial UI, generator transition, site commissioning, production inverter profiles, OTA and FAT/SAT. Generator/site/OTA/FAT bind to the exact final release. Industrial UI may use governed replay only with no behavior-affecting change and explicit replay/approval evidence. Signed SAT, an authorized representative, release evidence package digest and zero critical blockers are mandatory. Rev-A H4 is separate unless the final firmware release explicitly declares it coupled.

The current starter is **not** release evidence and is required by CI to fail validation until genuine final records populate it.

## Final release evidence outputs still required

1. #174 exact PR #179 physical PASS.
2. #80 generator/source-transition physical PASS and governed replay/promotion.
3. #81 real site source/meter mapping PASS.
4. #82 exact deployed inverter profile production approvals.
5. #86 exact-release OTA physical PASS.
6. #83 integrated FAT/endurance and authorized signed SAT.
7. Populate PR #188 final manifest with exact accepted identities; validator PASS and zero critical blockers.

Rev-A intended-fabricator DFM/H4 remains a separate product-hardware track unless explicitly coupled to the firmware release. Partial physical intervals cannot be combined, thresholds cannot be lowered, optional topology cannot be invented, and no validator manufactures a physical PASS.
