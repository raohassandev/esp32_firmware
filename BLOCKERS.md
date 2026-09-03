# AISH-OS Blocker Ledger v2

Master program: #79. Live evidence overrides stale snapshots. Current software integration baseline: `3096f2bfa10e86b3163b99ae7622bffded6791ac`.

## B-001 — Waveshare final continuous acceptance

**Lane:** L3 / #87 / #27  
**Candidate:** `87841ecee727fe1d814d4186be8c8c26e4afafb4`  
**State:** SHORT PASS / FINAL CONTINUOUS SOAK INCOMPLETE

Short display/touch/Alarms acceptance passed with healthy DMA/resources and zero recorded WDT/panic/NO_MEM/unexpected reset. The first continuous same-image soak produced about 2 h / 121 one-minute samples and 25 clean backend rounds, then the entire USB dock/power path disappeared. The required single uninterrupted >=4 h / >=240-sample run has not been achieved. Partial runs may not be combined.

PR #57 and parent PR #20 remain unpromoted; obsolete #46 stays closed.

## B-002 — Waveshare backend parity and persistence/ARM

**Issues:** #25/#26 / parent #87  
**State:** PHYSICAL PENDING AFTER B-001

After final soak PASS, complete HMI/Core/backend parity/recovery and save->readback->reboot/failure/ARM matrices on the exact accepted source/image before promotion.

## B-003 — Generator source-transition production merge

**Lane:** L2 / #80  
**Draft PR:** #106  
**State:** SOFTWARE GREEN / EXTERNAL BENCH BLOCKED

Physical Grid<->Generator/Transfer/Island/Sync/conflict/stale/source-loss behavior, actual run/breaker/ATS evidence and meter sign/scaling must pass before merge. Since `dev` has advanced, after physical PASS replay only the identical validated runtime slice onto current dev and obtain fresh exact-head CI.

## B-004 — Modbus/network physical endurance

**Lane:** L7 / #83  
**Software:** connection modes/deadlines complete  
**Physical:** NOT QUALIFIED

Remaining work is physical endurance: PCB/TIME_WAIT/socket/resource trends, healthy/slow/dead peers, Modbus exceptions, TCP/gateway resets, reconnect, Wi-Fi recovery and simultaneous multi-device load at commissioned rates.

## B-005 — Production inverter profiles

**Lane:** L6 / #82  
**State:** BLOCKED EXTERNAL MANUALS + BENCH

Generic engine safety is merged through #119, but no manufacturer profile is approved by generic software. Every production model still requires official manual/model/firmware identity, physical identity/telemetry/status proof, write/readback/rollback evidence and signed approval.

## B-006 — Real site source evidence

**Lane:** L5 / #81  
**State:** BLOCKED PARTIAL EXTERNAL EVIDENCE

Actual breaker/run/ATS/synchronism provenance, addresses/contacts, masks, active polarity, meter role/sign/scaling and topology are required. No kW-sign heuristic may manufacture breaker/sync state.

## B-007 — Secure OTA physical release qualification

**Lane:** L4 / #86/#50  
**Draft PR:** #52 software GREEN  
**State:** BLOCKED BY ACCEPTED WAVESHARE BASELINE + PHYSICAL OTA TESTS

After L3 closes, reconcile OTA onto the intended release baseline, run fresh exact-head CI, then physically prove invalid-image rejection, interrupted upload, power loss, previous-slot boot, pending verification, mark-valid and deliberate rollback without NVS/full-flash erase.

## B-008 — Integrated FAT/SAT

**Lane:** L7 / #83  
**State:** WAITING ON PHYSICAL/SITE/PROFILE QUALIFICATION

Software CI cannot close Grid/DG/mixed-source FAT, communication-loss endurance, physical fail-closed transitions or signed SAT. Evidence must be tied to exact firmware SHA, config, source mapping, approved profile and artifact identities.

## Resolved software blockers — do not reopen from stale audits without a live regression

- Modbus configurable connection modes / schema migration — PR #99.
- Compiled site STA defaults — PR #100.
- Browser served-poller audit — #90 closed.
- Inverter reconnect/stale identity — PR #102.
- Generic command width/scale/range semantics — PR #108.
- Unbounded synchronous DNS endpoint path — PR #114.
- Profile assignment persisted before control disable — PR #117.
- Positive production write without complete live evidence/status authority — PR #119.
- Legacy schema migration allocation failure replacing commissioned NVS — PR #122, merge `dfe93de50e2a5715f4d212ff3233d566d36e2cfd`.
- Safety alarm reader transiently observing a false all-clear during curtailment — PR #124, merge `3096f2bfa10e86b3163b99ae7622bffded6791ac`.

## Non-blocking separate track

Rev-A custom PCB/KiCad is #85 / PR #18/#19 and does not block the Waveshare firmware release unless the Product Owner explicitly couples milestones.
