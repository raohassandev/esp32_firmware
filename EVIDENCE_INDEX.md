# AISH-OS Evidence Index v4

Master program: #79. Evidence is valid only for the exact source/head/artifact/config/profile/site identity named. Software CI and validators never substitute for physical acceptance.

## Live integration baseline

Current `dev`: `14d13a0d6e5c4b4b95cea35b8cc32f1880ae8134` after governed PR #176.

## Current merged release/evidence chain

| Capability | Exact evidence | State |
|---|---|---|
| Waveshare post-soak validation | PR #150 -> `184d7e658ac44496a4f9efe0fd5db5844ad7fa43` | MERGED TOOLING ONLY |
| Generator/source-transition physical record validation | PR #151 -> `892a5811160098a765df7895af943eadf0457d48` | MERGED TOOLING ONLY |
| Secure OTA real-controller record validation | PR #152 -> `ad651806edb95a749b7d65b61fe1f6b2cf2148db` | MERGED TOOLING ONLY |
| Real site source commissioning validation | PR #156 -> `1c6e1de9ba01c759bc7dc6331f418160614cbbd7` | MERGED TOOLING ONLY |
| Inverter per-model physical production qualification | PR #158 -> `56e2abfb9291b8b5f0786dc8051820a53865984b` | MERGED TOOLING ONLY |
| Waveshare physical soak capture executor | PR #159 -> `3fd831b677ff590c54cb5cef412a55c9cdea5ca8` | MERGED TOOLING ONLY |
| Integrated FAT/endurance/signed-SAT evidence gate | PR #160 -> `1b4d7631862afdb38da99fbbae9aa170729b0bdb` | MERGED TOOLING ONLY |
| Governance reconciliation through PR #160 | PR #161 -> `72e817140a27f9833d79662a0d9b994e63477906` | MERGED |
| Industrial UI shell/design system | PR #165 head `75d42885a4baa92ef57bb674416671108f59a656` -> `ad4a091c267e9fc11e0903604fee5c8369da2488` | MERGED SOFTWARE |
| Industrial Operator actionable workflow | PR #167 head `57e243ba8a7ec0870c3e820637cf8809d56eb27d` -> `8fd6f1988ea32ba08b86872e62d873388abbed8f` | MERGED SOFTWARE |
| Industrial Engineering workspace | PR #169 head `1f63573f591ea974ba8debc6800f687c148e5125` -> `77e9c9d7046549970dc9bc58bd683304d4f1ced3` | MERGED SOFTWARE |
| Single Industrial UI nav ownership | PR #172 -> `4029a86ac15261a424a77401443680b551c7609f` | MERGED SOFTWARE |
| Browser socket/LRU/N16R8 PSRAM resilience | PR #173 -> `ada5cc8010183a69e831260b8d8bf36c1bb0dbed` | MERGED REGRESSION GATE |
| Industrial UI exact-image physical evidence validation | PR #175 head `931534dd490e95ee683b6c78904fde278add3111` -> `9a22d56b9749a7689581e2b8f5e92df3c1e58038` | MERGED TOOLING ONLY |
| Final task-based Operator IA | PR #176 head `4dfd09b76f9eb678ac1f6411f60904a5340271cd`; Industrial UI `33883395788`, physical tools `33883395629`, poller `33883395606`, OTA `33883395684`, browser resilience `33883395506`, full Firmware/Web/ESP32-S3 `33883395633` GREEN -> merge `14d13a0d6e5c4b4b95cea35b8cc32f1880ae8134` | MERGED SOFTWARE |

## Held physical/runtime identities

### Historical Waveshare exact candidate — #87/#27

- source `87841ecee727fe1d814d4186be8c8c26e4afafb4`
- tree `6ddd7900f9b4ece0fba9349b905e1c078fc3401e`
- package run `33622358267`
- artifact `9843536218` / `waveshare-800x480-87841ece`
- ZIP digest `sha256:89e621034d4c91096fc5d38dd57ac40eeeab34275e4af1fc0461b48575039096`
- app SHA256 `8be2a2aad5f223d8b9bca498db2e12c04f7f205feaa9908b7922c37421c46593`
- short display/touch/Alarms physical gate: PASS
- first continuous attempt: ~2 h / 121 one-minute samples + 25 clean backend rounds, interrupted by USB dock/power disappearance
- required new uninterrupted >=4 h / >=240 same-image run: PENDING

PR #159 supplies fail-closed serial/backend capture; PR #142 final/package validation; PR #150 post-soak parity/persistence validation. None creates the physical PASS. This identity predates Industrial UI v1 and cannot qualify #174.

### Industrial UI v1 exact-image physical candidate — #164/#174

Software is complete on current `dev` through PR #176, but **no immutable physical candidate has yet been selected and no physical PASS exists**.

Before #174 physical execution freeze and record:
- exact source SHA and Git tree;
- exact firmware/package artifact id and immutable digest;
- application image SHA256;
- exact Waveshare/LVGL build configuration;
- persisted configuration identity relevant to UI/runtime;
- fresh exact-head Firmware/Web/ESP32-S3, Industrial UI, browser resilience and physical-tool CI for the same source.

Then execute native 800x480 Overview/Grid/Solar/Alarms/Readiness, Engineering Commission/Configure/Service, Guided Commissioning/expert Service routes, light/dark readability, touchscreen and role checks, browser/API/history responsiveness, resource/fatal counters and one uninterrupted >=4 h / >=240-sample same-image run. PR #175 validates supplied evidence but cannot observe the panel.

### Generator source transition

Draft PR #106 exact head `a1620789235d21b515f9f245f2329fab88b50558` remains software-GREEN and frozen for #80 physical disposition. After genuine bench PASS, production promotion requires an equivalent current-`dev` replay, fresh exact-head CI and 0-behind expected-head merge.

### Site source commissioning

PR #156 provides the fail-closed evidence contract. Physical PASS requires exact site-specific breaker/run/ATS/synchronism provenance, manual/wiring reference, address/contact/mask/polarity, physical toggle, stale/recovery and meter sign/scaling proof. Another site/model or kW-sign inference is not evidence.

### Production inverter profiles

PR #158 provides staged documented -> read-only qualified -> write qualified -> production approved evidence validation. Generic core safety remains merged, but no model becomes production-approved without exact applicable manufacturer manual/model/firmware plus physical identity/telemetry/status/write/readback/rollback and signed approval.

### Secure OTA

Rollback-safe OTA software is PR #145; always-on regression is PR #148; physical evidence validation is PR #152. #86 remains physical on one exact intended OTA-capable release identity. No historical or Industrial UI HMI PASS automatically transfers to a different OTA image.

### Integrated FAT/SAT

PR #160 is the final release evidence contract. It requires genuine prerequisite physical PASS references, complete Grid/Generator/mixed-source FAT, all three Modbus connection modes and degraded-peer/network/resource endurance, zero fatal/reset/resource-collapse counters, exact OTA physical evidence, exact accepted UI release identity and authorized signed SAT.

## Rev-A H2/H3 evidence

Historical KiCad release run `33797012638` produced routed checkpoint `324e0db1600c2fd883d83f923a0c442669b237f0`, a marker claiming `ERC=0`, `DRC=0`, `UNCONNECTED=0`, L2 ground/SI PASS and STEP PASS, plus provider artifact `9909976209` digest `sha256:869bc723cd05f106aab850aa3de65bb4b46d600b77bc08e91dbedcaef41bd496` and engineering artifact `9909977211` digest `sha256:246830e56b8a17be3a0057186e7e30102c5e5dd371fb3bf4e64c2279502e8ea7`.

That historical `DRC=0` result is **not currently reproducible from the frozen checkpoint**. PR #163 head `f020be6bcabc8dec6c05d80aaf00ec47fe6476b4` deliberately removed a post-checkpoint `.kicad_dru` relaxation and replayed the original KiCad 10.0.5 upgrade/refill/save DRC semantics on a byte-for-byte copy. Run `33884657384` passed frozen checkpoint provenance, schematic ERC, netlist/design-control/HW-interface/power-budget checks, SI geometry, statistics and STEP, but DRC still returned **20 violations / 0 unconnected**: four USB-C J2 internal hole-clearance violations, two J3 RJ45 copper-edge violations, two U1 ESP32 copper-edge violations and twelve U1 thermal-via minimum-drill violations. The run uploaded evidence artifact `9941333133`, digest `sha256:668ab99694a0408b673b6b2875d396286bf965dce1a01f1de7de09c8540760b3`.

Therefore PR #163 must not be merged as a deterministic H2 PASS and the historical provider package must not be treated as the final fabrication package. Rev-A now requires a **new controlled H2 acceptance**: justify any footprint/fabrication exceptions from authoritative component/fabricator evidence, commit the approved rule context before the checkpoint, rerun ERC/DRC/SI/STEP/provider-package gates, and mint a new exact H2 checkpoint/package identity. H2/H3 automation never substitutes for H4 fabricated-prototype acceptance.

## Remaining release evidence outputs

1. #87/#27 historical new uninterrupted Waveshare >=4 h / >=240 sample same-image PASS, then #25/#26 on that same historical identity.
2. #174 new Industrial UI exact-image native 800x480/touch/browser/resource >=4 h PASS on its own immutable candidate.
3. #80 generator/source-transition physical matrix.
4. #81 real site source-contact/polarity/meter mapping PASS.
5. #82 per-deployed-model official-manual + physical production approval.
6. #86 complete secure OTA real-controller matrix.
7. #83 integrated Grid/DG/mixed-source FAT, Modbus/network endurance and signed SAT.
8. Rev-A new controlled H2 checkpoint/package, then #162 H4 fabricated prototype qualification.
9. Final release SHA/config/profile/source-map/artifact/UI index with no critical blocker.

Partial physical intervals cannot be combined to manufacture continuity, thresholds cannot be lowered to manufacture PASS, and no physical PASS transfers across changed identities unless the governing gate explicitly permits and proves equivalence.
