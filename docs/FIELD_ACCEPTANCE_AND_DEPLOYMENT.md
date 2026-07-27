# Automatrix PV-DG Controller — Field Acceptance and Deployment

Release candidate branch: `feature/multibrand-inverter-profiles`

Validated head: `027dac1189aa6820d592bae42879fbd601e513c3`

## Release status

- Browser/product suite: PASS
- SolTrix simulator suite: PASS
- Operator/Engineering access boundary: PASS
- ESP-IDF v6.0.1 build: PASS
- Compiler warnings: 0
- Physical automatic inverter control: LOCKED
- Field classification: OPERATOR PRODUCT + READ-ONLY COMMISSIONING RELEASE CANDIDATE

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

Expected SHA:

```text
027dac1189aa6820d592bae42879fbd601e513c3
```

Do not run `erase-flash`, `erase_flash`, or any command that clears NVS.

## Acceptance sequence

### 1. Boot health

- [ ] Capture the complete boot log.
- [ ] Confirm no panic, Guru Meditation, watchdog, abort, stack overflow, or reboot loop.
- [ ] Confirm the controller obtains its expected network address.
- [ ] Record the temporary Engineering password from the serial log.

### 2. Operator product UI

- [ ] Overview shows grid exchange, solar status, control state, and plant attention.
- [ ] Grid Power shows a realistic current kW value and correct import/export direction.
- [ ] Solar shows installed capacity, live production where available, and fleet availability.
- [ ] Control clearly reports monitoring-only, available, blocked, or active state.
- [ ] Alarms shows active/cleared conditions with plain-language actions.
- [ ] Controller shows product, connection, and service state.
- [ ] No register, scale, endpoint, function-code, raw-word, or profile detail is visible to operators.

### 3. Responsive product checks

- [ ] Desktop layout at 1366×768 or larger.
- [ ] Industrial tablet layout around 1024×600 or 1280×800.
- [ ] Mobile layout around 390×844.
- [ ] Mobile bottom navigation remains usable.
- [ ] Equipment detail modal opens and closes correctly.
- [ ] Comfortable and Compact density modes work.
- [ ] Light and dark themes remain readable.
- [ ] Kiosk/full-screen mode enters and exits correctly.

### 4. History and event checks

- [ ] Five-second history samples accumulate.
- [ ] 15-minute view renders grid and solar trends.
- [ ] 1-hour view renders after sufficient runtime.
- [ ] Minimum, average, and peak summaries are plausible.
- [ ] Disconnect and reconnect a non-critical test communication path and verify events.
- [ ] Alarm badge updates correctly.
- [ ] Active and cleared states remain distinguishable.

### 5. Engineering access

- [ ] Operator cannot access Engineering or Commissioning pages without authentication.
- [ ] Temporary password login succeeds.
- [ ] Permanent password is set.
- [ ] Logout removes Engineering access.
- [ ] Session timeout returns to operator mode.
- [ ] Technical meter and inverter configuration is visible only after login.

### 6. Guided commissioning

- [ ] Network step reflects the actual connection.
- [ ] Grid meter step reports a fresh online measurement.
- [ ] Inverter step reflects configured rated capacity and availability.
- [ ] Read-only verification uses zero-write probes only.
- [ ] Safety readiness keeps automatic control locked unless every physical qualification gate passes.
- [ ] Export the sanitized commissioning report and retain it with site records.

### 7. Stability soak

- [ ] Run continuously for at least 30 minutes.
- [ ] No spontaneous restart.
- [ ] Web pages remain responsive.
- [ ] Meter values remain consistent across Overview and Grid Power.
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

## Production-write gate

Simulator-only profiles must never be selected for physical equipment. Real inverter writes remain unavailable until exact manufacturer manuals, bench tests, physical readback, rollback behavior, and explicit production approval are completed for each exact model family.
