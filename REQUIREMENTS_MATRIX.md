# AISH-OS Requirements Closure Matrix v1

Master program: #79. Requirements originate from `docs/MASTER_EXECUTION_TODO.md`, live issues/PRs, current code and physical evidence. **VERIFY_LIVE** means the historical requirement must be checked against current `dev` before new work is opened; it does not mean missing.

Audit baseline for this reconciliation: `dev` at `7d9abdccb032b387525df9240b2943b74324a8e9` after PR #99 and PR #100.

| ID | Requirement group | Coder/Owner | QA | Current state | Tracking / done gate |
|---|---|---|---|---|---|
| R-GRID-01 | Zero Export control | ChatGPT | CI + FAT | SOFTWARE VERIFIED / physical qualification required | `solar_grid_control_source_contract.py`, `solar_grid_integration_test.c`; #83 exact load-step FAT |
| R-GRID-02 | Limited Export configurable cap | ChatGPT | CI + FAT | SOFTWARE VERIFIED / physical qualification required | `solar_grid_control_source_contract.py`, `solar_grid_integration_test.c`; #83 |
| R-GRID-03 | Minimum Grid Import Hold | ChatGPT | CI + FAT | SOFTWARE VERIFIED / physical qualification required | `solar_grid_control_source_contract.py`, `solar_grid_integration_test.c`; #83 |
| R-GRID-04 | grid loss, stale/invalid block, recovery/ramp | ChatGPT | CI + FAT | SOFTWARE VERIFIED / physical required | grid gate + source contract + integration tests; #83 |
| R-GEN-01 | Generator 1–3 roles/config/persistence | ChatGPT | GitHub Actions | COMPLETE/MERGED | PR #63 |
| R-GEN-02 | Generator 1–3 runtime aggregation | ChatGPT | GitHub Actions | COMPLETE/MERGED | PR #64 |
| R-GEN-03 | min loading/reserve/reverse margin fleet ceiling | ChatGPT | CI + FAT | SOFTWARE MERGED / physical required | PR #64, #83 |
| R-GEN-04 | explicit run/breaker strong evidence | ChatGPT | CI + site proof | SOFTWARE MERGED / site mapping pending | PR #58, #81 |
| R-SRC-01 | Grid/Generator/Sync/Transfer/Island/NoSource/Conflict/Unknown modes | ChatGPT | CI + bench | SOFTWARE READY / bench gated | PR #77, #80 |
| R-SRC-02 | fresh dwell on carrying-source transition | ChatGPT | CI + bench | SOFTWARE READY / bench gated | PR #77, #80 |
| R-SRC-03 | fail closed on Transfer/Conflict/Stale | ChatGPT | CI + bench | SOFTWARE READY / bench gated | PR #77, #80 |
| R-MOD-01 | cumulative Modbus transaction deadline | ChatGPT | GitHub Actions | COMPLETE IN LIVE ENGINE | runtime safety contract; #78 context |
| R-MOD-02 | Modbus exception preservation/diagnostics | ChatGPT | GitHub Actions | COMPLETE IN LIVE ENGINE | runtime safety contract; #78 context |
| R-MOD-03 | per-transaction/persistent/reconnect-on-error modes | ChatGPT | GitHub Actions | COMPLETE/MERGED | PR #99; merge `3aa69162a6847a852e9b648ef8ec6988f5e3f296`; focused + full CI GREEN; #88/#78 closed |
| R-MOD-04 | PCB/TIME_WAIT/multi-device endurance | Claude/site + ChatGPT plan | physical QA | NOT PHYSICALLY QUALIFIED | #83; software completion of R-MOD-03 does not satisfy this gate |
| R-ACQ-01 | no long Modbus scan in HTTP handler / cached acquisition | ChatGPT | GitHub Actions | COMPLETE IN LIVE ENGINE | `em500_snapshot_source_contract.py`: HTTP cache adapter only, background acquisition owns real Modbus I/O |
| R-ACQ-02 | freshness/quality/last-good/backoff diagnostics | ChatGPT | CI | SOFTWARE VERIFIED | EM500 cache last-good contract + `modbus_runtime_safety_source_contract.py`; physical endurance remains #83 |
| R-INV-01 | production profile manual/identity mapping | ChatGPT + Owner | CI + bench | BLOCKED EXTERNAL | #82 |
| R-INV-02 | command width/scale/range/finite validation | ChatGPT | CI + bench | VERIFY_LIVE PER PROFILE | #82 |
| R-INV-03 | write/readback/tolerance/rollback/safe-zero | ChatGPT + Claude | CI + bench | BLOCKED PER PROFILE | #82, #83 |
| R-INV-04 | reconnect identity reverification | ChatGPT | CI + bench | IMPLEMENTED / CONTRACT HARDENING ACTIVE | `inverter_manager.c` invalidates identity on failed/stale telemetry and enforces periodic recheck; focused regression lane active; per-profile bench qualification stays #82 |
| R-NET-01 | Wi-Fi config preservation/no default credentials | ChatGPT | CI | COMPLETE/MERGED | PR #100; merge `7d9abdccb032b387525df9240b2943b74324a8e9`; build STA defaults removed, provisioning remains opt-in |
| R-NET-02 | retry/recovery AP/single scan owner | ChatGPT | CI + soak | SOFTWARE VERIFIED / soak remains | `network_commissioning_flow_source_contract.py`; manager-task radio ownership + bounded scan; #83 physical soak |
| R-WEB-01 | browser pollers bounded/cancellable | ChatGPT | GitHub Actions | COMPLETE AUDIT | #90 closed; PRs #59/#62/#65/#69/#71/#73/#76 plus active-served-bundle closure sweep |
| R-WEB-02 | dead/unserved asset cleanup | ChatGPT | GitHub Actions | COMPLETE | PR #75; #90 closed |
| R-AUTH-01 | production Engineering authentication/endpoints | ChatGPT | CI/security review | SOFTWARE VERIFIED | `product_engineering_access_source_contract.py`: PBKDF2/session-cookie/lockout/gateway contracts; physical release review remains governed separately |
| R-HTTP-01 | bounded body/depth/timeouts, no unsafe serialization under locks | ChatGPT | CI | CORE WRITE PATHS VERIFIED / final lock audit retained | `config_import_safety_source_contract.py` + shared `http_json` bounded parser across write APIs; #92 retains any residual lock-lifetime audit |
| R-NUM-01 | NaN/Inf/non-finite fail-safe | ChatGPT | unit tests | SOFTWARE VERIFIED | config-import + Modbus runtime safety contracts and power-control policy tests |
| R-OPS-01 | operator vs engineering product model | ChatGPT | CI/UI QA | PARTIAL/MATURE; final requirements audit | #92 |
| R-OPS-02 | truthful source/evidence/age and no fabricated zero | ChatGPT | CI/UI QA | SOFTWARE VERIFIED | `telemetry_source_contract.py` + config/status null handling; physical UI qualification remains separate |
| R-WAVE-01 | no recurring LCD sweep/reload/flicker | Claude/site | physical QA | SHORT PASS | #87/#27 |
| R-WAVE-02 | Alarms opens / touch remains responsive | Claude/site | physical QA | SHORT PASS | #87/#27 |
| R-WAVE-03 | >=20 kB realistic DMA headroom | Claude/site | physical QA | PASS ON CURRENT CANDIDATE | #87/#27 |
| R-WAVE-04 | uninterrupted >=4 h exact-image soak | Claude/site | physical QA | INCOMPLETE: ~2 h/121 samples clean then USB hub lost | #87/#27 |
| R-WAVE-05 | backend parity/recovery on accepted source | Claude/site + ChatGPT | physical + API QA | PENDING | #25/#87 |
| R-WAVE-06 | persistence/ARM save-readback-reboot/failure matrix | Claude/site + ChatGPT | physical QA | PENDING | #26/#87 |
| R-OTA-01 | secure rollback-safe web OTA software | ChatGPT | GitHub Actions | SOFTWARE COMPLETE | #50 / PR #52 |
| R-OTA-02 | interruption/power-loss/previous-slot/pending-verify/rollback | Claude/site | physical QA | PENDING AFTER WAVESHARE GRAPH | #86/#50 |
| R-SITE-01 | real breaker/run/ATS/sync mappings and polarity | Owner/site + ChatGPT | physical QA | BLOCKED PARTIAL EXTERNAL | #81 |
| R-SITE-02 | meter sign/scaling/topology proof | Owner/site + Claude | physical QA | BLOCKED PARTIAL EXTERNAL | #81/#80 |
| R-FAT-01 | Grid mode FAT | ChatGPT plan + Claude | physical/Owner | PENDING | #83 |
| R-FAT-02 | Generator fleet FAT | ChatGPT plan + Claude | physical/Owner | PENDING | #83 |
| R-FAT-03 | source-transfer FAT | ChatGPT plan + Claude | physical/Owner | PENDING | #80/#83 |
| R-FAT-04 | communication-loss/fail-safe/endurance FAT | ChatGPT plan + Claude | physical/Owner | PENDING | #83 |
| R-SAT-01 | signed SAT tied to exact firmware/config/profile identity | Owner/site + ChatGPT evidence | release QA | PENDING | #83/#91 |
| R-GOV-01 | live AISH lane/owner/QA/dependency tracking | ChatGPT | diff review | V2 MERGED / CONTINUOUS | #79/#84; PR #97 merge `430e9157...` |
| R-GOV-02 | exact-head/zero-behind/expected-head merge gates | ChatGPT | GitHub Actions | ACTIVE GOVERNANCE | GATES.yaml / #93 |
| R-GOV-03 | evidence traceability / no cross-tree physical PASS | ChatGPT | release QA | ACTIVE GOVERNANCE | #91 |
| R-HW-01 | Rev-A PCB/enclosure/KiCad implementation | ChatGPT + hardware engineer | ERC/DRC/physical | SEPARATE TRACK | #85 / PR #18/#19 |

## Closure discipline

1. `VERIFY_LIVE` requirements must first be inspected on current `dev`; if already implemented/tested, mark complete with exact evidence instead of opening duplicate coding work.
2. Software-complete items that require physical behavior stay open until observed evidence passes.
3. External manual/site evidence blockers are owned explicitly; coders must not guess values to unblock themselves.
4. Every lane update records branch/SHA, tests, blocker, and whether that SHA is safe to build, flash, bench-test or release.
5. This matrix is updated by L12/#92 and evidence references are synchronized through L11/#91.
