# AISH-OS Blocker Ledger v5

Master program: #79. Snapshot software baseline: `dev` `7cb824a2341cb9072b1a3176afbd3685dec1a32a` after PR #181. Known software and evidence-authority gaps are closed; remaining release blockers require genuine physical/site/manufacturer/fabricator execution or governed post-PASS promotion.

## B-001 — Industrial UI exact-image Waveshare physical acceptance

**Lane:** L16 / #164/#174  
**Exact candidate:** PR #179 head `72a1a82a8fc5ad4406b5bd51fba1f80f9c182884`; tree `3069c65b4234fcd2b6418f9bbbe7859f1cd9abce`  
**Artifact:** `10293685030`; digest `sha256:44dc05fe2c6e61d3a8b5fdfc7c936937da948691a2038358d5c0b3c1008de541`  
**Application SHA256:** `0bbdb75be4ea7c0337f07e83dbdd3e34736ce8f667a42aa11abeb5c638f60734`  
**State:** SOFTWARE + EVIDENCE AUTHORITY COMPLETE / PHYSICAL PANEL EXECUTION PENDING  
**Automation:** PR #181; identity-bound starter `evidence/candidates/industrial_ui_72a1a82_physical_observations.json`

Physically prove native 800x480 layout/touch/roles; Network scan/select/manual/connect/restart; Grid, Gen1..3, Transfer and Sync source mappings; alarm filters/sorts and Engineering-only acknowledgement; real board↔bench-simulator Modbus counter/value activity; browser/API/resource health; and one uninterrupted >=4 h / >=240-sample run. PR #179 remains Draft/frozen until genuine PASS. Historical Waveshare evidence cannot transfer.

## B-002 — Generator transition physical bench

**Lane:** L2 / #80  
**Runtime candidate:** Draft PR #106 head `a1620789235d21b515f9f245f2329fab88b50558`  
**State:** SOFTWARE GREEN / PHYSICAL BENCH PENDING  
**Automation:** PR #151

Physical Grid<->Transfer<->Generator, island/sync, stale/conflict/source-loss, source-contact evidence, meter sign/scaling and recovery dwell must pass. Then replay equivalent runtime behavior to current `dev` and re-earn exact-head CI before merge.

## B-003 — Real site source commissioning

**Lane:** L5 / #81  
**State:** PHYSICAL SITE INPUT/EXECUTION PENDING  
**Automation:** PR #156

Need exact breaker/run/ATS/sync provenance, manual/wiring reference, terminal/register/address/mask/polarity, physical before/after toggle, stale/recovery and meter CT/PT/type/word-order/scale/sign. kW sign cannot manufacture source authority.

## B-004 — Production inverter profiles

**Lane:** L6 / #82  
**State:** EXACT OFFICIAL MODEL/FIRMWARE/MANUAL + BENCH + SIGNED APPROVAL PENDING  
**Automation:** PR #158

Current catalogue entries `huawei.sun2000.pending`, `goodwe.commercial.pending`, `solis.commercial.pending` and `foxess.commercial.pending` are intentionally fail-closed. Each deployed model requires exact official manual applicability, physical identity/telemetry/status, controlled write/readback/failure/rollback/safe-zero and signed production approval. Wrong-family or guessed maps remain forbidden.

## B-005 — Secure OTA physical qualification

**Lane:** L4 / #86  
**State:** REAL CONTROLLER MATRIX PENDING ON EXACT INTENDED FINAL RELEASE IDENTITY  
**Automation:** PR #152

Execute authenticated upload, invalid-image rejection before write, interrupted upload, power loss, partial-image non-selection, previous-slot boot, pending verification, mark-valid, deliberate rollback, fail-closed control and NVS persistence.

## B-006 — Integrated Grid/DG/Modbus endurance and signed SAT

**Lane:** L7 / #83  
**State:** BLOCKED BY PREREQUISITE PHYSICAL GATES  
**Automation:** PR #160

Final execution requires complete Grid, Generator and mixed-source FAT; all three Modbus modes; slow/dead/exception/reset/reconnect/gateway/Wi-Fi/multi-device endurance; resource trends; zero fatal/reset/resource-collapse counters; accepted UI and OTA physical references; and authorized signed SAT tied to exact release identity.

## B-007 — Rev-A H2 reproducibility + H4 prototype

**Lane:** L9 / #85/#162  
**State:** NEW CONTROLLED H2 REQUIRED / H4 PENDING  
**Historical provider artifact:** `9909976209` — evidence only, not fabrication authority  
**Reproducibility run:** `33884657384` — DRC FAIL, 20 violations / 0 unconnected

Before new H2, obtain authoritative component/fabricator evidence for any footprint/manufacturing exception and commit approved rules. Then rerun ERC/DRC/SI/STEP/provider packaging to a new exact H2 identity. H4 fabrication and electrical/comms/relay/enclosure/thermal/environmental qualification follow only from that new accepted package.

## B-008 — Historical Waveshare final acceptance (separate legacy lane)

**Lane:** L3 / #87/#27/#25/#26  
**Exact candidate:** `87841ecee727fe1d814d4186be8c8c26e4afafb4`  
**State:** SHORT PASS / >=4 H CONTINUOUS SOAK PENDING  
**Automation:** PR #159 then PR #150

Prior run reached ~2 h / 121 samples before bench power-path interruption; partial runs are not additive. After one genuine >=4 h / >=240-sample PASS, complete backend parity/recovery and persistence/ARM on the same old identity. This lane does not qualify PR #179.

## Final closure blocker

**#91 / #79:** after all required physical gates pass, bind exact release SHA/artifact/config/site maps/approved inverter profiles/UI/OTA/FAT/SAT evidence and require zero critical blockers before release closure.

## Resolved software/tooling blockers

Current release evidence authority is complete through PR #181. No new software patch should be created merely to avoid executing a physical gate. New code is justified only by an observed current defect, missing capability, failed evidence contract or governed integration defect.
