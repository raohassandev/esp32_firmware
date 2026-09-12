# AISH-OS v7 Program Board

Authoritative master: #79. **Reconciliation parent:** `dev` `c333db0752be89bdcb9bfbda01e2f19a1f05709f` after PR #188. Live repository and genuine physical evidence override this board. This board intentionally records the known parent before its own revision rather than claiming an unknowable future merge SHA.

## Executive board

| Lane | Scope | State | Execution owner / next gate |
|---|---|---|---|
| L0/L8 | Program management/governance | CONTINUOUS / LIVE RECONCILIATION | ChatGPT / #84 |
| L1 | Modbus modes/deadlines | SOFTWARE COMPLETE | #83 physical endurance |
| L2 | Generator source transition | SOFTWARE GREEN / BENCH PENDING | #80; Draft #106; PR #151 |
| L3 | Historical Waveshare release | RETIRED / SUPERSEDED EVIDENCE ONLY | closed historical graph |
| L4 | Secure OTA | SOFTWARE COMPLETE / PHYSICAL PENDING | #86; PR #152 |
| L5 | Real site source commissioning | TOOLING COMPLETE / SITE EXECUTION PENDING | #81; PR #156 |
| L6 | Production inverter profiles | GENERIC CORE + TOOLING COMPLETE / MODEL QUALIFICATION PENDING | #82; PR #158 |
| L7 | Integrated FAT/endurance/SAT | TOOLING COMPLETE / PHYSICAL RELEASE GATE PENDING | #83; PR #160 |
| L9 | Rev-A custom hardware | H2 CAD/ROUTING REQUALIFIED / FABRICATOR DFM + H4 PENDING | #178/#85/#162; PR #185/#187 |
| L10 | Browser final audit | COMPLETE/CLOSED | #90 |
| L11 | Evidence traceability | TOOLING COMPLETE / FINAL GENUINE EVIDENCE + SIGNOFF PENDING | #91; PR #188 |
| L12 | Requirements closure audit | COMPLETE/CLOSED | #92 |
| L13 | Promotion graph hygiene | CONTINUOUS | #93 |
| L14 | Live orchestration cadence | CONTINUOUS | #94 |
| L16 | Industrial UI v1 | EXACT SOFTWARE IMAGE FROZEN / PHYSICAL EXECUTION PENDING | #164/#174; PR #179 |

## Current integration/tooling state

PR #187 merged fail-closed Rev-A H4 physical evidence tooling without claiming H4 PASS. PR #188 merged the fail-closed final release traceability manifest gate. The release tooling surface is therefore complete for the current known scope; the release itself is not complete because genuine physical/site/manufacturer/FAT/SAT evidence remains outstanding.

## Current Industrial UI Waveshare candidate

PR #179 is the sole current Waveshare release candidate and remains intentionally Draft/frozen for physical #174:

- source `72a1a82a8fc5ad4406b5bd51fba1f80f9c182884`
- tree `3069c65b4234fcd2b6418f9bbbe7859f1cd9abce`
- artifact `10293685030` / `industrial-ui-waveshare-800x480-candidate`
- artifact digest `sha256:44dc05fe2c6e61d3a8b5fdfc7c936937da948691a2038358d5c0b3c1008de541`
- application SHA256 `0bbdb75be4ea7c0337f07e83dbdd3e34736ce8f667a42aa11abeb5c638f60734`
- UF2 SHA256 `199feec247563130f800d25e2a7024ebbaf64a65d3d8b6c6c5c9ec4c242b2500`
- ESP-IDF container `espressif/idf:v6.0.1`
- rollback enabled; no compiled STA credentials; no Engineering prefill; both bench auth bypasses OFF; minimum native touch target 44 px.

PR #181 established the full #174 evidence surface. PR #183 corrected Transfer/ATS and synchronized Grid+Generator to explicit topology-dependent applicability. Configured channels require a real round-trip PASS; absent/unsupported channels require roundtrip=false plus a factual non-empty reason.

## Physical release dependency graph

1. **Industrial UI #174:** execute exact PR #179 on a real Waveshare 800x480 panel with Network/source/alarm/real-Modbus/browser/resource and uninterrupted >=4 h / >=240 sample evidence.
2. **Generator #80:** execute source-transition physical bench; after PASS replay the validated #106 runtime slice to current `dev` and re-earn exact-head CI.
3. **Site #81:** prove real breaker/run/ATS/sync and meter commissioning from authoritative wiring/manual evidence.
4. **Inverters #82:** qualify each deployed exact model/firmware/manual with physical read/write/readback/rollback evidence and signed approval.
5. **OTA #86:** execute real-controller interruption/power-loss/rollback on one exact intended final release image.
6. **Final #83:** execute integrated Grid/DG/mixed-source FAT, all Modbus modes/network endurance and signed SAT.
7. **#91/#79:** populate and pass the PR #188 final traceability manifest with exact accepted evidence and zero critical blockers.

## Rev-A product-hardware track

The Rev-A H2 engineering gate is now requalified:

- clean H2 freeze `a877e5d844af114a6e4386f6294f514288ca5df6`
- tree `782189312aec046338d472858d65d8cb397bd473`
- authoritative KiCad run `34702074827`
- engineering artifact `10300950516`, digest `sha256:771ae830fa908b444d0fbce74da6fc35596bee4debaf7004447436df9eba0501`
- controlled RFQ/DFM candidate `10300571374`, digest `sha256:27b1709537715f08e928e65137262553791a95f957c19703da8dc3a104db0d30`
- provider ZIP `Automatrix_PVDG_RevA_PROVIDER_RFQ_a877e5d844.zip`
- committed prototype minima: 0.20 mm drill / 0.18 mm hole clearance / 0.25 mm copper-edge.

This checkpoint has ERC=0, DRC=0, unconnected=0, SI/STEP/mechanical/manufacturing PASS. It is controlled CAD/RFQ evidence, **not** intended-fabricator DFM acceptance. #178/#85 remain open for intended-fabricator written capability/DFM. PR #187 makes #162 evidence machine-checkable once a controlled board exists; it does not create a physical H4 PASS.

## Final traceability gate

PR #188 provides `tools/release_traceability_verify.py` and deliberately unexecuted `evidence/candidates/final_release_traceability_observations.json`. A final release record must bind exact final source/tree/artifact/application/config/site-map/profile-manifest identities and genuine accepted UI/generator/site/inverter/OTA/FAT-SAT evidence, signed SAT and zero critical blockers. Industrial UI may cross from its frozen candidate only through an explicit governed replay with no behavior-affecting change. Rev-A H4 is separate unless explicitly coupled to the firmware release.

## Retired historical Waveshare graph

Historical source `87841ecee727fe1d814d4186be8c8c26e4afafb4` retains its own short physical PASS and interrupted ~2 h / 121-sample attempt. The old >=4 h, backend-parity and persistence/ARM gates were never completed. Issues #87/#24/#25/#26/#27 and PRs #20/#57/#67 are closed/superseded. This retirement removes a duplicate active path; it does not rewrite incomplete evidence into PASS.

## Operating policy

- Fetch live `dev`, PR heads, issues, CI and physical evidence every orchestration cycle.
- Keep independent work moving while hardware/site lanes wait, but do not create churn merely to appear active.
- Every software merge uses fresh live target, exact head, fresh required CI, zero-behind and expected-head guard.
- Frozen physical candidates are not rebased/churned merely because `dev` advances.
- No guessed protocol/register/polarity/timing/topology/hardware-rule evidence and no fabricated physical PASS.
- Project reaches 100% only after all required release physical dependencies, promotions, FAT/endurance, signed SAT and final traceability close. Rev-A hardware closes separately unless explicitly coupled to that firmware release.
