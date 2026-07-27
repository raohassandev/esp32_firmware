# Automatrix PV-DG Controller — Field Acceptance and Deployment

Release candidate branch: `feature/multibrand-inverter-profiles`

Validated head: use the latest branch commit only after both **web** and **build** jobs pass in `Firmware and web checks`.

## Release status

- Browser/product suite: automated CI gate
- Pre-Lab Readiness suite: automated CI gate
- SolTrix simulator suite: automated CI gate
- Operator/Engineering access boundary: automated CI gate
- ESP-IDF v6.0.1 build: automated CI gate
- Compiler warnings: zero-warning gate
- Physical automatic inverter control: LOCKED
- Field classification: DEVELOPMENT FIELD-TEST CANDIDATE

## Current development conveniences

The current development branch intentionally contains:

- Primary Wi-Fi: `Rao`
- Development Wi-Fi provisioning generation: `1`
- Engineering development auto-unlock: enabled
- Recovery SSID: `Automatrix-PVDG-Setup`

These settings reduce lab setup time. They are not production security and must be removed before resale.

## Safe pull and flash

Run from the local repository in an ESP-IDF PowerShell environment:

```powershell
cd D:\Working\esp32_firmware

git fetch origin
git switch feature/multibrand-inverter-profiles
git pull --ff-only origin feature/multibrand-inverter-profiles
git rev-parse HEAD

& "C:\Espressif\frameworks\esp-idf-v6.0.1\export.ps1"

idf.py -B build-idf601 set-target esp32s3
idf.py -B build-idf601 build
idf.py -B build-idf601 -p COM5 flash
idf.py -B build-idf601 -p COM5 monitor
```

Confirm the displayed SHA has a successful web and build workflow before flashing.

Do not run `erase-flash`, `erase_flash`, or any command that clears NVS.

## Acceptance sequence

### 1. Boot and network health

- [ ] Capture the complete boot log.
- [ ] Confirm no panic, Guru Meditation, watchdog, abort, stack overflow, or reboot loop.
- [ ] Confirm the controller applies or retains the intended development Wi-Fi profile.
- [ ] Confirm connection to `Rao`, or confirm the recovery AP activates when the primary network is unavailable.
- [ ] Record controller IP, RSSI, reconnect count and boot reason.

### 2. Pre-Lab Readiness

Open **Readiness** before connecting or enabling any physical command path.

- [ ] Controller API check passes.
- [ ] Network check matches the actual primary/recovery state.
- [ ] Meter check reports the expected uncommissioned, warning or online state.
- [ ] Solar-fleet check matches enabled equipment.
- [ ] History begins collecting samples.
- [ ] Active alarm count is explainable.
- [ ] Automatic control reports locked/monitoring-only.
- [ ] No unexpected commandable physical inverter capacity is exposed.
- [ ] Development auto-unlock and provisioning warnings are visible.
- [ ] Export and retain the pre-lab readiness snapshot.

### 3. Operator product UI

- [ ] Overview shows grid exchange, solar status, control state, and plant attention.
- [ ] Grid Power shows a realistic current kW value and correct import/export direction.
- [ ] Solar shows installed capacity, live production where available, and fleet availability.
- [ ] Control clearly reports monitoring-only, available, blocked, or active state.
- [ ] Alarms shows active/cleared conditions with plain-language actions.
- [ ] Controller shows product, connection, and service state.
- [ ] No register, scale, endpoint, function-code, raw-word, or profile detail is visible to operators.

### 4. Responsive product checks

- [ ] Desktop layout at 1366×768 or larger.
- [ ] Industrial tablet layout around 1024×600 or 1280×800.
- [ ] Mobile layout around 390×844.
- [ ] Mobile bottom navigation remains usable.
- [ ] Equipment detail modal opens and closes correctly.
- [ ] Comfortable and Compact density modes work.
- [ ] Light and dark themes remain readable.
- [ ] Kiosk/full-screen mode enters and exits correctly.

### 5. History and event checks

- [ ] Five-second history samples accumulate.
- [ ] 15-minute view renders grid and solar trends.
- [ ] 1-hour view renders after sufficient runtime.
- [ ] Minimum, average, and peak summaries are plausible.
- [ ] Disconnect and reconnect a non-critical test communication path and verify events.
- [ ] Alarm badge updates correctly.
- [ ] Active and cleared states remain distinguishable.

### 6. Engineering development access

Development auto-unlock is intentionally enabled on this branch.

- [ ] Engineering becomes available automatically.
- [ ] Technical pages remain visually distinct from operator pages.
- [ ] Logout/session behavior does not expose stale technical content in operator mode.
- [ ] Technical meter and inverter configuration is shown only in Engineering context.
- [ ] Verify the session API reports `development_auto_unlock: true`.

The production candidate must repeat these tests with auto-unlock disabled and unique-password authentication enabled.

### 7. Guided commissioning

- [ ] Network step reflects the actual connection.
- [ ] Grid meter step reports a fresh online measurement.
- [ ] Inverter step reflects configured rated capacity and availability.
- [ ] Read-only verification uses zero-write probes only.
- [ ] Safety readiness keeps automatic control locked unless every physical qualification gate passes.
- [ ] Export the sanitized commissioning report and retain it with site records.

### 8. Stability soak

- [ ] Run continuously for at least 30 minutes.
- [ ] No spontaneous restart.
- [ ] Web pages remain responsive.
- [ ] Meter values remain consistent across Overview, Grid Power and Readiness.
- [ ] History continues accumulating.
- [ ] Alarm/event count does not grow continuously without a real state change.
- [ ] Free heap, socket use, and Modbus errors do not show uncontrolled degradation in the serial log.

## Release blockers

Do not approve automatic control if any of these remain unresolved:

- Physical inverter model/manual mismatch
- Unknown command or readback register
- Identity mismatch
- Stale or unstable meter feedback
- Command/readback mismatch
- Unverified ramp or command interval
- Repeated controller reset
- Incorrect grid-power sign or scale
- Development auto-unlock still enabled in a production candidate
- Development Wi-Fi credentials still compiled into a production candidate

## Production-write gate

Simulator-only profiles must never be selected for physical equipment. Real inverter writes remain unavailable until exact manufacturer manuals, bench tests, physical readback, rollback behavior, and explicit production approval are completed for each exact model family.
