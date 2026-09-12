# AISH-OS Requirements Closure Matrix v7

Master program: #79. Live source/evidence overrides this matrix. **Reconciliation parent:** `dev` `c333db0752be89bdcb9bfbda01e2f19a1f05709f` (PR #188 merge). Live `dev` must be re-fetched before every action; this file records the known parent before its own future merge.

| ID | Requirement group | Current state | Tracking / done gate |
|---|---|---|---|
| R-GRID-01 | Zero/limited export + minimum import | SOFTWARE VERIFIED / PHYSICAL FAT PENDING | #83 |
| R-GRID-02 | Grid stale/loss/recovery | SOFTWARE VERIFIED / PHYSICAL FAT PENDING | #83 |
| R-GEN-01 | Generator roles/config/fleet aggregation | COMPLETE/MERGED | PR #63/#64 |
| R-GEN-02 | min load/reserve/reverse margin | SOFTWARE MERGED / PHYSICAL FAT PENDING | #83 |
| R-GEN-03 | explicit run/breaker evidence | SOFTWARE MERGED / SITE MAPPING PENDING | #81 |
| R-SRC-01 | Grid/Generator/Transfer/Island/Conflict state engine | SOFTWARE GREEN / BENCH GATED | Draft #106 / #80 |
| R-SRC-02 | fresh recovery dwell + fail closed on stale/conflict/no-source | SOFTWARE GREEN / BENCH GATED | #80 |
| R-MOD-01 | bounded Modbus modes/deadlines/exceptions | COMPLETE/MERGED | PR #99/#114 |
| R-MOD-02 | physical multi-device/degraded-peer endurance | PHYSICAL PENDING | #83 |
| R-CONFIG-01 | import/depth/bounds + mutation fail-closed | COMPLETE/HARDENED | PR #122/#127/#129/#130/#131/#135 |
| R-INV-01 | generic command/readback/rollback/write authority | COMPLETE/MERGED | PR #102/#108/#117/#119 |
| R-INV-02 | exact production manufacturer/model/firmware mapping | EXACT OFFICIAL DOCUMENT + BENCH PENDING | #82 / PR #158 |
| R-NET-01 | no compiled site credentials / config preservation | COMPLETE/MERGED | PR #100 |
| R-NET-02 | retry/recovery/AP/scan ownership | SOFTWARE VERIFIED / ENDURANCE PENDING | #83 |
| R-WEB-01 | browser lifecycle/socket/LRU/PSRAM resilience | COMPLETE/MERGED | #90 / PR #173 |
| R-AUTH-01 | production Engineering authentication and lockout | SOFTWARE VERIFIED | access contracts / PR #179 exact candidate |
| R-HTTP-01 | bounded JSON/OTA handling | COMPLETE/MERGED | PR #107/#135/#145/#148 |
| R-SAFE-01 | alarm snapshot and fail-closed control authority | COMPLETE/MERGED | PR #124 and runtime contracts |
| R-UI-01 | authoritative Industrial shell + task IA | COMPLETE/MERGED | PR #165/#167/#169/#172/#176 |
| R-UI-02 | exact Waveshare Industrial UI firmware candidate | SOFTWARE GREEN / FROZEN | PR #179 head `72a1a82a...` |
| R-UI-03 | exact candidate build/package identity | COMPLETE SOFTWARE EVIDENCE | tree `3069c65b...`; artifact `10293685030`; app `0bbdb75b...` |
| R-UI-04 | on-device Network workflow | SOFTWARE COMPLETE / PHYSICAL PENDING | PR #179 / #174 |
| R-UI-05 | Grid/Gen1..3 native source commissioning | SOFTWARE COMPLETE / PHYSICAL PENDING | PR #179 / #174 |
| R-UI-05A | Transfer/ATS optional source evidence | SOFTWARE COMPLETE / EXPLICIT PHYSICAL APPLICABILITY PENDING | #174 / PR #183 |
| R-UI-05B | synchronized Grid+Generator optional evidence | SOFTWARE COMPLETE / EXPLICIT PHYSICAL APPLICABILITY PENDING | #174 / PR #183 |
| R-UI-06 | native alarm filters/sorts + role-gated ACK | SOFTWARE COMPLETE / PHYSICAL PENDING | PR #179 / #174 |
| R-UI-07 | board↔bench-simulator Modbus real counter/value evidence | PHYSICAL PENDING | #174 / PR #181/#183 |
| R-UI-08 | complete #174 fail-closed evidence validator and identity-bound starter | TOOLING COMPLETE / MERGED | PR #181/#183 |
| R-UI-09 | native 800x480 visual/touch/role/browser acceptance | PHYSICAL PENDING | #164/#174 |
| R-UI-10 | uninterrupted >=4 h / >=240 sample exact-image endurance | PHYSICAL PENDING | #174 |
| R-WAVE-01 | historical `87841ece...` short display/touch acceptance | HISTORICAL SHORT PASS / RETIRED | exact identity only |
| R-WAVE-02 | historical uninterrupted >=4 h / >=240 sample soak | NOT COMPLETED / RETIRED SUPERSEDED | closed #87/#27 |
| R-WAVE-03 | historical backend parity + persistence/ARM | NOT COMPLETED / RETIRED SUPERSEDED | closed #25/#26 |
| R-OTA-01 | rollback-safe secure OTA software | COMPLETE/MERGED | PR #145/#148 |
| R-OTA-02 | real interruption/power-loss/rollback matrix | PHYSICAL PENDING | #86 / PR #152 |
| R-SITE-01 | exact breaker/run/ATS/sync mapping/polarity + meter scaling | PHYSICAL SITE PENDING | #81 / PR #156 |
| R-FAT-01 | Grid/Generator/mixed-source FAT | PHYSICAL PENDING | #83 |
| R-FAT-02 | Modbus/network degraded-peer endurance | PHYSICAL PENDING | #83 |
| R-SAT-01 | signed SAT tied to exact release identity | PENDING | #83/#91 |
| R-REL-01 | fail-closed final release evidence manifest | TOOLING COMPLETE / MERGED | PR #188 / #91 |
| R-REL-02 | exact final SHA/tree/artifact/config/site/profile/UI/OTA/FAT/SAT binding | PHYSICAL/SIGNOFF INPUTS PENDING | #91; `tools/release_traceability_verify.py` |
| R-GOV-01 | live governance + exact-head/zero-behind promotion | ACTIVE | #79/#84/#93 |
| R-GOV-02 | exact evidence traceability/no cross-identity PASS | TOOLING COMPLETE / FINAL EVIDENCE PENDING | #91 / PR #188 |
| R-GOV-03 | reconciliation documents use known-parent semantics and re-fetch live target | COMPLETE/MERGED | #84 / PR #183 |
| R-GOV-04 | superseded historical release graphs retired without rewriting evidence | COMPLETE/MERGED | PR #184 / #84/#93 |
| R-HW-01 | Rev-A reproducible controlled H2 CAD/routing checkpoint | COMPLETE / REQUALIFIED | H2 `a877e5d844af...`; run `34702074827` |
| R-HW-02 | controlled Rev-A RFQ/DFM package | COMPLETE AS DFM INPUT / NOT FABRICATION APPROVAL | artifact `10300571374`, digest `27b17095...` |
| R-HW-03 | intended-fabricator written DFM/capability acceptance | EXTERNAL PENDING | #178/#85 |
| R-HW-04 | fail-closed H4 physical evidence tooling | COMPLETE/MERGED | PR #187 / #162 |
| R-HW-05 | fabricated prototype bring-up/validation | PHYSICAL PENDING AFTER DFM/FABRICATION | #162 |

## Exact Industrial UI physical candidate

- Source: `72a1a82a8fc5ad4406b5bd51fba1f80f9c182884`
- Tree: `3069c65b4234fcd2b6418f9bbbe7859f1cd9abce`
- Artifact: `10293685030`
- Artifact digest: `sha256:44dc05fe2c6e61d3a8b5fdfc7c936937da948691a2038358d5c0b3c1008de541`
- Application SHA256: `0bbdb75be4ea7c0337f07e83dbdd3e34736ce8f667a42aa11abeb5c638f60734`
- UF2 SHA256: `199feec247563130f800d25e2a7024ebbaf64a65d3d8b6c6c5c9ec4c242b2500`
- Build: `espressif/idf:v6.0.1`; rollback enabled; both bench auth bypasses disabled; minimum native touch target 44 px.

## Rev-A H2 authority boundary

- Clean H2 freeze: `a877e5d844af114a6e4386f6294f514288ca5df6`
- Tree: `782189312aec046338d472858d65d8cb397bd473`
- Engineering artifact `10300950516`, digest `sha256:771ae830fa908b444d0fbce74da6fc35596bee4debaf7004447436df9eba0501`
- RFQ/DFM candidate `10300571374`, digest `sha256:27b1709537715f08e928e65137262553791a95f957c19703da8dc3a104db0d30`
- Declared prototype minima: 0.20 mm drill / 0.18 mm hole clearance / 0.25 mm copper-edge.

This is a reproducible CAD/routing checkpoint and controlled DFM input, not intended-fabricator approval or H4 physical PASS.

## Closure discipline

1. Software/tooling complete does not equal physical acceptance.
2. PR #179 stays frozen/Draft until genuine #174 PASS.
3. Historical `87841ece...` evidence never transfers to PR #179.
4. Optional Transfer/ATS or Sync applicability must be explicit and cannot be guessed.
5. Every physical PASS binds exact source/tree/artifact/config/profile/site identity.
6. Clean Rev-A H2 does not replace intended-fabricator DFM or H4.
7. PR #188's validator can verify a final evidence manifest but cannot create the missing #174/#80/#81/#82/#86/#83 evidence or signed SAT.
8. Project 100% requires all remaining physical/manufacturer/site gates, signed SAT, a passing final traceability manifest and zero critical blockers.
