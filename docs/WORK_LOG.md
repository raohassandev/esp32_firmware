# Work log

**One row per task. I update Status, Flashed and Evidence. You write in
"Owner verdict" and "Owner comment" — I do not touch those two columns.**

Source of the requirements: `docs/PVDG_CONTROL_SCIENCE.md`.

Verdict values: `OK` / `NOT OK` / `PARTIAL`. Anything not `OK` comes back to the
top of my queue.

Firmware after each item is flashed to the board over COM5 before I mark it
done, so what you check is what is running.

---

## Queue

| # | Task | Status | Flashed | Evidence | Owner verdict | Owner comment |
|---|---|---|---|---|---|---|
| 1 | Commissioning step: grid policy + control basis | **done** | 01-08 | Step 4 of 8, "Plant control". `web/commissioning-release-v3.js` `plantControl()`; mounts the existing workspace from `web/solar-grid.js` — one form, not a copy. Board serves it: `labels` now has 8 steps. | | |
| 2 | Commissioning step: generator limits | **done with 1** | 01-08 | Same mounted workspace. Confirmed present in the bundle the board serves: "Rated power (kW)", "Minimum loading (% of rating)", "Spinning reserve (kW)", "Reverse-power margin (kW)", per-engine cards. No separate step built — it would have been a second copy. | | |
| 3 | Commissioning step: source detection (1 meter tariff / 2 meter) | **done** | 01-08 | Step 6 of 9. `sourceStep()` in `web/commissioning-release-v3.js`; mounts the panels from `web/source-detection.js` — same builders, no copy. Board serves 9 step labels with "Source detection" sixth. | | |
| 4 | Commissioning step: PV ramp per source | **done with 1** | 01-08 | Ramp editor is in the workspace step 4 mounts (`rampFields`, per-source profiles, live advisory). Confirmed in the bundle the board serves. | | |
| 5 | Commissioning: show WHY the gate is not met | **done** | 01-08 | The wizard's spine is now the controller's own nine prerequisites, read live from `/api/commissioning/gate` and shown on EVERY step, not just Review. Plus the runbook's risk ladder on each step header: READ ONLY / CONFIGURATION / WRITES TO PLANT. | | |
| 6 | Inverter comms fail-safe ordering (controller 2 min > inverter 1 min) | **done** | 01-08 | Schema 8: `inverter_config_t.comms_failsafe_ms`, 0 = not stated. Validator refuses a stated value >= the controller's grace. Both published on `/api/inverters/config`. Migration keeps commissioned NVS. | | |
| 7 | Periodic setpoint refresh: on comms restore | **done** | 01-08 | `inverter_manager_fleet_rejoins()` counts rejoins; the control loop forces one write when it changes. NOTE: the periodic keepalive already existed at **2 s**, not 30 min — see Notes. | | |
| 8 | Control-evidence: error kW, generator-safe PV ceiling | **done** | 01-08 | Added to the runtime gate panel in `web/solar-grid.js`. Both were published and reached no screen, so "why is PV held down" had no answer. min/max/average are not published by the API — not invented. | | |
| 9 | Automatic control arm/disarm in the UI | **blocked — needs your decision** | | Three separate contracts forbid it: the Solar-Grid page, the readiness page and the commissioning wizard. See Notes. | | |
| 10 | Audit log page — who changed what | todo | | | | |
| 11 | Service page: heap, PSRAM, partitions, firmware version | todo | | | | |
| 12 | Alarm detail: shelf expiry, out-of-service reason and actor, delays | todo | | | | |
| 13 | Urgent ramp 25% / 2x — currently hardcoded, make it commissioned | todo | | | | |
| 14 | Verify every page in a real browser on the board | **done** | 01-08 | `tools/browser_check.js` — Playwright/Chromium against the live board. Walks every route read FROM the shell, screenshots each, fails on any console error, and asserts the source attribution on the rendered screen. All 10 routes clean. | | |

## Blocked on you

| # | Task | What I need |
|---|---|---|
| B1 | Minimum loading default | Should commissioning propose 30%, or keep requiring an explicit value? Today it is 0 = fail-closed |
| B2 | Urgent ramp 25% / 2x | Commissioned values, or fixed constants? (drives task 13) |
| B3 | Offline debounce 2 min | Confirm, and confirm controller must always exceed the inverter's own fail-safe (drives task 6) |
| B4 | Periodic refresh interval | Confirm ~30 min (drives task 7) |
| B5 | Modbus efficiency | Serve the control read from the 72-register block instead of 4 separate reads per cycle. Cuts 4–5 transactions to 1. Touches the control input, so I will not do it without your go-ahead |
| B6 | Huawei bench qualification | The only path from LAB_ONLY to PRODUCTION for inverter writes |

I proceed on tasks 1–14 without waiting. Where a blocked answer changes a
default, I implement the mechanism and leave the value fail-closed.

---

## Notes

**Task 9 — arming is forbidden in the UI, deliberately, in three places.** I
tried each and each is held by its own contract:

- `solar_grid_control_source_contract.py` — the Solar-Grid page must not call
  `/api/control`. Every save there forces control off; an arm button beside it
  would let a setting be changed and re-armed without leaving the page.
- `prelab_readiness_source_contract.py` — the readiness workspace must remain
  read-only, so a controller can be inspected with no risk of changing it.
- `commissioning_release_v3_source_contract.py` — the wizard must not call
  `/api/control`.

Together these say the product has decided arming is an out-of-band action, not
a button. I reverted all three attempts rather than lift a safety contract to
satisfy a todo I wrote. **Your call:** should any of them be lifted, and if so
which page should own arming?

**Task 7 — the periodic half already existed, at a very different interval.**
The owner's rule was "one write every ~30 minutes, and one on communication
restore". The keepalive was already there at **2 seconds**, because a commanded
limit can EXPIRE — the Huawei SmartLogger's schedule-validity register drops it
after a configured time. I did not change 2 s to 30 min: there is a documented
reason for the short interval and changing it would need the SmartLogger
validity period to be read first. What was genuinely missing is now added: a
machine that rejoins the fleet is still holding the setpoint it had before its
link dropped, and nothing rewrote it while the target was steady.


**Commissioning had no journey — accepted.** Tasks 1 and 3 mounted existing
panels into new steps and called it done. That was component dumping, not a
commissioning flow, and the owner was right to say so.

`docs/SITE_COMMISSIONING_RUNBOOK.md` already contains the real procedure, and its
spine is section 9: nine named prerequisites, evaluated in order, fail-closed,
each with the reason it is not met. The wizard now reads them live from
`/api/commissioning/gate` and keeps them on screen the whole way through, with
the single next thing to do stated as a sentence — `first_unmet` is exactly what
the control engine publishes as its own inhibit reason.

Read from the firmware, never restated: a hardcoded list of nine here would drift
from the gate the first time one was added or renamed.

The runbook also tags every section [RO], [CFG] or [WRITE], and that order is a
safety property — identify before configuring, configure before writing. Each
wizard step now declares which it is. Still missing from the wizard, and recorded
rather than glossed: recording the plant inventory, proving the addressing
convention empirically, the first controlled write with settle-time measurement,
and abort/rollback.

**A defect the owner found that I should have: the source panel never
refreshed.** It rendered once when the tab opened and then never again, so a
plant that changed over from grid to generator went on showing the old source
until someone reloaded. On the one screen whose whole job is to say which supply
is live. Now polls every 4 s, stops when its host leaves the document or the tab
is hidden.

**And the audit that should have come first.** Every live module was checked at
once for two things: does it refresh, and does it name the source without asking
the controller. That found the remaining sites in one pass instead of the owner
finding them one at a time.


Anything I find while working that you should know goes here, newest first.

**Task 3 — where source detection was hiding.** It was implemented, tested and
given a full UI, as a TAB inside the EM-500 analyser workspace. To reach it an
engineer had to open the meter page, find the analyser, and know a tab called
"Source detection" existed. Commissioning never asked — on a plant where this
decides whether reverse-power protection applies at all. Same treatment as task
1: the panels were already detached nodes, so commissioning hosts the same ones.

**Task 1 — why it is a mount and not a new form.** Every control for the grid
policy, the generator limits and the ramps already existed in `web/solar-grid.js`,
with its own validation and its own fail-closed rules. Building a second copy
inside commissioning would have duplicated those safety rules, and the two would
drift the first time one was corrected. So `solar-grid.js` now builds its
workspace unattached and either page can host it. Anything commissioned in the
new step is commissioned on the Control page and the other way round.

**Task 1 — no blocker on this step, deliberately.** Whether a grid policy is
*required* depends on the plant: a generator-only site commissions none. Refusing
to continue would reject a legitimate configuration. What is genuinely required is
stated by the controller itself at `/api/commissioning/gate`, and that is task 5.

