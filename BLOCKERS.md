# AISH-OS Blocker Ledger v2

Master program: #79. Live evidence overrides stale snapshots. Current software integration baseline: `ad651806edb95a749b7d65b61fe1f6b2cf2148db` after PR #152.

## B-001 — Waveshare final continuous acceptance

**Lane:** L3 / #87 / #27  
**Candidate:** `87841ecee727fe1d814d4186be8c8c26e4afafb4`  
**State:** SHORT PASS / FINAL CONTINUOUS SOAK INCOMPLETE

Short display/touch/Alarms acceptance passed with healthy DMA/resources and zero recorded WDT/panic/NO_MEM/unexpected reset. The first continuous same-image soak produced about 2 h / 121 one-minute samples and 25 clean backend rounds, then the entire USB dock/power path disappeared. The required single uninterrupted >=4 h / >=240-sample run has not been achieved. Partial runs may not be combined; one new uninterrupted run is required.

PR #142 merged generic final-evidence parsing and immutable package validation to current `dev`; it does not change the physical verdict or candidate. PR #57 and parent PR #20 remain unpromoted.

## B-002 — Waveshare backend parity and persistence/ARM

**Issues:** #25/#26 / parent #87  
**State:** PHYSICAL PENDING AFTER B-001  
**Tooling:** PR #150 merged as `184d7e658ac44496a4f9efe0fd5db5844ad7fa43`

After final soak PASS, complete HMI/Core/backend parity/recovery and save->readback->reboot/restore/interrupted-save/ARM matrices on the exact accepted source/image. PR #150 validators fail closed on wrong identity, missing final-soak prerequisite, insufficient observation, erase use, incomplete operations or threshold weakening; a validator PASS is not the hardware PASS.

## B-003 — Generator source-transition production merge

**Lane:** L2 / #80  
**Draft PR:** #106, exact head `a1620789235d21b515f9f245f2329fab88b50558`  
**State:** SOFTWARE GREEN / EXTERNAL BENCH BLOCKED  
**Tooling:** PR #151 merged as `892a5811160098a765df7895af943eadf0457d48`

Physical Grid<->Generator/Transfer/Island/Sync/conflict/stale/source-loss behavior, actual run/breaker/ATS evidence and meter sign/scaling must pass before merge. PR #151 records the required evidence but does not create it. Since `dev` has advanced, after physical PASS replay only the identical validated runtime slice onto current `dev` and obtain fresh exact-head CI.

## B-004 — Modbus/network physical endurance

**Lane:** L7 / #83  
**Software:** connection modes/deadlines complete via PR #99/#114  
**Physical:** NOT QUALIFIED

Remaining work is physical endurance: PCB/TIME_WAIT/socket/resource trends, healthy/slow/dead peers, Modbus exceptions, TCP/gateway resets, reconnect, Wi-Fi recovery and simultaneous multi-device load at commissioned rates.

## B-005 — Production inverter profiles

**Lane:** L6 / #82  
**State:** BLOCKED EXTERNAL OFFICIAL MANUALS + BENCH

Generic engine safety is merged through #119. PR #112 prevents a production release from passing with zero actual production-approved real profiles, and PR #113 keeps pending manufacturer connection transport explicitly unqualified. Every production model still requires exact official manual/model/firmware identity, physical identity/telemetry/status proof, write/readback/rollback evidence and signed approval.

Current official-source research confirms GoodWe GW100K-HT uses Modbus-RTU/SunSpec-compatible communications but does not provide the required exact HT production control map. Huawei's official portal confirms the SUN2000-115KTL-M2 family, but an exact applicable official Modbus interface/control definition remains required. No production profile is promoted from incomplete documentation.

## B-006 — Real site source evidence

**Lane:** L5 / #81  
**State:** BLOCKED PARTIAL EXTERNAL EVIDENCE

Actual breaker/run/ATS/synchronism provenance, addresses/contacts, masks, active polarity, meter role/sign/scaling and topology are required. No kW-sign heuristic may manufacture breaker/sync/source authority.

## B-007 — Secure OTA physical release qualification

**Lane:** L4 / #86/#50  
**Software:** COMPLETE/MERGED via PR #145; always-on regression via PR #148  
**Tooling:** PR #152 merged as `ad651806edb95a749b7d65b61fe1f6b2cf2148db`  
**State:** SOFTWARE CLOSED / PHYSICAL BLOCKED BY WAVESHARE SOURCE GRAPH + REAL OTA MATRIX

Current `dev` includes rollback-safe OTA software, always-on OTA regression contracts and the fail-closed real-controller evidence validator from PR #152. This does not transfer physical qualification from the frozen Waveshare candidate. After L3/source-graph resolution, identify one exact intended OTA-capable release artifact and physically prove authenticated upload, invalid-image rejection before write, interrupted upload, power loss, partial-image non-selection, previous-slot boot, pending verification, mark-valid and deliberate rollback while fail-closed and without NVS/full-flash erase.

## B-008 — Integrated FAT/SAT

**Lane:** L7 / #83  
**State:** WAITING ON PHYSICAL/SITE/PROFILE QUALIFICATION

Software CI cannot close Grid/DG/mixed-source FAT, communication-loss endurance, physical fail-closed transitions or signed SAT. Evidence must be tied to exact firmware SHA, config, source mapping, approved profile and artifact identities.

## Resolved software/tooling blockers — do not reopen from stale audits without a live regression

- All previously reconciled software blockers through PR #149 remain resolved according to their exact merge identities.
- Waveshare post-soak evidence structure/consistency tooling absent from live `dev` — resolved by PR #150; physical #25/#26 remain open.
- Generator transition physical evidence structure/consistency tooling absent from live `dev` — resolved by PR #151; physical #80 remains open.
- Secure OTA real-controller evidence structure/consistency tooling absent from live `dev` — resolved by PR #152; physical #86 remains open.

## Non-blocking separate track

Rev-A custom PCB/KiCad is #85 / PR #18/#19 and does not block the Waveshare firmware release unless the Product Owner explicitly couples milestones.
