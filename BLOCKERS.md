# AISH-OS Blocker Ledger v9

Master program: #79. **Reconciliation parent:** `dev` `26e9d103462ead5dbc09ff47c30aff3346f7d5a8` after PR #196. Live repository state overrides this ledger. Known release software/evidence-tooling gaps are closed; remaining blockers are genuine physical/site/manufacturer/fabricator execution, signed acceptance, or governed post-PASS promotion.

## B-001 — Industrial UI exact-image Waveshare physical acceptance

**Lane:** L16 / #164/#174  
**Exact candidate:** PR #179 head `72a1a82a8fc5ad4406b5bd51fba1f80f9c182884`; tree `3069c65b4234fcd2b6418f9bbbe7859f1cd9abce`  
**Artifact:** `10293685030`; digest `sha256:44dc05fe2c6e61d3a8b5fdfc7c936937da948691a2038358d5c0b3c1008de541`  
**State:** SOFTWARE COMPLETE / PHYSICAL PANEL EXECUTION PENDING

Need native 800x480 layout/touch/roles, Network workflow, Grid/Gen1..3 mappings, explicit Transfer/ATS and Sync applicability, alarm filters/sorts/role-gated ACK, real board↔simulator Modbus, browser/resource health and one uninterrupted >=4 h / >=240-sample run. PR #179 stays Draft/frozen until genuine PASS.

## B-002 — Generator transition physical bench

**Lane:** L2 / #80  
**Runtime candidate:** Draft PR #106 head `a1620789235d21b515f9f245f2329fab88b50558`  
**State:** SOFTWARE GREEN / PHYSICAL BENCH PENDING

Execute authoritative source transition, island/supported-sync-or-authoritative-not-supported, stale/conflict/source-loss, meter sign/scaling and recovery dwell. Then governed replay/equivalence to then-current `dev`, fresh exact-head CI, zero-behind merge.

## B-003 — Real site source commissioning

**Lane:** L5 / #81  
**Latest tooling:** PR #194, merged `d15e98c845da031442e16385c20a5c6ac53adf7c`  
**State:** FULL EXTERNAL SITE IDENTITY TOOLING COMPLETE / PHYSICAL SITE INPUT PENDING

The validator now externally locks firmware/artifact/site/config/approved-SLD/channel-map identity and independently checks actual physical/runtime state change, meter raw×scale, configured recovery dwell and persisted readback identity. Genuine wiring/manual/register/contact/mask/polarity/toggle/meter evidence is still required; kW sign cannot manufacture source authority.

## B-004 — Production inverter profiles

**Lane:** L6 / #82  
**Latest tooling:** PR #193, merged `d2e1045a9bca6e5ecfe1b7b6c2af32deb9dabb58`  
**State:** FULL EXTERNAL DEVICE/MANUAL/CONTROLLER IDENTITY TOOLING COMPLETE / MANUFACTURER + BENCH + SIGNED APPROVAL PENDING

Pending profiles remain fail-closed. GoodWe GW100K-HT still needs exact official applicable HT control protocol; Huawei SUN2000-115KTL-M2 exact official applicable definitions remain unresolved; Solis exact S6 family is known but available compiled mapping is non-authoritative for writes; Growatt/Knox/FoxESS exact installed identity/protocol remains unresolved. Every production profile needs exact observed identity, manufacturer-documented raw command range, measured write/readback/rollback and signed approval.

## B-005 — Secure OTA physical qualification

**Lane:** L4 / #86  
**State:** REAL CONTROLLER MATRIX PENDING ON EXACT INTENDED FINAL RELEASE IDENTITY

Execute authenticated upload, invalid-image rejection, interrupted upload, power loss, partial-image non-selection, previous-slot boot, pending verification, mark-valid, deliberate rollback, fail-closed control and NVS persistence.

## B-006 — Integrated Grid/DG/Modbus endurance and signed SAT

**Lane:** L7 / #83  
**Latest tooling:** PR #192, merged `82e389ac719120d3ac03909684c0e29a962fcf34`  
**State:** FULL FINAL-RELEASE IDENTITY TOOLING COMPLETE / BLOCKED BY PREREQUISITE PHYSICAL GATES

The evidence gate now externally locks exact final firmware/tree/artifact/application/config/site-map/profile-manifest identity and binds every scenario plus signed SAT to it. Real Grid/Generator/mixed-source FAT, all Modbus modes, degraded-peer/network/resource endurance, accepted UI/OTA physical references and authorized signed SAT remain unexecuted.

## B-007 — Rev-A intended-fabricator DFM + H4 prototype

**Lane:** L9 / #178/#85/#162  
**State:** H2 CAD/ROUTING REQUALIFIED / INTENDED-FABRICATOR DFM + FABRICATION + H4 PENDING  
**Clean H2:** `a877e5d844af114a6e4386f6294f514288ca5df6`; tree `782189312aec046338d472858d65d8cb397bd473`  
**Authoritative run:** `34702074827`  
**Engineering artifact:** `10300950516`, digest `sha256:771ae830fa908b444d0fbce74da6fc35596bee4debaf7004447436df9eba0501`  
**RFQ/DFM candidate:** `10300571374`, digest `sha256:27b1709537715f08e928e65137262553791a95f957c19703da8dc3a104db0d30`

Current H2 is reproducible and CAD-clean at committed 0.20 mm drill / 0.18 mm hole clearance / 0.25 mm copper-edge. It is not intended-fabricator acceptance. Obtain written DFM/capability or exact DFM changes, then fabricate and execute #162 H4. PR #187 tooling cannot create H4 PASS.

## B-008 — Final release evidence population/signoff

**Lane:** L11 / #91  
**Latest tooling:** PR #196, merged `26e9d103462ead5dbc09ff47c30aff3346f7d5a8`  
**State:** FULL EXTERNAL TRACEABILITY TOOLING COMPLETE / FINAL GENUINE INPUTS PENDING

The final validator externally locks source/tree/artifact/application/config/site-map/profile-manifest, site/config identity, all six required lane evidence digests and signed SAT digest; exact lanes repeat the full final release identity. After B-001 through B-006 genuinely pass and required promotions are complete, populate the deliberately unexecuted starter and require zero critical blockers. The validator cannot create missing physical evidence or signatures.

## RETIRED — Historical Waveshare acceptance graph

**Former lane:** L3 / #87/#24/#25/#26/#27; PRs #20/#57/#67  
**Exact historical candidate:** `87841ecee727fe1d814d4186be8c8c26e4afafb4`  
**Disposition:** SUPERSEDED / CLOSED NOT_PLANNED / PRs CLOSED UNMERGED

The old identity keeps its short physical PASS and interrupted ~2 h / 121-sample record. The uninterrupted >=4 h gate, backend parity and persistence/ARM matrices were never completed. No old-image evidence transfers to the current release.

## Resolved software/tooling blockers

Current known release software and evidence automation are complete for the known scope. PR #192 hardened final FAT/SAT identity, PR #193 hardened inverter identity/evidence, PR #194 hardened site commissioning identity/evidence, and PR #196 hardened final release traceability. These changes did not create any physical PASS. Future code is justified only by an observed defect, missing capability, failed evidence contract or governed integration defect—not as a substitute for real physical execution.
