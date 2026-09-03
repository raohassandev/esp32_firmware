# AISH-OS Blocker Ledger v2

Master program: #79. Live evidence overrides stale snapshots. Current software integration baseline: `d07dca2d2b20a2cf4e712df45fae9dfe7e3024c2` after PR #142.

## B-001 — Waveshare final continuous acceptance

**Lane:** L3 / #87 / #27  
**Candidate:** `87841ecee727fe1d814d4186be8c8c26e4afafb4`  
**State:** SHORT PASS / FINAL CONTINUOUS SOAK INCOMPLETE

Short display/touch/Alarms acceptance passed with healthy DMA/resources and zero recorded WDT/panic/NO_MEM/unexpected reset. The first continuous same-image soak produced about 2 h / 121 one-minute samples and 25 clean backend rounds, then the entire USB dock/power path disappeared. The required single uninterrupted >=4 h / >=240-sample run has not been achieved. Partial runs may not be combined; one new uninterrupted run is required.

PR #142 merged generic final-evidence parsing and immutable package validation to current `dev`; it does not change the physical verdict or candidate. PR #57 and parent PR #20 remain unpromoted; obsolete #46 stays closed. Historical PR #67 remains source-coupled to the frozen Waveshare graph.

## B-002 — Waveshare backend parity and persistence/ARM

**Issues:** #25/#26 / parent #87  
**State:** PHYSICAL PENDING AFTER B-001

After final soak PASS, complete HMI/Core/backend parity/recovery and save->readback->reboot/failure/ARM matrices on the exact accepted source/image before promotion.

## B-003 — Generator source-transition production merge

**Lane:** L2 / #80  
**Draft PR:** #106, exact head `a1620789235d21b515f9f245f2329fab88b50558`  
**State:** SOFTWARE GREEN / EXTERNAL BENCH BLOCKED

Physical Grid<->Generator/Transfer/Island/Sync/conflict/stale/source-loss behavior, actual run/breaker/ATS evidence and meter sign/scaling must pass before merge. Since `dev` has advanced, after physical PASS replay only the identical validated runtime slice onto current `dev` and obtain fresh exact-head CI.

## B-004 — Modbus/network physical endurance

**Lane:** L7 / #83  
**Software:** connection modes/deadlines complete via PR #99/#114  
**Physical:** NOT QUALIFIED

Remaining work is physical endurance: PCB/TIME_WAIT/socket/resource trends, healthy/slow/dead peers, Modbus exceptions, TCP/gateway resets, reconnect, Wi-Fi recovery and simultaneous multi-device load at commissioned rates.

## B-005 — Production inverter profiles

**Lane:** L6 / #82  
**State:** BLOCKED EXTERNAL OFFICIAL MANUALS + BENCH

Generic engine safety is merged through #119. PR #112 also prevents a production release from passing with zero actual production-approved real profiles, and PR #113 keeps pending manufacturer connection transport explicitly unqualified. Every production model still requires exact official manual/model/firmware identity, physical identity/telemetry/status proof, write/readback/rollback evidence and signed approval.

Official-source research currently confirms GoodWe GW100K-HT uses Modbus-RTU/SunSpec-compatible communications but does not provide the required exact HT production control map; the publicly available Huawei SUN2000MB Modbus definition is a different family and is not accepted as SUN2000-115KTL-M2 register evidence. No profile is promoted from incomplete documentation.

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
- Production release accepting zero real approved inverter profiles — PR #112.
- Pending manufacturer profile claiming an unproven connection transport — PR #113.
- Unbounded synchronous DNS endpoint path — PR #114.
- Profile assignment persisted before control disable — PR #117.
- Positive production write without complete live evidence/status authority — PR #119.
- Legacy schema migration allocation failure replacing commissioned NVS — PR #122.
- Safety alarm reader transiently observing a false all-clear during curtailment — PR #124.
- Generic config import / meter mapping / inverter mapping / profile assignment leaving live command authority active during persistence — PR #127.
- Wi-Fi transport configuration leaving live command authority active during persistence — PR #129.
- Source-detection topology/register/threshold changes leaving live command authority active during persistence — PR #130.
- Missing single regression inventory for current safety-relevant commissioning mutation surfaces — PR #131.
- Wi-Fi driver scan-result lifetime leak on failed/empty scan paths — PR #133.
- Schema-6 wrapper parsing JSON ahead of the established depth gate — PR #135.
- Whole `app_config_t` snapshot on the inverter-profile HTTP-task path — PR #136.
- Whole schema-6 init/import/export config snapshots on main/HTTP task stacks — PR #137.
- Runtime-component automatic `app_config_t` stack-frame regression gap — PR #138.
- Project-wide app-config guard missing `main/` ownership and declaration escape forms — PR #140; focused `33765114501`, full `33765114492`, merge `093954b5626c034e126fde3b773cedb1add92707`.
- Stale governance PR #139 — closed unmerged after #140 advanced `dev`.
- Governance reconciliation through PR #140 — PR #141; full `33766240186`, merge `2272caefa87581f27e815ce4420a5880d2d16e38`.
- Generic Waveshare final-evidence/package validation tooling absent from live `dev` — PR #142; focused `33768630723`, full `33768630667`, merge `d07dca2d2b20a2cf4e712df45fae9dfe7e3024c2`. Tooling cannot substitute for physical evidence.

## Non-blocking separate track

Rev-A custom PCB/KiCad is #85 / PR #18/#19 and does not block the Waveshare firmware release unless the Product Owner explicitly couples milestones.
