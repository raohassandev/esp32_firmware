# AISH-OS Blocker Ledger v8

Master program: #79. **Reconciliation parent:** `dev` `c333db0752be89bdcb9bfbda01e2f19a1f05709f` after PR #188. Live repository state overrides this ledger. Known release software/evidence-tooling gaps are closed; remaining blockers are genuine physical/site/manufacturer/fabricator execution, signed acceptance, or governed post-PASS promotion.

## B-001 — Industrial UI exact-image Waveshare physical acceptance

**Lane:** L16 / #164/#174  
**Exact candidate:** PR #179 head `72a1a82a8fc5ad4406b5bd51fba1f80f9c182884`; tree `3069c65b4234fcd2b6418f9bbbe7859f1cd9abce`  
**Artifact:** `10293685030`; digest `sha256:44dc05fe2c6e61d3a8b5fdfc7c936937da948691a2038358d5c0b3c1008de541`  
**State:** SOFTWARE COMPLETE / PHYSICAL PANEL EXECUTION PENDING

Physically prove native 800x480 layout/touch/roles; Network scan/select/manual/connect/restart; Grid and Gen1..3 mappings; explicit Transfer/ATS and Sync applicability; alarm filters/sorts and role-gated ACK; real board↔bench-simulator Modbus counter/value activity; browser/API/resource health; and one uninterrupted >=4 h / >=240-sample run. PR #179 remains Draft/frozen until genuine PASS.

## B-002 — Generator transition physical bench

**Lane:** L2 / #80  
**Runtime candidate:** Draft PR #106 head `a1620789235d21b515f9f245f2329fab88b50558`  
**State:** SOFTWARE GREEN / PHYSICAL BENCH PENDING

Execute Grid<->Transfer<->Generator, island/supported-sync-or-authoritative-not-supported, stale/conflict/source-loss, source-contact evidence, meter sign/scaling and recovery dwell. Then replay equivalent validated runtime behavior to then-current `dev` and re-earn exact-head CI before merge.

## B-003 — Real site source commissioning

**Lane:** L5 / #81  
**State:** PHYSICAL SITE INPUT/EXECUTION PENDING

Need exact breaker/run/ATS/sync provenance, manual/wiring reference, terminal/register/address/mask/polarity, physical before/after toggle, stale/recovery and meter CT/PT/type/word-order/scale/sign. kW sign cannot manufacture source authority.

## B-004 — Production inverter profiles

**Lane:** L6 / #82  
**State:** EXACT OFFICIAL MODEL/FIRMWARE/MANUAL + BENCH + SIGNED APPROVAL PENDING

Pending profiles remain fail-closed. GoodWe GW100K-HT still needs the exact official applicable HT control protocol; Huawei SUN2000-115KTL-M2 still needs exact official applicable ME definitions; Solis exact S6 family is known but available compiled mapping is non-authoritative for writes; Growatt/Knox/FoxESS exact installed identity/protocol remains unresolved. Every deployed model requires physical identity/telemetry/status plus controlled write/readback/failure/rollback/safe-zero and signed production approval.

## B-005 — Secure OTA physical qualification

**Lane:** L4 / #86  
**State:** REAL CONTROLLER MATRIX PENDING ON EXACT INTENDED FINAL RELEASE IDENTITY

Execute authenticated upload, invalid-image rejection, interrupted upload, power loss, partial-image non-selection, previous-slot boot, pending verification, mark-valid, deliberate rollback, fail-closed control and NVS persistence.

## B-006 — Integrated Grid/DG/Modbus endurance and signed SAT

**Lane:** L7 / #83  
**State:** BLOCKED BY PREREQUISITE PHYSICAL GATES

Final execution requires complete Grid, Generator and mixed-source FAT; all three Modbus modes; slow/dead/exception/reset/reconnect/gateway/Wi-Fi/multi-device endurance; resource trends; zero fatal/reset/resource-collapse counters; accepted UI and OTA physical references; and authorized signed SAT tied to exact release identity.

## B-007 — Rev-A intended-fabricator DFM + H4 prototype

**Lane:** L9 / #178/#85/#162  
**State:** H2 CAD/ROUTING REQUALIFIED / INTENDED-FABRICATOR DFM + FABRICATION + H4 PENDING  
**Clean H2:** `a877e5d844af114a6e4386f6294f514288ca5df6`; tree `782189312aec046338d472858d65d8cb397bd473`  
**Authoritative run:** `34702074827`  
**Engineering artifact:** `10300950516`, digest `sha256:771ae830fa908b444d0fbce74da6fc35596bee4debaf7004447436df9eba0501`  
**RFQ/DFM candidate:** `10300571374`, digest `sha256:27b1709537715f08e928e65137262553791a95f957c19703da8dc3a104db0d30`  
**Provider ZIP:** `Automatrix_PVDG_RevA_PROVIDER_RFQ_a877e5d844.zip`

The current H2 is reproducible and clean: ERC=0, DRC=0, unconnected=0, SI/STEP/mechanical/manufacturing PASS. It proves internal CAD/routing consistency at the committed prototype minima (0.20 mm drill / 0.18 mm hole clearance / 0.25 mm copper-edge), not an intended fabricator's acceptance. Obtain written DFM/capability or exact DFM changes before fabrication authority advances. PR #187 H4 tooling is merged, but only a controlled fabricated board/lot can satisfy #162.

## B-008 — Final release evidence population/signoff

**Lane:** L11 / #91  
**State:** TRACEABILITY TOOLING COMPLETE / FINAL GENUINE INPUTS PENDING  
**Tooling:** PR #188 / `tools/release_traceability_verify.py`  
**Starter:** `evidence/candidates/final_release_traceability_observations.json` — deliberately unexecuted

After B-001 through B-006 pass and all required current-base promotions are complete, bind exact final SHA/tree/artifact/application/config/site-map/profile-manifest identities, approved inverter profiles, UI evidence/replay, exact generator/site/OTA/FAT-SAT records, signed SAT and zero critical blockers. The validator cannot create any missing physical or signed evidence.

## RETIRED — Historical Waveshare acceptance graph

**Former lane:** L3 / #87/#24/#25/#26/#27; PRs #20/#57/#67  
**Exact historical candidate:** `87841ecee727fe1d814d4186be8c8c26e4afafb4`  
**Disposition:** SUPERSEDED / CLOSED NOT_PLANNED / PRs CLOSED UNMERGED

The old identity keeps its short physical PASS and interrupted ~2 h / 121-sample record. The uninterrupted >=4 h gate, backend parity and persistence/ARM matrices were never completed. No old-image evidence transfers to the current release.

## Resolved software/tooling blockers

Current known release software and physical-evidence automation are complete. PR #184 retired the obsolete Waveshare graph; the Rev-A H2 engineering defect was repaired/requalified by PR #185 and run #224; PR #187 added fail-closed H4 evidence tooling; PR #188 added fail-closed final release traceability tooling. Future code is justified only by an observed defect, missing capability, failed evidence contract or governed integration defect—not as a substitute for real physical execution.
