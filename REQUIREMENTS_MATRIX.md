# AISH-OS Requirements Closure Matrix v2

Master program: #79. Live source/evidence overrides stale text. Snapshot baseline: `dev` `1b4d7631862afdb38da99fbbae9aa170729b0bdb` after PR #160.

| ID | Requirement group | Current state | Tracking / done gate |
|---|---|---|---|
| R-GRID-01 | Zero export | SOFTWARE VERIFIED / PHYSICAL FAT PENDING | #83 / PR #160 evidence gate |
| R-GRID-02 | Limited export | SOFTWARE VERIFIED / PHYSICAL FAT PENDING | #83 |
| R-GRID-03 | Minimum grid import | SOFTWARE VERIFIED / PHYSICAL FAT PENDING | #83 |
| R-GRID-04 | Grid stale/loss/recovery | SOFTWARE VERIFIED / PHYSICAL FAT PENDING | #83 |
| R-GEN-01 | Generator roles/config/persistence | COMPLETE/MERGED | PR #63 |
| R-GEN-02 | Generator fleet aggregation | COMPLETE/MERGED | PR #64 |
| R-GEN-03 | minimum load/reserve/reverse margin | SOFTWARE MERGED / PHYSICAL FAT PENDING | #83 |
| R-GEN-04 | explicit run/breaker evidence | SOFTWARE MERGED / SITE MAPPING PENDING | #81 |
| R-SRC-01 | Grid/Generator/Transfer/Island/Conflict/Unknown state engine | SOFTWARE GREEN / BENCH GATED | Draft #106 / #80 |
| R-SRC-02 | fresh recovery dwell after source transition | SOFTWARE GREEN / BENCH GATED | #80 |
| R-SRC-03 | fail closed on transfer/conflict/stale/no-source | SOFTWARE GREEN / BENCH GATED | #80 |
| R-SRC-04 | physical transition evidence validator | COMPLETE/MERGED TOOLING | PR #151 |
| R-MOD-01 | bounded Modbus connection/deadline behavior | COMPLETE/MERGED | PR #99/#114 |
| R-MOD-02 | exception preservation/diagnostics | COMPLETE IN LIVE ENGINE | runtime contracts |
| R-MOD-03 | per-transaction/persistent/reconnect-on-error | COMPLETE/MERGED | PR #99 |
| R-MOD-04 | socket/lwIP/multi-device/degraded-peer endurance | PHYSICAL PENDING | #83 / PR #160 |
| R-CONFIG-01 | import/depth/bounds fail closed | COMPLETE/HARDENED | PR #122/#135 etc. |
| R-CONFIG-02 | safety mutation revokes command authority before persistence | COMPLETE/MERGED | PR #127/#129/#130/#131 |
| R-INV-01 | exact production model/manual/firmware mapping | EXTERNAL DOCUMENT + BENCH PENDING | #82 |
| R-INV-02 | command width/scale/range/FC semantics | COMPLETE/MERGED GENERIC CORE | PR #108 |
| R-INV-03 | write/readback/tolerance/rollback core | COMPLETE GENERIC CORE / MODEL BENCH PENDING | #82 |
| R-INV-04 | reconnect identity revalidation | COMPLETE/MERGED | PR #102 |
| R-INV-05 | profile assignment disables control before persistence | COMPLETE/MERGED | PR #117 |
| R-INV-06 | positive writes require complete fresh profile evidence | COMPLETE GENERIC AUTHORITY | PR #119 |
| R-INV-07 | release blocked with zero production-approved profiles | COMPLETE/MERGED | PR #112 |
| R-INV-08 | pending transports remain explicitly unqualified | COMPLETE/MERGED | PR #113 |
| R-INV-09 | exact model physical qualification evidence validator | COMPLETE/MERGED TOOLING / PHYSICAL PENDING | PR #158 / #82 |
| R-NET-01 | no compiled site credentials / config preservation | COMPLETE/MERGED | PR #100 |
| R-NET-02 | retry/recovery/AP/scan ownership | SOFTWARE VERIFIED / ENDURANCE PENDING | #83 |
| R-WEB-01 | browser lifecycle/bounded pollers | COMPLETE/CLOSED | #90 |
| R-AUTH-01 | production Engineering authentication | SOFTWARE VERIFIED | access contracts |
| R-HTTP-01 | bounded JSON/OTA HTTP handling | COMPLETE/MERGED REGRESSION | PR #107/#135/#145/#148 |
| R-SAFE-01 | alarm snapshot cannot expose transient all-clear | COMPLETE/MERGED | PR #124 |
| R-OPS-01 | operator/engineering separation and truthful Plant verdict | COMPLETE/MERGED | PR #144 |
| R-WAVE-01 | no recurring flicker/sweep/reload | SHORT PHYSICAL PASS | #87/#27 exact `87841ece...` |
| R-WAVE-02 | Alarms + touch responsiveness | SHORT PHYSICAL PASS | #87/#27 |
| R-WAVE-03 | >=20 kB realistic DMA headroom | SHORT PHYSICAL PASS | #87/#27 |
| R-WAVE-04 | uninterrupted >=4 h / >=240 sample same-image soak | PENDING | #87/#27 |
| R-WAVE-05 | backend parity/recovery | PENDING AFTER SOAK | #25 |
| R-WAVE-06 | persistence/ARM save/readback/reboot/failure | PENDING AFTER SOAK | #26 |
| R-WAVE-07 | deterministic final acceptance/package validation | COMPLETE/MERGED TOOLING | PR #142 |
| R-WAVE-08 | post-soak parity/persistence validation | COMPLETE/MERGED TOOLING | PR #150 |
| R-WAVE-09 | one-command fail-closed physical soak capture | COMPLETE/MERGED TOOLING | PR #159 -> `3fd831b...` |
| R-OTA-01 | rollback-safe secure OTA software | COMPLETE/MERGED | PR #145 |
| R-OTA-02 | real interruption/power-loss/rollback matrix | PHYSICAL PENDING | #86 |
| R-OTA-03 | OTA always-on PR/dev-push regression | COMPLETE/MERGED | PR #148 |
| R-OTA-04 | real-controller evidence validator | COMPLETE/MERGED TOOLING | PR #152 |
| R-SITE-01 | exact breaker/run/ATS/sync mapping/polarity | PHYSICAL SITE PENDING | #81 |
| R-SITE-02 | meter CT/PT/type/word-order/scale/sign proof | PHYSICAL SITE PENDING | #81 |
| R-SITE-03 | fail-closed site commissioning evidence validator | COMPLETE/MERGED TOOLING | PR #156 -> `1c6e1de...` |
| R-FAT-01 | Grid FAT | PHYSICAL PENDING | #83 |
| R-FAT-02 | Generator FAT | PHYSICAL PENDING | #83 |
| R-FAT-03 | mixed source/source transition FAT | PHYSICAL PENDING | #80/#83 |
| R-FAT-04 | Modbus/network degraded-peer endurance | PHYSICAL PENDING | #83 |
| R-FAT-05 | integrated exact-identity FAT/endurance/SAT validator | COMPLETE/MERGED TOOLING | PR #160 -> `1b4d763...` |
| R-SAT-01 | signed SAT tied to exact release identity | PENDING | #83/#91 |
| R-GOV-01 | live owner/lane/dependency/governance tracking | CONTINUOUS | #79/#84 |
| R-GOV-02 | exact-head/zero-behind/expected-head merge gate | ACTIVE | #93 |
| R-GOV-03 | exact evidence traceability/no cross-identity PASS | ACTIVE | #91 |
| R-HW-01 | Rev-A H1/H2/H3 controlled design/provider package | H2/H3 AUTOMATED PASS; PR #19 EXACT-HEAD CI ACTIVE | #85 / PR #19 / run `33797012638` |
| R-HW-02 | Rev-A fabricated prototype bring-up and validation | PHYSICAL PENDING | #85 H4 |

## Closure discipline

1. Software/tooling complete does not equal physical acceptance.
2. Frozen physical identities are not churned merely because `dev` advances.
3. No external mapping/manual gap may be filled by guesswork.
4. Every physical PASS binds exact source/artifact/config/profile/site identity.
5. PR #160 is the final integrated evidence contract, not a substitute for Grid/DG/endurance/SAT execution.
6. Rev-A H2/H3 CI/provider package is not H4 prototype acceptance.
7. 100% release requires every remaining physical gate, signed SAT and zero critical blockers.
