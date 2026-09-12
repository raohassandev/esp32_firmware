# AISH-OS Blocker Ledger v7

Master program: #79. **Reconciliation parent:** `dev` `f924726db9c24ab55ebe4f43fe28caf75a9b2c3d` after PR #183. Live repository state overrides this ledger. Known release software gaps are closed; remaining blockers are genuine physical/site/manufacturer/fabricator execution or governed post-PASS promotion.

## B-001 — Industrial UI exact-image Waveshare physical acceptance

**Lane:** L16 / #164/#174  
**Exact candidate:** PR #179 head `72a1a82a8fc5ad4406b5bd51fba1f80f9c182884`; tree `3069c65b4234fcd2b6418f9bbbe7859f1cd9abce`  
**Artifact:** `10293685030`; digest `sha256:44dc05fe2c6e61d3a8b5fdfc7c936937da948691a2038358d5c0b3c1008de541`  
**Application SHA256:** `0bbdb75be4ea7c0337f07e83dbdd3e34736ce8f667a42aa11abeb5c638f60734`  
**State:** SOFTWARE COMPLETE / PHYSICAL PANEL EXECUTION PENDING  
**Evidence lineage:** PR #181 baseline + PR #183 topology-correct v3; identity-bound starter `evidence/candidates/industrial_ui_72a1a82_physical_observations.json`

Physically prove native 800x480 layout/touch/roles; Network scan/select/manual/connect/restart; Grid and Gen1..3 source mappings; alarm filters/sorts and Engineering-only acknowledgement; real board↔bench-simulator Modbus counter/value activity; browser/API/resource health; and one uninterrupted >=4 h / >=240-sample run. Transfer/ATS and Sync require explicit applicability: configured => genuine round-trip PASS; absent/unsupported => roundtrip false plus non-empty factual reason. Missing applicability or invented optional PASS is failure. PR #179 remains Draft/frozen until genuine PASS.

## B-002 — Generator transition physical bench

**Lane:** L2 / #80  
**Runtime candidate:** Draft PR #106 head `a1620789235d21b515f9f245f2329fab88b50558`  
**State:** SOFTWARE GREEN / PHYSICAL BENCH PENDING  
**Automation:** PR #151

Physical Grid<->Transfer<->Generator, island/supported-sync-or-authoritative-not-supported, stale/conflict/source-loss, source-contact evidence, meter sign/scaling and recovery dwell must pass. Then replay equivalent runtime behavior to then-current `dev` and re-earn exact-head CI before merge.

## B-003 — Real site source commissioning

**Lane:** L5 / #81  
**State:** PHYSICAL SITE INPUT/EXECUTION PENDING  
**Automation:** PR #156

Need exact breaker/run/ATS/sync provenance, manual/wiring reference, terminal/register/address/mask/polarity, physical before/after toggle, stale/recovery and meter CT/PT/type/word-order/scale/sign. kW sign cannot manufacture source authority.

## B-004 — Production inverter profiles

**Lane:** L6 / #82  
**State:** EXACT OFFICIAL MODEL/FIRMWARE/MANUAL + BENCH + SIGNED APPROVAL PENDING  
**Automation:** PR #158

Pending profiles remain fail-closed. Current provenance audit narrows known gaps:
- GoodWe GW100K-HT: exact official HT-series production control protocol still required;
- Huawei SUN2000-115KTL-M2: exact official applicable ME-family Modbus definitions still required;
- Solis `S6-EH3P(80-125)K10-NV-YD-H`: exact family known, but available compiled register reference is explicitly non-authoritative for production writes;
- Growatt/Knox/FoxESS: exact installed manufacturer/model/protocol identity not yet established.

Every deployed model still requires physical identity/telemetry/status plus controlled write/readback/failure/rollback/safe-zero and signed production approval. Wrong-family or guessed maps remain forbidden.

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

**Lane:** L9 / #178/#85/#162  
**State:** NEW CONTROLLED H2 REQUIRED / H4 PENDING  
**Historical provider artifact:** `9909976209` — evidence only, not fabrication authority  
**Reproducibility run:** `33884657384` — DRC FAIL, 20 violations / 0 unconnected

Exact affected identities are U1 Espressif ESP32-S3-WROOM-1-N8, J2 GCT USB4105-GF-A-120 and J3 CETUS J1B1211CCD. Manufacturer drawings are available, but intended-fabricator capability/DFM evidence and exact dimensional justification remain mandatory before any narrowly scoped exception. Then rerun ERC/DRC/unconnected/SI/STEP/provider packaging to a new exact H2 identity. H4 fabrication and electrical/comms/relay/enclosure/thermal/environmental qualification follow only from that new accepted package.

## RETIRED — Historical Waveshare acceptance graph

**Former lane:** L3 / #87/#24/#25/#26/#27; PRs #20/#57/#67  
**Exact historical candidate:** `87841ecee727fe1d814d4186be8c8c26e4afafb4`  
**Disposition:** SUPERSEDED / CLOSED NOT_PLANNED / PRs CLOSED UNMERGED  

The old identity keeps its short physical PASS and interrupted ~2 h / 121-sample record. The uninterrupted >=4 h gate, backend parity and persistence/ARM matrices were never completed. These are not open release blockers because the active release path is PR #179/#174, and no old-image evidence transfers to it.

## Final closure blocker

**#91 / #79:** after all required current release physical gates pass, bind exact release SHA/tree/artifact/config/site maps/approved inverter profiles/UI/OTA/FAT/SAT evidence and require zero critical blockers before release closure.

## Resolved software/tooling blockers

Current release software and physical-evidence automation are complete for known scope. PR #183 corrected the #174 optional-topology evidence contract and governance known-parent semantics without changing frozen firmware. Historical Waveshare duplicate-release graph has now been retired rather than left open as a misleading second completion path. Future code is justified only by an observed defect, missing capability, failed evidence contract or governed integration defect—not as a substitute for real physical execution.
