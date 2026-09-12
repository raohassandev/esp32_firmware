# AISH-OS Evidence Index v6

Master program: #79. Evidence is valid only for the exact source/head/tree/artifact/config/profile/site identity named. Software CI and validators never substitute for physical acceptance.

## Integration reconciliation parent

Known parent before this revision: `dev` `8016c005be8d548e9388026a89592b70b92ec1a3` after governed PR #182. The post-revision live `dev` head is deliberately not hard-coded here because a PR cannot know its own future merge SHA; fetch live repository state before every execution decision.

## Current software/evidence chain

| Capability | Exact evidence | State |
|---|---|---|
| Core runtime/config/Modbus safety | PR #99/#102/#108/#114/#117/#119 etc. | MERGED SOFTWARE |
| Rollback-safe OTA + always-on regression | PR #145/#148 | MERGED SOFTWARE |
| Site/inverter/generator/OTA/FAT physical validators | PR #151/#152/#156/#158/#160 | MERGED TOOLING ONLY |
| Historical Waveshare final/post-soak/capture tooling | PR #142/#150/#159 | MERGED TOOLING ONLY |
| Industrial UI shell/operator/engineering/nav/browser chain | PR #165/#167/#169/#172/#173/#176 | MERGED SOFTWARE |
| Governance through Industrial UI v1 | PR #177 -> `150118a8425462e1756b115ddf3d463a022b395f` | MERGED |
| Exact current Industrial UI Waveshare integration | PR #179 head `72a1a82a8fc5ad4406b5bd51fba1f80f9c182884` | SOFTWARE GREEN / DRAFT PHYSICAL-GATED |
| Industrial UI physical evidence authority v2 | PR #181 -> `7cb824a2341cb9072b1a3176afbd3685dec1a32a` | MERGED TOOLING ONLY |
| Eight-file governance reconciliation | PR #182 -> `8016c005be8d548e9388026a89592b70b92ec1a3` | MERGED |
| Industrial UI physical evidence v3 topology correction | PR #183 | CI/PROMOTION GATED; NO FIRMWARE CHANGE |

## Exact Industrial UI v1 physical candidate — #164/#174

The immutable candidate remains:

- PR: #179 (intentionally Draft until genuine #174 physical PASS)
- source: `72a1a82a8fc5ad4406b5bd51fba1f80f9c182884`
- tree: `3069c65b4234fcd2b6418f9bbbe7859f1cd9abce`
- artifact id/name: `10293685030` / `industrial-ui-waveshare-800x480-candidate`
- artifact digest: `sha256:44dc05fe2c6e61d3a8b5fdfc7c936937da948691a2038358d5c0b3c1008de541`
- application SHA256: `0bbdb75be4ea7c0337f07e83dbdd3e34736ce8f667a42aa11abeb5c638f60734`
- UF2 SHA256: `199feec247563130f800d25e2a7024ebbaf64a65d3d8b6c6c5c9ec4c242b2500`
- dependencies lock SHA256: `fd82d3c69b9507f0b4966e1d7a9479a9ee49e7cc58b8199966db75d4af86eb91`
- effective sdkconfig SHA256: `4df1cfd44cd617b35cf3700daf70fc8633ae05132b4d334b68675757f6fed822`
- build container: `espressif/idf:v6.0.1`
- rollback: enabled
- compiled STA credentials: absent
- compiled Engineering credential prefill: absent
- Engineering HTTP bench bypass: disabled
- Network-page bench bypass: disabled
- minimum native touch target: 44 px

The exact PR #179 candidate earned current root firmware/web checks, exact Waveshare ESP-IDF build/package, immutable dependency-lock/config checks, UF2 generation and independent diagnostic build. These are software/build prerequisites, not a panel PASS.

PR #181 established the full Network/source/alarm/Modbus physical evidence contract and exact starter `evidence/candidates/industrial_ui_72a1a82_physical_observations.json`. The starter is explicitly `UNEXECUTED_TEMPLATE_NOT_A_PHYSICAL_PASS`.

PR #183 corrects the only discovered v2 contract mismatch: #174 says Transfer/ATS and synchronism are optional when configured, while v2 required both roundtrip flags unconditionally. V3 requires explicit boolean applicability for each optional channel. If configured, real round-trip PASS is mandatory and a skip reason is contradictory. If not configured/supported, roundtrip must remain false and a non-empty factual reason is mandatory. Applicability left `null` in the unexecuted starter prevents accidental PASS.

A genuine #174 PASS must physically prove on this exact image:
- native 800x480 visual hierarchy, touch behavior and Operator/Engineering role boundaries;
- Network layout/scan/select/manual SSID/password/connect/restart/Wi-Fi indicator/Engineering lock and observed connected RSSI;
- Grid + Generator 1..3 source mapping round trips;
- explicit Transfer/ATS and Sync applicability with configured=>real roundtrip or absent=>false+reason;
- save disabling current automatic control and no kW-sign source inference;
- alarm All/Active/Unack filters, Priority/State/ID sorts, Engineering acknowledgement and operator acknowledgement refusal;
- real board↔bench-simulator Modbus connection, request/success counters that increase, at least 3 decoded samples and decoded values following simulator changes;
- browser/API/history responsiveness, resource stability, no fatal/reset/wedge; and one uninterrupted >=4 h / >=240-sample run.

No physical PASS exists yet for this candidate.

## Historical Waveshare exact candidate — #87/#27

- source `87841ecee727fe1d814d4186be8c8c26e4afafb4`
- tree `6ddd7900f9b4ece0fba9349b905e1c078fc3401e`
- artifact `9843536218`
- digest `sha256:89e621034d4c91096fc5d38dd57ac40eeeab34275e4af1fc0461b48575039096`
- application SHA256 `8be2a2aad5f223d8b9bca498db2e12c04f7f205feaa9908b7922c37421c46593`
- short display/touch/Alarms gate: PASS
- first continuous attempt: ~2 h / 121 samples before external bench power-path interruption
- uninterrupted >=4 h / >=240 run: PENDING

This old identity predates the current Industrial UI and never qualifies #174. After its own final soak PASS, #25/#26 still require backend parity/recovery and persistence/ARM on that exact old image.

## Other held physical authorities

### Generator source transition — #80
Draft PR #106 head `a1620789235d21b515f9f245f2329fab88b50558` remains software-GREEN and frozen for physical disposition. PR #151 validates supplied evidence. Genuine bench PASS is mandatory before replay/promotion.

### Site source commissioning — #81
PR #156 validates exact site-specific breaker/run/ATS/sync provenance, address/contact/mask/polarity, physical toggle, stale/recovery and meter CT/PT/type/word-order/scale/sign. Another site/model or kW-sign inference is not evidence.

### Production inverter profiles — #82
PR #158 validates staged exact-model qualification. Pending catalogue entries remain write-locked until exact applicable official model/firmware/manual plus physical identity/telemetry/status/write/readback/rollback and signed production approval exist.

### Secure OTA — #86
PR #152 validates the real-controller interruption/power-loss/rollback record on one exact intended release identity. Build/CI evidence is insufficient.

### Integrated FAT/SAT — #83
PR #160 is the final evidence contract: genuine prerequisite physical references, complete Grid/Generator/mixed-source FAT, all three Modbus modes and degraded-peer/network/resource endurance, accepted exact UI/OTA references and authorized signed SAT.

## Rev-A H2/H3/H4 evidence

Historical provider artifact `9909976209` is evidence only. Reproducibility run `33884657384` could not reproduce historical `DRC=0` and returned 20 DRC violations / 0 unconnected. #178/#85 therefore require authoritative component/fabricator evidence for any exceptions, rules committed before a new checkpoint, and fresh ERC/DRC/SI/STEP/provider-package acceptance to a new H2 identity. #162 H4 physical qualification follows only from the new accepted package.

## Final release evidence outputs still required

1. #174 exact PR #179 candidate physical PASS.
2. #80 generator/source-transition physical PASS and governed replay/promotion.
3. #81 real site source/meter mapping PASS.
4. #82 exact deployed inverter profile production approvals.
5. #86 exact-release OTA physical PASS.
6. #83 integrated FAT/endurance and authorized signed SAT.
7. #91 final identity index with zero critical blockers.

Historical #87/#27/#25/#26 and Rev-A #178/#85/#162 remain separate tracked hardware lanes and must not be silently used as evidence for a changed release identity. Partial physical intervals cannot be combined, thresholds cannot be lowered, optional topology cannot be invented, and no validator manufactures a physical PASS.
