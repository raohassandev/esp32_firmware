# AISH-OS Requirements Closure Matrix v1

Master program: #79. Live source/evidence overrides stale historical text. Audit baseline for this reconciliation: `dev` at `1a3b0f4ee73ae08588caee7b46f9ab87d1e5b491` after governed PR #148 merge.

| ID | Requirement group | Current state | Tracking / done gate |
|---|---|---|---|
| R-GRID-01 | Zero Export control | SOFTWARE VERIFIED / physical required | current grid policy/integration contracts; #83 |
| R-GRID-02 | Limited Export configurable cap | SOFTWARE VERIFIED / physical required | current grid policy/integration contracts; #83 |
| R-GRID-03 | Minimum Grid Import Hold | SOFTWARE VERIFIED / physical required | current grid policy/integration contracts; #83 |
| R-GRID-04 | grid loss/stale/invalid block/recovery/ramp | SOFTWARE VERIFIED / physical required | source/grid gates; #83 |
| R-GEN-01 | Generator 1–3 roles/config/persistence | COMPLETE/MERGED | PR #63 |
| R-GEN-02 | Generator 1–3 runtime aggregation | COMPLETE/MERGED | PR #64 |
| R-GEN-03 | minimum loading/reserve/reverse margin fleet ceiling | SOFTWARE MERGED / physical required | PR #64; #83 |
| R-GEN-04 | explicit run/breaker strong evidence | SOFTWARE MERGED / real site mapping pending | PR #58; #81 |
| R-SRC-01 | Grid/Generator/Sync/Transfer/Island/NoSource/Conflict/Unknown | SOFTWARE GREEN / BENCH GATED | Draft PR #106; #80 |
| R-SRC-02 | fresh dwell on carrying-source transition | SOFTWARE GREEN / BENCH GATED | Draft PR #106; #80 |
| R-SRC-03 | fail closed on Transfer/Conflict/Stale | SOFTWARE GREEN / BENCH GATED | Draft PR #106; #80 |
| R-MOD-01 | cumulative Modbus deadline + endpoint admission | COMPLETE/MERGED | PR #114 `fa8ca9b17e08f2478e104942b9d6dbfad4f0ca7f` |
| R-MOD-02 | Modbus exception preservation/diagnostics | COMPLETE IN LIVE ENGINE | runtime safety contracts |
| R-MOD-03 | per-transaction/persistent/reconnect-on-error | COMPLETE/MERGED | PR #99 `3aa69162a6847a852e9b648ef8ec6988f5e3f296` |
| R-MOD-04 | PCB/TIME_WAIT/multi-device/network endurance | NOT PHYSICALLY QUALIFIED | #83 |
| R-ACQ-01 | no long Modbus scan in HTTP handler / cached acquisition | COMPLETE IN LIVE ENGINE | EM500 background acquisition contracts |
| R-ACQ-02 | freshness/quality/last-good/backoff diagnostics | SOFTWARE VERIFIED / physical endurance remains | #83 |
| R-CONFIG-01 | imported config numeric/depth/bounds fail closed and cannot arm control | SOFTWARE VERIFIED / HARDENED | legacy import contracts + PR #135 schema-6 pre-cJSON depth guard |
| R-CONFIG-02 | legacy migration allocation failure cannot replace commissioned NVS | COMPLETE/MERGED | PR #122 `dfe93de50e2a5715f4d212ff3233d566d36e2cfd` |
| R-CONFIG-03 | generic config/meter/inverter/profile mutations revoke live command authority before persistence | COMPLETE/MERGED | PR #127 `df282a3e8afee27dfc220694e4461e4ad49d2277` |
| R-CONFIG-04 | Wi-Fi transport configuration revokes live command authority before persistence | COMPLETE/MERGED | PR #129 `5e5a63dba3e157d2658227ff691e9f975cedff96` |
| R-CONFIG-05 | source-detection topology/register/threshold mutation revokes live command authority before persistence | COMPLETE/MERGED | PR #130 `0969a119e4fcb97405da26a59b55ec44a5a292f4` |
| R-CONFIG-06 | current safety-relevant commissioning mutation surfaces covered by one regression inventory | COMPLETE/MERGED | PR #131 `41eaf22f8b92057cdbe5427c590ccd84d7fbce9b` |
| R-CONFIG-07 | schema-6 init/import/export avoid whole config task-stack snapshots | COMPLETE/MERGED | PR #137 `43d4bd509ea07aadbd9ea18e9813e3ec11c60297` |
| R-CONFIG-08 | project runtime C sources cannot introduce whole automatic `app_config_t` task-stack frames, including arrays/multi-declarators/common qualifiers | COMPLETE/MERGED REGRESSION GATE | PR #138 + PR #140 `093954b5626c034e126fde3b773cedb1add92707` |
| R-INV-01 | production profile manual/model/firmware/identity mapping | BLOCKED EXTERNAL | #82 |
| R-INV-02 | command width/scale/range/finite + FC06/FC16 | COMPLETE/MERGED GENERIC CORE | PR #108 |
| R-INV-03 | write/readback/tolerance/rollback/safe-zero core | GENERIC CORE VERIFIED / manufacturer bench pending | #82/#83 |
| R-INV-04 | reconnect/stale identity reverification | COMPLETE/MERGED GENERIC CORE | PR #102; #82 bench remains |
| R-INV-05 | profile assignment disables control before new map persists | COMPLETE/MERGED | PR #117 `1360c4a8356ff8acdc19878f65da311c0b0eccc6` |
| R-INV-06 | positive writes require complete profile evidence + fresh mapped ON_GRID; zero remains fail-safe | COMPLETE/MERGED GENERIC AUTHORITY / profiles still blocked | PR #119 `d9cd81bcf500a034d6cc88ea92e3bb74e42ed258`; #82 |
| R-INV-07 | production release cannot pass with zero actual production-approved inverter profiles | COMPLETE/MERGED | PR #112 `7a683092696c6334870b5c1007e086f8556dbc88`; #82 remains blocking |
| R-INV-08 | pending manufacturer profile transport explicitly unqualified, never guessed | COMPLETE/MERGED | PR #113 `4aa935dfcf944f83cbb96333be67f99878e32c30` |
| R-INV-09 | profile-assignment config snapshot does not consume a full HTTP-task stack frame | COMPLETE/MERGED | PR #136 `321198bb5d970aa5f4842331a229ce63475e0776` |
| R-NET-01 | Wi-Fi config preservation/no compiled site credentials | COMPLETE/MERGED | PR #100 |
| R-NET-02 | retry/recovery AP/single scan owner | SOFTWARE VERIFIED / physical endurance remains | #83 |
| R-NET-03 | successful Wi-Fi scan results are released on zero-result/allocation/fetch failure paths | COMPLETE/MERGED | PR #133 `e918aef8465435d4af87eaa6c1f001767a9d2170` |
| R-WEB-01 | browser pollers bounded/cancellable | COMPLETE AUDIT | #90 closed |
| R-WEB-02 | dead/unserved asset cleanup | COMPLETE | PR #75; #90 |
| R-WEB-03 | error rendering/DOM mutation cannot freeze served UI | COMPLETE/MERGED | PR #105 |
| R-AUTH-01 | production Engineering authentication/endpoints | SOFTWARE VERIFIED | production access contracts |
| R-HTTP-01 | bounded body/depth/timeouts; reviewed bounded OTA binary stream; no blocking/heap work under spinlocks | SOFTWARE VERIFIED/MERGED REGRESSION GATES | shared `http_json`; PR #107; PR #135; PR #145 |
| R-NUM-01 | NaN/Inf/non-finite fail-safe | SOFTWARE VERIFIED | decoder/config/control contracts |
| R-SAFE-01 | safety alarm snapshot cannot expose transient false all-clear | COMPLETE/MERGED | PR #124 `3096f2bfa10e86b3163b99ae7622bffded6791ac` |
| R-OPS-01 | operator vs engineering product model, continuity and truthful Plant verdict | COMPLETE/MERGED | PR #144 `8b5fce29aaf0de7ec9a5531ad3ea66c78e4539ed`; focused `33771513776`; full `33771513697` |
| R-OPS-02 | truthful source/evidence/age and no fabricated zero | SOFTWARE VERIFIED | telemetry/status contracts |
| R-WAVE-01 | no recurring LCD sweep/reload/flicker | SHORT PASS EXACT CANDIDATE | #87/#27 `87841ece...` |
| R-WAVE-02 | Alarms opens / touch responsive | SHORT PASS EXACT CANDIDATE | #87/#27 |
| R-WAVE-03 | >=20 kB realistic DMA headroom | PASS ON CURRENT CANDIDATE | #87/#27 |
| R-WAVE-04 | uninterrupted >=4 h exact-image soak | INCOMPLETE: ~2 h/121 samples clean then USB dock/power lost | #87/#27; PR #142 validates evidence but cannot create it |
| R-WAVE-05 | backend parity/recovery on accepted source | PENDING AFTER FINAL SOAK | #25/#87 |
| R-WAVE-06 | persistence/ARM save-readback-reboot/failure | PENDING AFTER FINAL SOAK | #26/#87 |
| R-WAVE-07 | deterministic final acceptance + exact package evidence validation available on live integration branch | COMPLETE/MERGED TOOLING | PR #142 `d07dca2d2b20a2cf4e712df45fae9dfe7e3024c2`; focused `33768630723`; full `33768630667`; physical thresholds remain authoritative |
| R-OTA-01 | secure rollback-safe web OTA software on current `dev` | COMPLETE/MERGED | PR #145 head `7ab58704c72cf61eca858e8004c12094a0d6bbe3`; merge `73bcb3e3a57dd482ac87b174906254ad60c8575b`; focused `33773071036`; HTTP ownership `33773071278`; full `33773071302` |
| R-OTA-02 | interruption/power-loss/previous-slot/pending-verify/rollback | PENDING PHYSICAL ON EXACT INTENDED OTA RELEASE IDENTITY | #86/#50; CI is not physical PASS |
| R-OTA-03 | OTA-specific regression contracts run on every PR and every `dev` push | COMPLETE/MERGED REGRESSION GATE | PR #148 head `9ed4ec82066ca1687a5a8ce4b4f2cf130281f44a`; always-on `33778878536`; full `33778878091`; merge `1a3b0f4ee73ae08588caee7b46f9ab87d1e5b491` |
| R-SITE-01 | real breaker/run/ATS/sync mappings and polarity | BLOCKED PARTIAL EXTERNAL | #81 |
| R-SITE-02 | meter sign/scaling/topology proof | BLOCKED PARTIAL EXTERNAL | #81/#80 |
| R-FAT-01 | Grid mode FAT | PENDING PHYSICAL | #83 |
| R-FAT-02 | Generator fleet FAT | PENDING PHYSICAL | #83 |
| R-FAT-03 | source-transfer FAT | PENDING PHYSICAL | #80/#83 |
| R-FAT-04 | communication-loss/fail-safe/endurance FAT | PENDING PHYSICAL | #83 |
| R-SAT-01 | signed SAT tied to exact firmware/config/profile | PENDING | #83/#91 |
| R-GOV-01 | live lane/owner/QA/dependency tracking | MERGED / CONTINUOUS | #79/#84; PR #143 reconciled through #142; current reconciliation follows #148 |
| R-GOV-02 | exact-head/zero-behind/expected-head merge gates | ACTIVE | #93 |
| R-GOV-03 | exact evidence traceability/no cross-tree PASS | ACTIVE | #91 |
| R-HW-01 | Rev-A PCB/enclosure/KiCad | SEPARATE TRACK | #85 / PR #18/#19 |

## Closure discipline

1. Inspect current `dev` before opening work; do not resurrect stale findings without a live regression.
2. Software-complete items requiring physical behavior remain release-open until observed evidence passes.
3. External manual/site blockers remain explicit; software must fail closed rather than guess.
4. Runtime configuration/mapping changes that invalidate live assumptions must remove command authority before persistence.
5. Release evidence records exact SHA/run/artifact/config/profile identity.
6. Physical PASS does not transfer silently across changed identities.
7. Evidence validators and CI regression gates may reject insufficient evidence but must never lower physical thresholds or substitute CI for observation.
8. A software merge onto `dev` does not transfer any physical qualification from the frozen Waveshare candidate to the newer integration image.
