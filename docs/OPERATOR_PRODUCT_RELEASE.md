# Automatrix PV-DG — Operator Product Release

Branch: `feature/multibrand-inverter-profiles`

Implementation head: `e39ef8aa417834619c849d393efd9c26dcd13c4c`

## Completed product stages

### 1. Controller-resident operational history

- 5-second grid/solar/control/alarm samples for the recent 15-minute view.
- One-minute samples retained for up to 24 hours.
- Bounded RAM rings to protect memory and flash endurance.
- 15-minute, 1-hour and 24-hour ranges.
- Minimum, average and peak grid and solar values.
- Public sanitized endpoint: `GET /api/operator/history`.

### 2. Alarm and event center

- Controller-start event.
- Network loss/recovery.
- Grid-meter loss/recovery.
- Meter-offline and stale-data alarms.
- Solar-fleet availability changes.
- Automatic-control enabled/disabled events.
- Critical, warning and information severity.
- Plain-language cause and recommended action.
- Active and cleared states.
- Bounded controller-resident event history.
- Public sanitized endpoint: `GET /api/operator/events`.

### 3. Equipment drill-down

- Clickable grid-meter rows.
- Clickable solar-inverter rows.
- Operator detail modal with state, output, freshness, capacity and control eligibility.
- Engineering configuration links visible only after authentication.
- No endpoint, protocol, register or scaling detail in operator mode.

### 4. Final operator navigation

- Overview.
- Grid Power.
- Solar.
- Control.
- Alarms.
- Controller.
- Protected Engineering entry.
- Alarm-count badge.
- Mobile bottom navigation for primary operator pages.
- Protected Commissioning route visible only in Engineering mode.

### 5. Industrial operating modes

- Persistent comfortable/compact density selection.
- Kiosk/full-screen plant display mode.
- Light and dark theme compatibility.
- Industrial tablet touch targets.
- Mobile bottom-sheet equipment details.
- Responsive desktop, tablet and mobile layouts.
- Print-safe layout.

### 6. Guided commissioning workflow

- Protected Engineering-only commissioning page.
- Ordered steps: network, grid meter, inverter fleet, read-only verification, safety readiness and handover report.
- Live completion status derived from controller APIs.
- Progress indicator.
- Direct navigation to each engineering task.
- JSON commissioning report export with sanitized site status and test results.
- Explicit production-control safety warning.

## Safety classification

The operator product workflow is implemented, but physical automatic inverter control remains locked until:

1. Exact manufacturer manuals are verified.
2. Model-specific telemetry and command maps are extracted.
3. Bench read qualification passes.
4. Conservative command/readback tests pass.
5. Rollback and mismatch behavior is physically proven.
6. Each physical profile receives explicit production approval.

## Field validation TODO

- [ ] Flash the final CI-green head.
- [ ] Confirm operator navigation contains no engineering pages.
- [ ] Verify 15-minute, 1-hour and 24-hour history ranges.
- [ ] Trigger and clear a meter communication alarm.
- [ ] Verify alarm count, severity, cause and recommended action.
- [ ] Open grid-meter and inverter detail modals.
- [ ] Verify compact and comfortable density modes.
- [ ] Verify kiosk/full-screen behavior.
- [ ] Verify mobile bottom navigation on a phone/tablet.
- [ ] Log into Engineering and complete the commissioning wizard.
- [ ] Export and review the commissioning JSON report.
- [ ] Run a 30-minute stability soak.
- [ ] Keep automatic control disabled until physical profile approval.
