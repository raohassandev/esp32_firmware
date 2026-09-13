# AISH-OS v8 Program Board

Authoritative master: #79. **Reconciliation parent:** `dev` `26e9d103462ead5dbc09ff47c30aff3346f7d5a8` after PR #196. Live repository and genuine physical evidence override this board. This board records the known parent before its own revision; live `dev` must be re-fetched before decisions.

## Executive board

| Lane | Scope | State | Execution owner / next gate |
|---|---|---|---|
| L0/L8 | Program management/governance | CONTINUOUS / LIVE RECONCILIATION | ChatGPT / #84 |
| L1 | Modbus modes/deadlines | SOFTWARE COMPLETE | #83 physical endurance |
| L2 | Generator source transition | SOFTWARE GREEN / BENCH PENDING | #80; Draft #106; PR #151 |
| L3 | Historical Waveshare release | RETIRED / SUPERSEDED EVIDENCE ONLY | closed historical graph |
| L4 | Secure OTA | SOFTWARE COMPLETE / PHYSICAL PENDING | #86; PR #152 |
| L5 | Real site source commissioning | FULL EXTERNAL SITE IDENTITY TOOLING COMPLETE / SITE EXECUTION PENDING | #81; PR #156/#194 |
| L6 | Production inverter profiles | FULL EXTERNAL DEVICE/MANUAL IDENTITY TOOLING COMPLETE / MODEL QUALIFICATION PENDING | #82; PR #158/#193 |
| L7 | Integrated FAT/endurance/SAT | FULL FINAL-RELEASE IDENTITY TOOLING COMPLETE / PHYSICAL RELEASE GATE PENDING | #83; PR #160/#192 |
| L9 | Rev-A custom hardware | H2 CAD/ROUTING REQUALIFIED / FABRICATOR DFM + H4 PENDING | #178/#85/#162; PR #185/#187 |
| L10 | Browser final audit | COMPLETE/CLOSED | #90 |
| L11 | Evidence traceability | FULL EXTERNAL RELEASE/LANE IDENTITY TOOLING COMPLETE / FINAL GENUINE EVIDENCE + SIGNOFF PENDING | #91; PR #188/#196 |
| L12 | Requirements closure audit | COMPLETE/CLOSED | #92 |
| L13 | Promotion graph hygiene | CONTINUOUS | #93 |
| L14 | Live orchestration cadence | CONTINUOUS | #94 |
| L16 | Industrial UI v1 | EXACT SOFTWARE IMAGE FROZEN / PHYSICAL EXECUTION PENDING | #164/#174; PR #179 |

## Current integration/tooling state

Since the prior governance snapshot:
- PR #192 locked integrated FAT/SAT evidence to the exact final release identity.
- PR #193 locked inverter qualification to exact external manufacturer/model/fw/manual/profile/controller/endpoint identity and measured physical evidence.
- PR #194 locked site commissioning to exact external firmware/artifact/site/config/SLD/channel-map identity and independent physical/meter/recovery checks.
- PR #196 locked final traceability to full external release identity, all six required lane evidence digests and the signed SAT digest.

All were merged only after exact-head CI and zero-behind checks. None created a physical PASS.

## Current Industrial UI Waveshare candidate

PR #179 is the sole current Waveshare release candidate and remains intentionally Draft/frozen for physical #174:

- source `72a1a82a8fc5ad4406b5bd51fba1f80f9c182884`
- tree `3069c65b4234fcd2b6418f9bbbe7859f1cd9abce`
- artifact `10293685030`, digest `sha256:44dc05fe2c6e61d3a8b5fdfc7c936937da948691a2038358d5c0b3c1008de541`
- application `0bbdb75be4ea7c0337f07e83dbdd3e34736ce8f667a42aa11abeb5c638f60734`
- UF2 `199feec247563130f800d25e2a7024ebbaf64a65d3d8b6c6c5c9ec4c242b2500`
- build `espressif/idf:v6.0.1`; rollback enabled; no compiled STA credentials; no Engineering prefill; both bench auth bypasses disabled.

PR #181/#183 remain the #174 physical evidence authority, including explicit topology-dependent Transfer/ATS and Sync applicability. No physical PASS exists yet for this candidate.

## Physical release dependency graph

1. **Industrial UI #174:** real Waveshare visual/touch/roles, Network/source/alarm/real-Modbus/browser/resource and uninterrupted >=4 h / >=240 sample acceptance.
2. **Generator #80:** exact #106 source-transition bench PASS, then governed current-base replay/equivalence and fresh CI.
3. **Site #81:** genuine exact-site wiring/manual/mapping/toggle/stale/recovery/meter evidence under PR #194 authority.
4. **Inverters #82:** exact official deployed model/fw/manual plus physical write/readback/rollback and signed approval under PR #193 authority.
5. **OTA #86:** exact intended final release real-controller interruption/power-loss/rollback matrix.
6. **Final #83:** integrated Grid/DG/mixed-source FAT, all Modbus modes/network/resource endurance and signed SAT under PR #192 authority.
7. **#91/#79:** populate PR #196 final traceability record with those exact accepted evidence packages, zero critical blockers and signed release identity.

## Rev-A product-hardware track

H2 remains a clean controlled engineering/DFM input, not fabrication approval:
- freeze `a877e5d844af114a6e4386f6294f514288ca5df6`, tree `782189312aec046338d472858d65d8cb397bd473`
- run `34702074827`
- engineering artifact `10300950516`
- RFQ/DFM candidate `10300571374`, digest `sha256:27b1709537715f08e928e65137262553791a95f957c19703da8dc3a104db0d30`
- prototype minima 0.20 mm drill / 0.18 mm hole clearance / 0.25 mm copper-edge.

#178/#85 still require intended-fabricator written DFM/capability. #162 still requires a controlled fabricated board and actual H4 execution. PR #187 only validates submitted H4 evidence.

## Final traceability gate

PR #196 is the current #91 authority. The final manifest externally binds source/tree/artifact/application/config/site-map/profile-manifest, site ID/config identity, each of the six required lane evidence digests and the signed SAT digest. Generator/site/inverter/OTA/FAT-SAT lanes must repeat the full exact final identity. Industrial UI may cross from its frozen candidate only via explicit governed replay with no behavior-affecting change and a full final identity binding.

## Retired historical Waveshare graph

Historical `87841ecee727fe1d814d4186be8c8c26e4afafb4` retains its own short physical PASS and interrupted ~2 h / 121-sample attempt. The old >=4 h, backend-parity and persistence/ARM gates were never completed. Issues #87/#24/#25/#26/#27 and PRs #20/#57/#67 are closed/superseded. Retirement never qualifies #174 or PR #179.

## Operating policy

- Fetch live `dev`, PR heads, issues, CI and physical evidence every orchestration cycle.
- Every software merge uses fresh live target, exact head, required exact-head CI, zero-behind and expected-head guard.
- Frozen physical candidates are not rebased merely because `dev` advances.
- No guessed protocol/register/polarity/timing/topology/hardware rule and no fabricated physical PASS.
- Project reaches 100% only after all required physical dependencies, promotions, FAT/endurance, signed SAT and final traceability close. Rev-A hardware closes separately unless explicitly coupled to the firmware release.
