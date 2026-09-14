# AISH-OS Blocker Ledger v9

Master program: #79. **Reconciliation parent:** `dev` `3129f7a17550ae5262e4d51c381dda12dedacc16` after PR #195 and PR #196. Live `dev` overrides this ledger. Known release-side software and evidence-tooling defects found through the current audit are closed; the remaining blockers require genuine physical/site/manufacturer/fabricator execution, signed acceptance, or governed post-PASS promotion.

## B-001 — Industrial UI exact-image Waveshare physical acceptance

**Lane:** L16 / #174  
**Candidate:** PR #179 `72a1a82a8fc5ad4406b5bd51fba1f80f9c182884`, artifact `10293685030`  
**State:** SOFTWARE COMPLETE / PHYSICAL PANEL EXECUTION PENDING

Required: native 800x480 visual/touch/roles, Network commissioning, Grid+Gen1..3 mappings, topology-correct Transfer/ATS and Sync applicability, alarm filter/sort/Engineering ACK, real board↔bench Modbus counter/value proof, runtime resource health and one uninterrupted >=4 h / >=240-sample run.

## B-002 — Generator transition physical bench

**Lane:** L2 / #80  
**Runtime candidate:** Draft PR #106 `a1620789235d21b515f9f245f2329fab88b50558`  
**Evidence authority:** PR #151 + merged PR #195  
**State:** SOFTWARE/EVIDENCE TOOLING GREEN / PHYSICAL BENCH PENDING

PR #195 now externally locks exact firmware/artifact/site/config/topology/source-map/meter-map identity, scenario/raw evidence digests, independent meter scaling/sign proof and recovery chronology. It does not create a physical PASS. After genuine #80 PASS, replay the validated runtime slice onto then-current `dev`, prove equivalence, rerun exact-head CI and merge zero-behind.

## B-003 — Real site source commissioning

**Lane:** L5 / #81  
**Evidence authority:** PR #156 + merged PR #194  
**State:** PHYSICAL SITE INPUT/EXECUTION PENDING

Need authoritative wiring/manual/SLD/channel map, exact terminal/register/mask/polarity, physical before/after toggles, stale/recovery evidence and meter CT/PT/type/word-order/scale/sign proof on the exact site/config identity.

## B-004 — Production inverter profiles

**Lane:** L6 / #82  
**Evidence authority:** PR #158 + merged PR #193  
**State:** EXACT OFFICIAL MODEL/FIRMWARE/MANUAL + BENCH + SIGNED APPROVAL PENDING

Unknown/unqualified profiles remain fail-closed. Every deployed model needs exact official applicability, physical identity/telemetry/status, controlled write/readback/failure/rollback/safe-zero and signed production approval.

## B-005 — Secure OTA physical qualification

**Lane:** L4 / #86  
**State:** REAL CONTROLLER MATRIX PENDING ON EXACT INTENDED FINAL RELEASE IDENTITY

Execute authenticated upload, invalid rejection, interruption, power loss, partial-image non-selection, previous-slot boot, rollback-pending verification, mark-valid, deliberate rollback, fail-closed authority and NVS persistence.

## B-006 — Integrated Grid/DG/Modbus endurance and signed SAT

**Lane:** L7 / #83  
**Evidence authority:** PR #160 + merged PR #192  
**State:** BLOCKED BY PREREQUISITE PHYSICAL GATES

Run complete Grid/DG/mixed-source FAT, all three Modbus modes, degraded-peer/network endurance and resource trends, then obtain authorized signed SAT bound to the exact final release identity.

## B-007 — Rev-A intended-fabricator DFM + H4 prototype

**Lane:** L9 / #178/#85/#162  
**State:** H2 REQUALIFIED / INTENDED-FABRICATOR DFM + FABRICATION + H4 PENDING  
**H2 freeze:** `a877e5d844af114a6e4386f6294f514288ca5df6`  
**Engineering artifact:** `10300950516`  
**RFQ/DFM artifact:** `10300571374`

Internal CAD/routing is clean. Obtain intended-fabricator written DFM/capability before fabrication. PR #187 plus merged PR #191 provide H4 fail-closed evidence tooling and exact binary/chronology locks; only a controlled fabricated lot can satisfy #162.

## B-008 — Final release evidence population/signoff

**Lane:** L11 / #91  
**Evidence authority:** PR #188 + #190 + #196  
**State:** TRACEABILITY TOOLING COMPLETE / FINAL GENUINE INPUTS PENDING

Final traceability now externally locks source/tree/artifact/application/config/site-map/profile identities, site/config identity, all six mandatory lane evidence digests and signed SAT digest. Populate only after B-001 through B-006 genuinely pass, required post-PASS promotions are complete, and zero critical blockers remain.

## RETIRED — Historical Waveshare acceptance graph

**Former lane:** L3 / #87/#24/#25/#26/#27  
**Exact historical candidate:** `87841ecee727fe1d814d4186be8c8c26e4afafb4`  
**Disposition:** CLOSED NOT_PLANNED / PRs CLOSED UNMERGED

The old identity retains its own short physical PASS and interrupted ~2 h / 121-sample record. The uninterrupted >=4 h gate, backend parity and persistence/ARM matrices were never completed. No evidence transfers to current PR #179.

## Resolved software/tooling blockers

Core runtime and known release evidence automation are complete for the current scope. PRs #190–#196 closed the latest identity/chronology fail-open findings for final traceability, Rev-A H4, integrated FAT/SAT, inverter qualification, site commissioning and generator transition. Future code work should be driven by an observed defect or failed evidence contract—not used as a substitute for the real physical work above.
