# AISH-OS Blocker Ledger v2

Master program: #79. Live evidence overrides stale snapshots. Current software integration baseline: `093954b5626c034e126fde3b773cedb1add92707` after PR #140.

## B-001 — Waveshare final continuous acceptance

**Lane:** L3 / #87 / #27  
**Candidate:** `87841ecee727fe1d814d4186be8c8c26e4afafb4`  
**State:** SHORT PASS / FINAL CONTINUOUS SOAK INCOMPLETE

Short display/touch/Alarms acceptance passed with healthy DMA/resources and zero recorded WDT/panic/NO_MEM/unexpected reset. The first continuous same-image soak produced about 2 h / 121 one-minute samples and 25 clean backend rounds, then the USB dock/power path disappeared. A new single uninterrupted >=4 h / >=240-sample run is required; partial runs may not be combined. PR #57 and parent PR #20 remain unpromoted; obsolete #46 stays closed.

## B-002 — Waveshare backend parity and persistence/ARM

**Issues:** #25/#26 / parent #87  
**State:** PHYSICAL PENDING AFTER B-001

After final soak PASS, complete HMI/Core/backend parity/recovery and save->readback->reboot/failure/ARM matrices on the exact accepted source/image before promotion.

## B-003 — Generator source-transition production merge

**Lane:** L2 / #80  
**Draft PR:** #106  
**State:** SOFTWARE GREEN / EXTERNAL BENCH BLOCKED

Physical Grid<->Generator/Transfer/Island/Sync/conflict/stale/source-loss behavior, actual run/breaker/ATS evidence and meter sign/scaling must pass before merge. Because `dev` has advanced, any eventual merge must replay only the identical validated runtime slice onto current `dev` and obtain fresh exact-head CI.

## B-004 — Modbus/network physical endurance

**Lane:** L7 / #83  
**Software:** connection modes/deadlines complete via PR #99/#114  
**Physical:** NOT QUALIFIED

Remaining work is physical endurance: PCB/TIME_WAIT/socket/resource trends, healthy/slow/dead peers, Modbus exceptions, TCP/gateway resets, reconnect, Wi-Fi recovery and simultaneous multi-device load at commissioned rates.

## B-005 — Production inverter profiles

**Lane:** L6 / #82  
**State:** BLOCKED EXTERNAL OFFICIAL MANUALS + BENCH

Generic engine safety is merged. PR #112 prevents production release with zero actual production-approved real profiles, and PR #113 keeps pending manufacturer transport explicitly unqualified. Every production model still requires exact official manual/model/firmware identity, physical identity/telemetry/status proof, write/readback/rollback evidence and signed approval.

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
- Browser served-poller audit — #90 closed.
- Inverter reconnect/stale identity — PR #102.
- Generic command width/scale/range semantics — PR #108.
- Production release accepting zero real approved inverter profiles — PR #112.
- Pending manufacturer transport truthfulness — PR #113.
- Unbounded synchronous DNS endpoint path — PR #114.
- Profile assignment persisted before control disable — PR #117.
- Positive production write without complete live evidence/status authority — PR #119.
- Legacy schema migration allocation failure replacing commissioned NVS — PR #122.
- Safety alarm reader transient false all-clear — PR #124.
- Generic config/meter/inverter/profile persistence leaving command authority active — PR #127.
- Wi-Fi configuration persistence leaving command authority active — PR #129.
- Source-detection configuration persistence leaving command authority active — PR #130.
- Missing consolidated commissioning-mutation interlock inventory — PR #131.
- Wi-Fi driver scan-result lifetime leak on failed/empty scan paths — PR #133.
- Schema-6 wrapper parsing JSON ahead of the established depth gate — PR #135.
- Whole `app_config_t` snapshot on the inverter-profile HTTP-task path — PR #136.
- Whole schema-6 init/import/export config snapshots on main/HTTP task stacks — PR #137.
- Runtime-component automatic `app_config_t` stack-frame regression gap — PR #138.
- Project-wide guard missing `main/` ownership and declaration escape forms — PR #140; focused `33765114501`, full `33765114492`, merge `093954b5626c034e126fde3b773cedb1add92707`.
- Stale governance PR #139 — closed unmerged and replayed on current `dev`.

## Non-blocking separate track

Rev-A custom PCB/KiCad is #85 / PR #18/#19 and does not block the Waveshare firmware release unless the Product Owner explicitly couples milestones.
