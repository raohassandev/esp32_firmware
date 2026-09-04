# AISH-OS Evidence Index v3

Master program: #79. Evidence is valid only for the exact source/head/artifact/config/profile/site identity named. Software CI and validators never substitute for physical acceptance.

## Live integration baseline

Current `dev`: `1b4d7631862afdb38da99fbbae9aa170729b0bdb` after PR #160.

## Current merged release/evidence chain

| Capability | Exact evidence | State |
|---|---|---|
| Waveshare post-soak validation | PR #150 -> `184d7e658ac44496a4f9efe0fd5db5844ad7fa43` | MERGED TOOLING ONLY |
| Generator/source-transition physical record validation | PR #151 -> `892a5811160098a765df7895af943eadf0457d48` | MERGED TOOLING ONLY |
| Secure OTA real-controller record validation | PR #152 -> `ad651806edb95a749b7d65b61fe1f6b2cf2148db` | MERGED TOOLING ONLY |
| Governance reconciliation through PR #152 | PR #153 -> `df815696fb201f36d46846e4efac0740274884a8` | MERGED |
| Real site source commissioning record validation | PR #156 head `10329c741d3d78672eac3111ad402b714da29164`; focused `33796735953`; OTA `33796735766`; full `33796735830`; merge `1c6e1de9ba01c759bc7dc6331f418160614cbbd7` | MERGED TOOLING ONLY |
| Inverter per-model physical production qualification | PR #158 head `002a122c2152bad13cd44ba74c54b6439177df02`; focused `33797589153`; OTA `33797588865`; full `33797588858`; merge `56e2abfb9291b8b5f0786dc8051820a53865984b` | MERGED TOOLING ONLY |
| Waveshare physical soak capture executor | PR #159 head `c1c1b458812cbf13e287eb87180644c93a9dca62`; focused `33798050886`; OTA `33798050815`; full `33798050594`; merge `3fd831b677ff590c54cb5cef412a55c9cdea5ca8` | MERGED TOOLING ONLY |
| Integrated FAT/endurance/signed-SAT evidence gate | PR #160 head `86d9bd4595d3745edd3bb717b9fc92f1e3d3fde5`; focused `33829562747`; OTA `33829562751`; full `33829562735`; merge `1b4d7631862afdb38da99fbbae9aa170729b0bdb` | MERGED TOOLING ONLY |

## Held physical/runtime identities

### Waveshare exact candidate

- source `87841ecee727fe1d814d4186be8c8c26e4afafb4`
- tree `6ddd7900f9b4ece0fba9349b905e1c078fc3401e`
- package run `33622358267`
- artifact `9843536218` / `waveshare-800x480-87841ece`
- ZIP digest `sha256:89e621034d4c91096fc5d38dd57ac40eeeab34275e4af1fc0461b48575039096`
- app SHA256 `8be2a2aad5f223d8b9bca498db2e12c04f7f205feaa9908b7922c37421c46593`
- short display/touch/Alarms physical gate: PASS
- first continuous attempt: ~2 h / 121 one-minute samples + 25 clean backend rounds, interrupted by USB dock/power disappearance
- required new uninterrupted >=4 h / >=240 same-image run: PENDING

PR #159 now supplies fail-closed serial/backend capture on current `dev`; PR #142 supplies final/package validation; PR #150 supplies post-soak parity/persistence validation. None creates the physical PASS.

### Generator source transition

Draft PR #106 exact head `a1620789235d21b515f9f245f2329fab88b50558` remains software-GREEN and frozen for #80 physical disposition. After genuine bench PASS, production promotion requires an equivalent current-`dev` replay, fresh exact-head CI and 0-behind expected-head merge.

### Site source commissioning

PR #156 provides the current fail-closed evidence contract. Physical PASS requires exact site-specific breaker/run/ATS/synchronism provenance, manual/wiring reference, address/contact/mask/polarity, physical toggle, stale/recovery and meter sign/scaling proof. Another site/model or kW-sign inference is not evidence.

### Production inverter profiles

PR #158 provides staged documented -> read-only qualified -> write qualified -> production approved evidence validation. Generic core safety remains merged, but no model becomes production-approved without exact applicable manufacturer manual/model/firmware plus physical identity/telemetry/status/write/readback/rollback and signed approval.

### Secure OTA

Rollback-safe OTA software is PR #145; always-on regression is PR #148; physical evidence validation is PR #152. #86 remains physical on one exact intended OTA-capable release identity. No Waveshare short PASS transfers to a different OTA-enabled image.

### Integrated FAT/SAT

PR #160 is the final release evidence contract. It requires genuine prerequisite physical PASS references, complete Grid/Generator/mixed-source FAT, all three Modbus connection modes and degraded-peer/network/resource endurance, zero fatal/reset/resource-collapse counters, exact OTA physical evidence, and authorized signed SAT bound to one release identity.

## Rev-A H2/H3 evidence

Successful KiCad release run `33797012638` proved routed design/provider-package automation:
- source for successful route workflow `0330eed27eda84fb08ecf3cb49345719229d1f01`;
- persisted routed native checkpoint `324e0db1600c2fd883d83f923a0c442669b237f0`;
- enforced `ERC=0`, `DRC=0`, `UNCONNECTED=0`, L2 ground PASS, critical-route SI geometry PASS and STEP PASS;
- provider package artifact `9909976209`, digest `sha256:869bc723cd05f106aab850aa3de65bb4b46d600b77bc08e91dbedcaef41bd496`;
- engineering evidence artifact `9909977211`, digest `sha256:246830e56b8a17be3a0057186e7e30102c5e5dd371fb3bf4e64c2279502e8ea7`.

PR #19 exact head `ad7417153d85ba60a440d161385793c21eac4076` is earning fresh PR CI before integration. H2/H3 evidence is not H4 fabricated-prototype acceptance.

## Remaining release evidence outputs

1. #87/#27 new uninterrupted Waveshare >=4 h / >=240 sample same-image PASS.
2. #25 backend parity/recovery and #26 persistence/ARM on that same accepted identity.
3. #80 generator/source-transition physical matrix.
4. #81 real site source-contact/polarity/meter mapping PASS.
5. #82 per-deployed-model official-manual + physical production approval.
6. #86 complete secure OTA real-controller matrix.
7. #83 integrated Grid/DG/mixed-source FAT, Modbus/network endurance and signed SAT.
8. Final release SHA/config/profile/source-map/artifact index with no critical blocker.

Partial physical intervals cannot be combined to manufacture continuity, thresholds cannot be lowered to manufacture PASS, and no physical PASS transfers across changed identities unless the governing gate explicitly permits and proves equivalence.
