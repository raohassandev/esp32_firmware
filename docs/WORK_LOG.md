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
| 4 | Commissioning step: PV ramp per source | todo | | | | |
| 5 | Commissioning: show WHY the gate is not met | todo | | | | |
| 6 | Inverter comms fail-safe ordering (controller 2 min > inverter 1 min) — not implemented at all | todo | | | | |
| 7 | Periodic setpoint refresh: on comms restore + every ~30 min — not implemented | todo | | | | |
| 8 | Control-evidence page: grid min/max/average, error kW, safe PV, gate state | todo | | | | |
| 9 | Automatic control arm/disarm in the UI | todo | | | | |
| 10 | Audit log page — who changed what | todo | | | | |
| 11 | Service page: heap, PSRAM, partitions, firmware version | todo | | | | |
| 12 | Alarm detail: shelf expiry, out-of-service reason and actor, delays | todo | | | | |
| 13 | Urgent ramp 25% / 2x — currently hardcoded, make it commissioned | todo | | | | |
| 14 | Verify every page in a real browser on the board | todo | | | | |

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

