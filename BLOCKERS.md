# AISH-OS Blocker Ledger v10

Master program: #79. **Reconciliation parent:** `dev` `a86d801cc952ce9ab6032821d41b531d2df343ef` after PR #199 and PR #198. Live `dev` overrides this ledger. Known release-side software and evidence-tooling defects found through the current audit are closed; the remaining blockers require genuine physical/site/manufacturer/fabricator execution, signed acceptance, or governed post-PASS promotion.

## B-001 — Industrial UI exact-image Waveshare physical acceptance

**Lane:** L16 / #174  
**Candidate:** PR #179 `72a1a82a8fc5ad4406b5bd51fba1f80f9c182884`, artifact `10293685030`  
**State:** SOFTWARE COMPLETE / PHYSICAL PANEL EXECUTION PENDING

Required: native 800x480 visual/touch/roles, Network commissioning, Grid+Gen1..3 mappings, topology-correct Transfer/ATS and Sync applicability, alarm filter/sort/Engineering ACK, real board↔bench Modbus counter/value proof, runtime resource health and one uninterrupted >=4 h / >=240-sample run.

## B-002 — Generator transition physical bench

**Lane:** L2 / #80  
**Runtime candidate:** Draft PR #106 `a1620789235d21b515f9f245f2329fab88b50558`  
**Evidence authority:** PR #151 + PR #195  
**State:** SOFTWARE/EVIDENCE TOOLING GREEN / PHYSICAL BENCH PENDING

PR #195 locks exact firmware/artifact/site/config/topology/source-map/meter-map identity, scenario/raw evidence digests, independent meter scaling/sign proof and recovery chronology. It does not create a physical PASS. After genuine #80 PASS, replay the validated runtime slice onto then-current `dev`, prove equivalence, rerun exact-head CI and merge zero-behind.

## B-003 — Real site source commissioning

**Lane:** L5 / #81  
**Evidence authority:** PR #156 + PR #194  
**State:** PHYSICAL SITE INPUT/EXECUTION PENDING

Need authoritative wiring/manual/SLD/channel map, exact terminal/register/mask/polarity, physical before/after toggles, stale/recovery evidence and meter CT/PT/type/word-order/scale/sign proof on the exact site/config identity.

## B-004 — Production inverter profiles

**Lane:** L6 / #82  
**Evidence authority:** PR #158 + PR #193  
**Software support:** PR #198 + PR #199  
**State:** GENERIC SOFTWARE + SOURCE INVENTORY COMPLETE / EXACT DEPLOYMENT MANUAL + BENCH + SIGNED APPROVAL PENDING

PR #198 inventories immutable source SHAs for the current SolTrix inverter-manual tree and records the manufacturer-source discovery boundary. PR #199 adds fail-closed atomic backup/restore for all 12 compiled profile assignments, exact profile-definition fingerprints, runtime/persistent control disable and restart enforcement. Neither imports register authority, qualification or production approval.

Every deployed model still needs exact installed manufacturer/model/firmware/connection identity, an accepted official applicable manual revision/digest, documented telemetry/status/command/readback definitions, physical identity/read-only proof, controlled write/readback/failure/rollback/safe-zero evidence and signed production approval. Unknown/unqualified profiles remain fail-closed.

## B-005 — Secure OTA physical qualification

**Lane:** L4 / #86  
**State:** REAL CONTROLLER MATRIX PENDING ON EXACT INTENDED FINAL RELEASE IDENTITY

Execute authenticated upload, invalid rejection, interruption, power loss, partial-image non-selection, previous-slot boot, rollback-pending verification, mark-valid, deliberate rollback, fail-closed authority and NVS persistence.

## B-006 — Integrated Grid/DG/Modbus endurance and signed SAT

**Lane:** L7 / #83  
**Evidence authority:** PR #160 + PR #192  
**State:** BLOCKED BY PREREQUISITE PHYSICAL GATES

Run complete Grid/DG/mixed-source FAT, all three Modbus modes, degraded-peer/network endurance and resource trends, then obtain authorized signed SAT bound to the exact final release identity.

## B-007 — Rev-A intended-fabricator DFM + H4 prototype

**Lane:** L9 / #178/#85/#162  
**State:** H2 REQUALIFIED / INTENDED-FABRICATOR DFM + FABRICATION + H4 PENDING  
**H2 freeze:** `a877e5d844af114a6e4386f6294f514288ca5df6`  
**Engineering artifact:** `10300950516`  
**RFQ/DFM artifact:** `10300571374`

Internal CAD/routing is clean. Obtain intended-fabricator written DFM/capability before fabrication. PR #187 + PR #191 provide H4 fail-closed evidence tooling and exact binary/chronology locks; only a controlled fabricated lot can satisfy #162.

## B-008 — Final release evidence population/signoff

**Lane:** L11 / #91  
**Evidence authority:** PR #188 + #190 + #196  
**State:** TRACEABILITY TOOLING COMPLETE / FINAL GENUINE INPUTS PENDING

Final traceability locks source/tree/artifact/application/config/site-map/profile identities, site/config identity, all six mandatory lane evidence digests and signed SAT digest. Populate only after B-001 through B-006 genuinely pass, required post-PASS promotions are complete, and zero critical blockers remain.

## RETIRED — Historical Waveshare acceptance graph

**Former lane:** L3 / #87/#24/#25/#26/#27  
**Exact historical candidate:** `87841ecee727fe1d814d4186be8c8c26e4afafb4`  
**Disposition:** CLOSED NOT_PLANNED / PRs CLOSED UNMERGED

The old identity retains its own short physical PASS and interrupted ~2 h / 121-sample record. The uninterrupted >=4 h gate, backend parity and persistence/ARM matrices were never completed. No evidence transfers to current PR #179.

## Resolved software/tooling blockers

Core runtime and known release evidence automation are complete for the current scope. PRs #190–#196 closed the latest identity/chronology fail-open findings. PR #198 closed inverter manual-source discovery/inventory. PR #199 closed generic compiled-profile assignment backup/restore and hardened profile-change runtime disable. Future code work should be driven by an observed defect, exact accepted manufacturer manual requirement, or failed evidence contract—not used as a substitute for the real physical work above.
