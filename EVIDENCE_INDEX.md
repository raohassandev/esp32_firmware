# AISH-OS Evidence Index v10

**Reconciliation parent:** `dev` `a86d801cc952ce9ab6032821d41b531d2df343ef`. This records the known parent before this revision; live `dev` is authoritative and must be re-fetched before execution or promotion.

## Current software/evidence authority chain

| Area | Authority | What it proves | What it does NOT prove |
|---|---|---|---|
| Industrial UI software | PRs #165/#167/#169/#172/#173/#176 + frozen PR #179 | Software/build/package readiness | Physical #174 PASS |
| Industrial UI physical evidence contract | PR #181/#183 | Complete topology-correct fail-closed evidence schema | Hardware observation |
| Generator transition evidence | PR #151 + #195 | Exact physical identity, meter scaling/sign, chronology and fail-closed validator | #80 bench PASS |
| Site commissioning evidence | PR #156 + #194 | Exact site/config/SLD/channel identity and physical-toggle/stale/recovery validation | #81 site PASS |
| Inverter profile evidence | PR #158 + #193 | Exact manufacturer/model/firmware/manual/controller identity, measured write/readback/rollback and signed-approval contract | #82 manufacturer/bench approval |
| Inverter manual-source inventory | PR #198 | Immutable SolTrix manual/protocol source SHAs plus public manufacturer discovery boundary | Installed-model applicability, accepted register map or production write authority |
| Inverter assignment manifest | PR #199 | Exact compiled-profile assignment backup/restore, fingerprint match, atomic persistence, live+persistent control disable and restart enforcement | Dynamic register import, qualification transfer, production approval or physical proof |
| Secure OTA evidence | PR #152 | Fail-closed OTA physical evidence validator | #86 real-controller PASS |
| Integrated FAT/SAT evidence | PR #160 + #192 | Exact final-release identity and physical-scenario/signed-SAT evidence contract | #83 FAT/SAT PASS |
| Rev-A H4 evidence | PR #187 + #191 | Exact H2/provider/firmware binary/lot chronology and fail-closed H4 validator | Fabricated-board #162 PASS |
| Final release traceability | PR #188 + #190 + #196 | Full release identity, all mandatory lane evidence digests, signed SAT digest and signed release record consistency | Missing physical evidence |

## Current exact candidates / controlled identities

### Industrial UI / #174
- PR #179 source: `72a1a82a8fc5ad4406b5bd51fba1f80f9c182884`
- tree: `3069c65b4234fcd2b6418f9bbbe7859f1cd9abce`
- artifact: `10293685030`
- artifact digest: `sha256:44dc05fe2c6e61d3a8b5fdfc7c936937da948691a2038358d5c0b3c1008de541`
- application SHA256: `0bbdb75be4ea7c0337f07e83dbdd3e34736ce8f667a42aa11abeb5c638f60734`
- physical verdict: **PENDING**

### Generator transition / #80
- Draft PR #106 source: `a1620789235d21b515f9f245f2329fab88b50558`
- software verdict: GREEN / deliberately physical-gated
- evidence tooling: PR #151 + PR #195
- physical verdict: **PENDING**

### Inverter software support / #82
- manual inventory authority: PR #198
- SolTrix inverter-manual tree identity recorded in `docs/INVERTER_MANUAL_INVENTORY.md`
- assignment manifest authority: PR #199 merge `a86d801cc952ce9ab6032821d41b531d2df343ef`
- generic software verdict: **COMPLETE FOR KNOWN SCOPE**
- exact deployed manual applicability: **PENDING**
- physical read/write qualification: **PENDING**
- signed production approval: **PENDING**

### Rev-A H2 / H4
- H2 freeze: `a877e5d844af114a6e4386f6294f514288ca5df6`
- H2 tree: `782189312aec046338d472858d65d8cb397bd473`
- engineering evidence artifact: `10300950516`
- RFQ/DFM artifact: `10300571374`
- H2 verdict: reproducible CAD/routing **PASS**
- intended-fabricator DFM: **PENDING**
- fabricated prototype H4 / #162: **PENDING**

## Required final physical outputs still missing

1. #174 exact PR #179 Waveshare physical PASS including uninterrupted >=4 h / >=240 samples.
2. #80 generator transition physical PASS, then governed runtime replay/promotion to current `dev`.
3. #81 exact real-site source/meter commissioning PASS.
4. #82 exact official manual applicability plus signed production qualification for every deployed inverter model/profile.
5. #86 exact-release Secure OTA physical PASS.
6. #83 integrated Grid/DG/Modbus endurance PASS plus authorized signed SAT.
7. #91 executed final traceability record binding all exact identities/evidence digests with zero critical blockers.
8. Separate Rev-A track: intended-fabricator written DFM, controlled fabrication and #162 H4 PASS.

## Retired historical Waveshare evidence

Historical exact source: `87841ecee727fe1d814d4186be8c8c26e4afafb4`.

- short physical gate: PASS for that historical image only
- interrupted run: approximately 2 h / 121 samples, no final acceptance
- uninterrupted >=4 h / >=240 run: NOT COMPLETED
- backend parity: NOT COMPLETED
- persistence/ARM: NOT COMPLETED
- issues #24/#25/#26/#27/#87: CLOSED NOT_PLANNED as superseded
- PRs #20/#57/#67: CLOSED UNMERGED
- release dependency: false
- transfer to PR #179: prohibited

## Evidence rules

Evidence is valid only for its exact firmware/tree/artifact/application/config/site/profile/manufacturer identity as required by its lane. Partial physical intervals are not additive where continuity is required. A validator PASS means the supplied record satisfies the schema and acceptance contract; it is never permission to invent observations or infer a physical PASS. Manual-source inventory and profile assignment backups are engineering support evidence only and cannot establish installed-model applicability, physical qualification or production approval.
