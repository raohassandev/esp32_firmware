# AISH-OS Blocker Ledger v2

Master program: #79. Live evidence overrides stale snapshots.

## B-001 — Waveshare final continuous acceptance

**Lane:** L3 / #87  
**Detailed issues:** #24 #25 #26 #27  
**Candidate:** `87841ecee727fe1d814d4186be8c8c26e4afafb4`  
**State:** SHORT PASS / FINAL CONTINUOUS SOAK INCOMPLETE

Short display/touch/Alarms acceptance passed and runtime DMA headroom is now comfortably above the former >=20 kB target. The first continuous same-image soak produced about 2 hours / 121 consecutive one-minute samples with zero WDT/panic/NO_MEM/reboot/resource collapse, then the bench USB hub/power path disappeared. There is no firmware-crash evidence, but AISH hardware rules require one uninterrupted >=4 h same-image evidence run (>=240 one-minute samples). Do not waive or add partial runs together.

PR #57 and parent PR #20 therefore remain unpromoted. Obsolete promotion PR #46 stays closed.

## B-002 — Waveshare backend parity and persistence/ARM matrices

**Issues:** #25 #26  
**State:** BLOCKED_EXTERNAL / after final soak evidence

Short backend health and fail-closed behavior are clean, but full HMI/Core parity/recovery and save->readback->reboot/failure/ARM matrices are not yet physically proven. Complete without silently rebuilding/substituting the accepted source identity.

## B-003 — Generator source-transition production merge

**Lane:** L2 / #80  
**PR:** #77  
**Software:** exact-head GREEN  
**State:** BLOCKED_EXTERNAL_BENCH

Software now admits qualified Generator-only, Island and synchronized modes with fresh recovery dwell while Transfer/Conflict/Stale remain fail-closed. Because this changes industrial PV command admission, PR #77 must not merge from CI alone. Real Grid<->Generator/Island/Sync/ATS/run/breaker/meter evidence and command behavior must pass first.

## B-004 — Modbus TCP connection modes

**Lane:** L1 / #88 / #78  
**State:** SOFTWARE EXECUTING

Core WIP exists on `work/modbus/connection-modes`, but safe config/NVS migration, meter/inverter exposure, complete tests, fresh PR/CI and governed merge are unfinished. PCB/TIME_WAIT endurance remains a later physical gate in #83.

## B-005 — Production inverter profiles

**Lane:** L6 / #82  
**State:** BLOCKED_EXTERNAL_MANUALS_AND_BENCH

Production writes remain blocked per model until exact manufacturer documentation, identity/telemetry/command/readback mapping, simulator/source-contract evidence, bench write/readback/rollback proof and signed production approval exist. No third-party guessed register map is acceptable.

## B-006 — Real site source evidence

**Lane:** L5 / #81  
**State:** BLOCKED_PARTIAL_EXTERNAL_EVIDENCE

Software supports strong grid/generator evidence, but real breaker/run/ATS/synchronism provenance, addresses/contacts, masks, active polarity, meter sign/scaling and topology must come from actual manuals/site wiring. Defaults remain fail-closed.

## B-007 — Secure OTA release qualification

**Lane:** L4 / #86 / #50  
**PR:** #52 software GREEN  
**State:** BLOCKED_BY_WAVESHARE_SOURCE_GRAPH_AND_PHYSICAL_OTA_TESTS

After L3 closes, replay/reconcile OTA onto the intended release baseline, run fresh exact-head CI, then physically prove interrupted upload, power loss, previous-slot boot, pending verification, mark-valid and deliberate rollback without NVS/full-flash erase.

## B-008 — Integrated endurance/FAT/SAT

**Lane:** L7 / #83  
**State:** WAITING_ON_COMPONENT_AND_SOURCE_QUALIFICATION

Software CI cannot close Grid/DG/mixed-source FAT, TCP PCB/TIME_WAIT endurance, physical fail-closed transitions or signed SAT. Evidence must be tied to exact firmware SHA/config/profile identities.

## Non-blocking separate track

Rev-A custom PCB/KiCad work is tracked by L9 / #85 and PRs #18/#19. It does not block the current Waveshare firmware release unless the Product Owner explicitly couples the milestones.
