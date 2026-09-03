# AISH-OS Blocker Ledger v2

Master program: #79. Live evidence overrides stale snapshots. Current software integration baseline: `d9cd81bcf500a034d6cc88ea92e3bb74e42ed258`.

## B-001 — Waveshare final continuous acceptance

**Lane:** L3 / #87 / #27  
**Candidate:** `87841ecee727fe1d814d4186be8c8c26e4afafb4`  
**State:** SHORT PASS / FINAL CONTINUOUS SOAK INCOMPLETE

Short display/touch/Alarms acceptance passed on the exact candidate with healthy DMA/resources and zero WDT/panic/NO_MEM/unexpected reset. The first continuous same-image soak produced about 2 h / 121 consecutive one-minute samples and 25 clean backend rounds, then the entire bench USB dock/power path disappeared. There is no recorded firmware crash, but the required single uninterrupted >=4 h / >=240-sample run has not been achieved. Partial runs may not be added together.

PR #57 and parent PR #20 remain unpromoted. Obsolete PR #46 stays closed.

## B-002 — Waveshare backend parity and persistence/ARM matrices

**Issues:** #25 #26 / parent #87  
**State:** PHYSICAL PENDING AFTER B-001

After final soak PASS, complete HMI/Core/backend parity/recovery plus save->readback->reboot/failure/ARM matrices on the exact accepted source/image before promotion. No rebuild/source substitution may inherit the existing physical PASS.

## B-003 — Generator source-transition production merge

**Lane:** L2 / #80  
**Draft PR:** #106  
**State:** SOFTWARE GREEN / EXTERNAL BENCH BLOCKED

Software supports explicit Grid-only, Generator-only, Island and synchronized carrying modes with fresh recovery dwell and immediate fail-closed Transfer/Conflict/Stale behavior. Because this changes industrial command admission, physical Grid<->Generator/Island/Sync/ATS/run/breaker/meter-sign evidence must pass before merge. If live `dev` has advanced, replay the identical validated runtime slice only after bench PASS and obtain fresh exact-head CI.

## B-004 — Modbus/network physical endurance

**Lane:** L7 / #83  
**Software state:** COMPLETE for connection modes/deadlines  
**Physical state:** NOT QUALIFIED

PR #99 merged all three connection modes; PR #114 closed the synchronous-DNS endpoint path that could escape the cumulative transaction deadline. Remaining work is physical endurance only: PCB/TIME_WAIT/socket/resource trends, slow/dead peers, exceptions, resets/reconnect, Wi-Fi recovery and simultaneous multi-device load at commissioned rates.

## B-005 — Production inverter profiles

**Lane:** L6 / #82  
**State:** BLOCKED EXTERNAL MANUALS_AND_BENCH

Generic engine safety is merged: reconnect identity revalidation (#102), command width/scale/range/FC06/FC16 hardening (#108), profile-change control-disable ordering (#117), and complete positive-write authority/fresh ON_GRID gating (#119). These do not approve a manufacturer register map. Every production model still needs exact official manual/model/firmware identity, read-only identity/telemetry/status proof, command/readback/rollback bench evidence and signed production approval.

## B-006 — Real site source evidence

**Lane:** L5 / #81  
**State:** BLOCKED PARTIAL EXTERNAL EVIDENCE

Real breaker/run/ATS/synchronism provenance, address/contact, mask, active polarity, meter role/sign/scaling and topology must be documented from actual manuals/site wiring. No kW-sign heuristic may manufacture breaker or synchronism state.

## B-007 — Secure OTA release qualification

**Lane:** L4 / #86 / #50  
**Draft PR:** #52 software GREEN  
**State:** BLOCKED BY ACCEPTED WAVESHARE BASELINE + PHYSICAL OTA TESTS

After L3 closes, replay/reconcile OTA onto the intended release baseline, run fresh exact-head CI, then physically prove invalid-image rejection, interrupted upload, power loss, previous-slot boot, pending verification, mark-valid and deliberate rollback without NVS/full-flash erase.

## B-008 — Integrated endurance/FAT/SAT

**Lane:** L7 / #83  
**State:** WAITING ON PHYSICAL/SITE/PROFILE QUALIFICATION

Software CI cannot close Grid/DG/mixed-source FAT, communication-loss endurance, physical fail-closed transitions or signed SAT. Evidence must be tied to exact firmware SHA, configuration, source mapping, approved profile and artifact identities.

## Non-blocking separate track

Rev-A custom PCB/KiCad work is tracked by #85 and PR #18/#19. It does not block the current Waveshare firmware release unless the Product Owner explicitly couples the milestones.

## Resolved software blockers — historical only

- Modbus configurable connection modes / safe schema migration: resolved PR #99.
- Compiled site STA defaults: resolved PR #100.
- Browser served-poller final audit: #90 closed.
- Inverter reconnect/stale identity core: resolved PR #102.
- Generic command width/scale/range semantics: resolved PR #108.
- Unbounded synchronous DNS endpoint path: resolved PR #114.
- Profile assignment persisted before control disable: resolved PR #117.
- Positive production write without complete live evidence/status authority: resolved PR #119.

Do not reopen resolved blockers from stale TODO text without a current live regression.
