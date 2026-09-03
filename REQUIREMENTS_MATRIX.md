# AISH-OS Requirements Closure Matrix v1

Master program: #79. Requirements originate from `docs/MASTER_EXECUTION_TODO.md`, live issues/PRs, current code and physical evidence. Live source/evidence overrides stale historical text.

Audit baseline for this reconciliation: `dev` at `d9cd81bcf500a034d6cc88ea92e3bb74e42ed258` after PR #119.

| ID | Requirement group | Coder/Owner | QA | Current state | Tracking / done gate |
|---|---|---|---|---|---|
| R-GRID-01 | Zero Export control | ChatGPT | CI + FAT | SOFTWARE VERIFIED / physical qualification required | `solar_grid_control_source_contract.py`, `solar_grid_integration_test.c`; #83 exact load-step FAT |
| R-GRID-02 | Limited Export configurable cap | ChatGPT | CI + FAT | SOFTWARE VERIFIED / physical qualification required | current Solar-Grid contracts; #83 |
| R-GRID-03 | Minimum Grid Import Hold | ChatGPT | CI + FAT | SOFTWARE VERIFIED / physical qualification required | current Solar-Grid contracts; #83 |
| R-GRID-04 | grid loss, stale/invalid block, recovery/ramp | ChatGPT | CI + FAT | SOFTWARE VERIFIED / physical required | grid/source gates + integration tests; #83 |
| R-GEN-01 | Generator 1–3 roles/config/persistence | ChatGPT | GitHub Actions | COMPLETE/MERGED | PR #63 |
| R-GEN-02 | Generator 1–3 runtime aggregation | ChatGPT | GitHub Actions | COMPLETE/MERGED | PR #64 |
| R-GEN-03 | min loading/reserve/reverse margin fleet ceiling | ChatGPT | CI + FAT | SOFTWARE MERGED / physical required | PR #64; #83 |
| R-GEN-04 | explicit run/breaker strong evidence | ChatGPT | CI + site proof | SOFTWARE MERGED / real site mapping pending | PR #58; #81 |
| R-SRC-01 | Grid/Generator/Sync/Transfer/Island/NoSource/Conflict/Unknown modes | ChatGPT | CI + bench | SOFTWARE GREEN / BENCH GATED | Draft PR #106; #80 |
| R-SRC-02 | fresh dwell on carrying-source transition | ChatGPT | CI + bench | SOFTWARE GREEN / BENCH GATED | Draft PR #106; #80 |
| R-SRC-03 | fail closed on Transfer/Conflict/Stale | ChatGPT | CI + bench | SOFTWARE GREEN / BENCH GATED | Draft PR #106; #80 |
| R-MOD-01 | cumulative Modbus transaction deadline, including endpoint admission | ChatGPT | GitHub Actions | COMPLETE/MERGED | runtime deadline contracts + PR #114 merge `fa8ca9b17e08f2478e104942b9d6dbfad4f0ca7f`; public endpoint init fails closed on hostnames rather than entering unbounded synchronous DNS |
| R-MOD-02 | Modbus exception preservation/diagnostics | ChatGPT | GitHub Actions | COMPLETE IN LIVE ENGINE | runtime safety contracts |
| R-MOD-03 | per-transaction/persistent/reconnect-on-error modes | ChatGPT | GitHub Actions | COMPLETE/MERGED | PR #99 merge `3aa69162a6847a852e9b648ef8ec6988f5e3f296`; #88/#78 closed |
| R-MOD-04 | PCB/TIME_WAIT/multi-device/network endurance | site physical + ChatGPT plan | physical QA | NOT PHYSICALLY QUALIFIED | #83; software completion of R-MOD-01/03 does not satisfy endurance |
| R-ACQ-01 | no long Modbus scan in HTTP handler / cached acquisition | ChatGPT | GitHub Actions | COMPLETE IN LIVE ENGINE | EM500 cache/background acquisition contracts |
| R-ACQ-02 | freshness/quality/last-good/backoff diagnostics | ChatGPT | CI | SOFTWARE VERIFIED / endurance physical | EM500 + Modbus runtime contracts; #83 |
| R-INV-01 | production profile manual/model/firmware/identity mapping | ChatGPT + Owner | CI + bench | BLOCKED EXTERNAL | #82 |
| R-INV-02 | command width/scale/range/finite + FC06/FC16 semantics | ChatGPT | CI | COMPLETE/MERGED GENERIC CORE | PR #108 merge `b8996d5f834cc8edeb084f56c802ff5fc6ecd04d`; per-profile exact metadata still #82 |
| R-INV-03 | write/readback/tolerance/rollback/safe-zero transaction core | ChatGPT + site bench | CI + bench | GENERIC CORE SOFTWARE VERIFIED / MANUFACTURER BENCH PENDING | runtime write-gate contracts; #82/#83 |
| R-INV-04 | reconnect/stale identity reverification | ChatGPT | CI + bench | COMPLETE/MERGED GENERIC CORE / per-profile bench remains | PR #102 merge `b60bcb9474f4ed7dd0b0ed631410b54628a47d01`; #82 |
| R-INV-05 | profile assignment must disable control before new register map persists | ChatGPT | GitHub Actions | COMPLETE/MERGED | PR #117 head `115ec2f...`, full run `33729168281`, merge `1360c4a8356ff8acdc19878f65da311c0b0eccc6` |
| R-INV-06 | positive production writes require complete profile evidence + fresh mapped ON_GRID fleet status; fail-safe zero remains available | ChatGPT | GitHub Actions + bench | COMPLETE/MERGED GENERIC AUTHORITY / production profiles still blocked | PR #119 head `eef24d...`; focused `33729644794` and full `33729644800`; merge `d9cd81bcf500a034d6cc88ea92e3bb74e42ed258`; #82 physical profile approval |
| R-NET-01 | Wi-Fi config preservation/no compiled site credentials | ChatGPT | CI | COMPLETE/MERGED | PR #100 merge `7d9abdccb032b387525df9240b2943b74324a8e9`; provisioning remains opt-in |
| R-NET-02 | retry/recovery AP/single scan owner | ChatGPT | CI + soak | SOFTWARE VERIFIED / physical endurance remains | network commissioning contracts; #83 |
| R-WEB-01 | browser pollers bounded/cancellable | ChatGPT | GitHub Actions | COMPLETE AUDIT | #90 closed; served-bundle audit complete |
| R-WEB-02 | dead/unserved asset cleanup | ChatGPT | GitHub Actions | COMPLETE | PR #75; #90 closed |
| R-WEB-03 | error rendering / DOM mutation cannot freeze served UI | ChatGPT | GitHub Actions | COMPLETE/MERGED | PR #105 |
| R-AUTH-01 | production Engineering authentication/endpoints | ChatGPT | CI/security review | SOFTWARE VERIFIED | production access contracts |
| R-HTTP-01 | bounded body/depth/timeouts and no blocking/heap work under spinlocks | ChatGPT | CI | SOFTWARE VERIFIED/MERGED REGRESSION GATES | shared `http_json`; PR #107 web spinlock/nonblocking regression |
| R-NUM-01 | NaN/Inf/non-finite fail-safe | ChatGPT | unit/source contracts | SOFTWARE VERIFIED | Modbus/config/control contracts |
| R-OPS-01 | operator vs engineering product model | ChatGPT | CI/UI QA | SOFTWARE MATURE / HELD PRESENTATION SLICE | current product contracts; PR #54 held for post-Waveshare baseline |
| R-OPS-02 | truthful source/evidence/age and no fabricated zero | ChatGPT | CI/UI QA | SOFTWARE VERIFIED | telemetry/status contracts; physical UI qualification separate |
| R-WAVE-01 | no recurring LCD sweep/reload/flicker | site operator | physical QA | SHORT PASS EXACT CANDIDATE | #87/#27, source `87841ece...` |
| R-WAVE-02 | Alarms opens / touch remains responsive | site operator | physical QA | SHORT PASS EXACT CANDIDATE | #87/#27 |
| R-WAVE-03 | >=20 kB realistic DMA headroom | site operator | physical QA | PASS ON CURRENT CANDIDATE | #87/#27 |
| R-WAVE-04 | uninterrupted >=4 h exact-image soak | site operator | physical QA | INCOMPLETE: ~2 h/121 samples clean then USB dock/power lost | #87/#27; one new uninterrupted >=4 h run required |
| R-WAVE-05 | backend parity/recovery on accepted source | site + ChatGPT | physical/API QA | PENDING AFTER FINAL SOAK | #25/#87 |
| R-WAVE-06 | persistence/ARM save-readback-reboot/failure matrix | site + ChatGPT | physical QA | PENDING AFTER FINAL SOAK | #26/#87 |
| R-OTA-01 | secure rollback-safe web OTA software | ChatGPT | GitHub Actions | SOFTWARE COMPLETE | #50 / Draft PR #52 |
| R-OTA-02 | interruption/power-loss/previous-slot/pending-verify/rollback | site operator | physical QA | PENDING AFTER ACCEPTED WAVESHARE BASELINE | #86/#50 |
| R-SITE-01 | real breaker/run/ATS/sync mappings and polarity | Owner/site + ChatGPT | physical QA | BLOCKED PARTIAL EXTERNAL | #81 |
| R-SITE-02 | meter sign/scaling/topology proof | Owner/site | physical QA | BLOCKED PARTIAL EXTERNAL | #81/#80 |
| R-FAT-01 | Grid mode FAT | ChatGPT plan + site | physical/Owner | PENDING | #83 |
| R-FAT-02 | Generator fleet FAT | ChatGPT plan + site | physical/Owner | PENDING | #83 |
| R-FAT-03 | source-transfer FAT | ChatGPT plan + site | physical/Owner | PENDING | #80/#83 |
| R-FAT-04 | communication-loss/fail-safe/endurance FAT | ChatGPT plan + site | physical/Owner | PENDING | #83 |
| R-SAT-01 | signed SAT tied to exact firmware/config/profile identity | Owner/site + ChatGPT evidence | release QA | PENDING | #83/#91 |
| R-GOV-01 | live lane/owner/QA/dependency tracking | ChatGPT | diff review | V2 MERGED / CONTINUOUS | #79/#84 |
| R-GOV-02 | exact-head/zero-behind/expected-head merge gates | ChatGPT | GitHub Actions | ACTIVE GOVERNANCE | GATES.yaml / #93 |
| R-GOV-03 | exact evidence traceability / no cross-tree physical PASS | ChatGPT | release QA | ACTIVE GOVERNANCE | #91 |
| R-HW-01 | Rev-A PCB/enclosure/KiCad implementation | ChatGPT + hardware engineer | ERC/DRC/physical | SEPARATE TRACK | #85 / PR #18/#19 |

## Closure discipline

1. Inspect current `dev` before opening new work; do not resurrect resolved findings from stale audits without a live regression.
2. Software-complete items that require physical behavior stay open until observed evidence passes.
3. External manual/site evidence blockers are explicit; software must fail closed rather than guess values.
4. Every release-relevant update records exact branch/SHA, tests, blocker and whether that identity is safe to build, flash, bench-test or release.
5. Physical PASS is scoped to exact source/artifact/config/profile identity and does not transfer silently.
6. This matrix is a requirements snapshot; #84/#91 keep it synchronized as live state changes after L12/#92 closure.
