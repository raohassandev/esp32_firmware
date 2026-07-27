# Codex Local Execution Roadmap

## Purpose

This file is the authoritative local-execution roadmap for the Automatrix ESP32 PV-DG Controller.

Codex is responsible for local repository inspection, builds, tests, flashing, serial monitoring, browser checks, API qualification, physical evidence capture, narrowly scoped local fixes, and updating the qualification record.

ChatGPT remains responsible for architecture, safety decisions, release decisions, GitHub review, merges, and final shipment approval.

Codex must read this file from the repository at the start of every session and resume from the first incomplete gate. Do not rely on chat memory when repository evidence is available.

---

## Repository and release baseline

Repository:

`raohassandev/esp32_firmware`

Expected local branch:

`feature/pvdg-batch3-consolidated`

Current shipped commissioning merge commit:

`fc5cbf59196f016226bbe1584c718fd32bfeb1b2`

Controller hardware:

- ESP32-S3 DevKitC-1 N16R8
- 16 MB flash
- 8 MB PSRAM
- Expected serial port: `COM5`
- Confirmed controller IP during pre-flash discovery: `192.168.0.102`
- Confirmed controller identity: `automatrix-pvdg`
- Confirmed web title: `Automatrix PV-DG Controller`

Known local network during qualification:

- Host IP: `192.168.0.100/24`
- Gateway: `192.168.0.1`
- Wi-Fi SSID: `Tenda_69B540`
- Meter gateway: `192.168.0.200:502`
- Known Modbus slave IDs: `1`, `2`, `3`
- Expected meter mix: two EM500-family meters and one Carlo Gavazzi WM15

---

## Responsibility boundary

Codex may:

- Inspect the local repository and working tree
- Fetch and fast-forward the approved branch
- Run tests and builds
- Install official Espressif ESP-IDF tooling
- Identify and safely release a confirmed stale serial-port process
- Read serial logs
- Send read-only HTTP requests
- Back up device configuration and runtime data
- Flash an approved build without erasing NVS
- Run local browser and API qualification
- Run read-only meter and network soak tests
- Create evidence files and qualification reports
- Make small, directly justified local fixes on the current approved branch when a failed test identifies a concrete software defect
- Commit local fixes only when the task explicitly authorizes it

Codex must not independently:

- Merge or close pull requests
- Push unreviewed changes
- Rewrite branch history
- Force-reset or clean the working tree
- Delete or overwrite unrelated local work
- Touch SolTrix or another repository
- Erase NVS or perform a full-chip erase
- Enable automatic PV-DG control
- Enable live inverter commands
- Enable physical CT/PT, wiring, tariff, or meter setup writes
- Expose destructive meter commands
- Treat register `0x2160` as sufficient source proof by itself
- Change production Wi-Fi, meter, inverter, or controller configuration without a specific approved test step
- Weaken acceptance criteria to obtain a pass
- Claim success without saved evidence

When safety or scope is ambiguous, stop at the safe boundary, record the blocker in the repository report, and leave the hardware in its prior state.

---

## Non-negotiable safety state

Until a later release explicitly changes these requirements:

- `control.enabled` must remain `false`
- Inverter channels must remain disabled or non-commandable
- Meter settings Apply must remain locked
- All CT/PT/wiring/tariff operations are preview-only
- No read-only diagnostic endpoint may mutate configuration
- No diagnostics operation may generate an inverter command
- No unsupported register may be returned as a valid-looking zero
- Password registers must remain masked
- Reset-energy, restore-defaults, reboot, and other destructive meter commands must not be exposed in the normal operator interface

Register `0x2160` / decimal `8544` has a site-observed mapping:

- `0 = grid`
- `1 = generator`

It is only one item of source evidence. Future source detection must also validate voltage, frequency, current, active power, freshness, stability, and conflict conditions.

---

## Current completed software scope

The consolidated branch contains:

- Transactional multi-meter configuration for up to four profiles
- Duplicate endpoint protection
- Automatic control forced disabled after meter-profile changes
- EM500 instantaneous measurement API
- EM500 energy and hour-counter API
- Maximum, minimum, average, and maximum-demand history
- Complete read-only M01-M18 settings catalogue
- Password masking
- Four-register U64 energy decoding
- CT/PT/wiring/tariff change-plan preview with zero physical writes
- Embedded operator workspace for:
  - Live measurements
  - Energy
  - History
  - Settings M01-M18
  - Meter profiles
  - CT/PT/tariff planning
- Correct modular asset delivery through `/app.css` and `/app.js`
- Browser asset-delivery regression contract
- Grid/generator control design specification
- Physical qualification checklist

The EM500 JavaScript modules must be served in this order:

1. `em500-utils.js`
2. `em500-core.js`
3. `em500-profiles.js`
4. `em500-plan.js`

---

## Evidence layout

Use this local structure:

```text
build/
evidence/YYYY-MM-DD/
  preflash/
  build-idf601/
  postflash/
  browser/
  api/
  meters/
  source-input/
  soak/
  wifi-reconnect/
docs/PHYSICAL_QUALIFICATION_REPORT_YYYY-MM-DD.md
```

All evidence files must be timestamped or placed in a dated directory.

For every saved JSON, HTML, CSS, JavaScript, log, and firmware artifact, calculate SHA256 where practical.

Never paste secrets into the report. Preserve exact local backups, but mask credentials in summaries.

---

# Execution phases

## Phase 0 - Session bootstrap

At the beginning of every Codex session:

```powershell
git status -sb
git branch --show-current
git rev-parse HEAD
git log -1 --oneline
git remote -v
```

Then read:

```text
docs/CODEX_LOCAL_EXECUTION_ROADMAP.md
docs/PHYSICAL_QUALIFICATION_REPORT_2026-07-27.md
docs/MULTI_METER_PVDG_TODO.md
docs/PVDG_GRID_GENERATOR_CONTROL_SPEC.md
docs/WIFI_RECONNECT_ADMISSION_GATE.md
```

Identify:

- Current completed phase
- Current blocker
- Existing evidence
- Uncommitted files
- Exact next safe action

Do not repeat completed destructive or state-changing steps unless evidence shows they must be rerun.

Report the starting state in the qualification report before continuing.

---

## Phase 1 - Repository and source validation

Status: completed for commit `fc5cbf5`, but rerun after any code change.

Required checks:

```powershell
node --check web/app.js
node --check web/wifi-utils.js
node --check web/wifi-guard.js
node --check web/wifi.js
node --check web/devices-utils.js
node --check web/devices.js
node --check web/devices-refresh.js
node --check web/em500-utils.js
node --check web/em500-core.js
node --check web/em500-profiles.js
node --check web/em500-plan.js

node web/tests/wifi-utils.test.js
node web/tests/devices-utils.test.js
node web/tests/em500-utils.test.js

python tests/telemetry_source_contract.py
python tests/multi_meter_config_source_contract.py
python tests/modbus_u64_decoder_source_contract.py
python tests/em500_snapshot_source_contract.py
python tests/em500_history_source_contract.py
python tests/em500_settings_source_contract.py
python tests/em500_settings_plan_source_contract.py
python tests/em500_web_assets_source_contract.py
```

Use `python3` when required.

Acceptance:

- Every check passes
- No browser syntax error
- No source-contract failure
- No unreviewed code change

---

## Phase 2A - Device discovery and pre-flash backup

Status: completed except serial boot capture.

Confirmed:

- ESP32 serial port: `COM5`
- ESP32 IP: `192.168.0.102`
- Controller identity verified through USB, MAC, web title, API schema, and reported IP
- Pre-flash backups saved in `evidence/2026-07-27/preflash/`
- `control.enabled = false`
- Inverter disabled and `has_command = false`
- Recovery AP inactive

Required files include:

```text
api-status.json
api-config.json
api-meters.json
api-inverters.json
api-telemetry.json
index.html
app.css
app.js
serial.log
host-network.txt
device-discovery.json
SHA256SUMS.txt
```

Do not overwrite these files without retaining the original hash and explaining why.

---

## Phase 2B - Serial access and authoritative ESP-IDF 6.0.1 build

This is the next incomplete phase.

### 2B.1 Resolve COM5 access

Confirm the port and identify the owning process:

```powershell
Get-CimInstance Win32_SerialPort |
  Format-Table DeviceID,Name,PNPDeviceID -Auto

Get-PnpDevice -PresentOnly |
  Where-Object { $_.InstanceId -match 'VID_303A&PID_1001' } |
  Format-List Status,Class,FriendlyName,InstanceId

Get-CimInstance Win32_Process |
  Where-Object { $_.CommandLine -match 'COM5|idf.py.*monitor|esptool|serial' } |
  Select-Object ProcessId,Name,CommandLine |
  Format-List
```

Only stop a process when its command line or handle evidence proves it owns `COM5`.

Do not kill generic Python or VS Code processes blindly.

### 2B.2 Capture a real pre-flash serial boot log

Open `COM5` at the firmware baud rate, normally 115200, without flashing.

A normal board reset is allowed.

Capture one complete boot and at least 90 seconds of runtime.

Save:

```text
build/phase2b_preflash_serial.log
evidence/2026-07-27/preflash/serial-boot.log
```

Record:

- Reset reason
- Bootloader and application startup
- Wi-Fi association
- Assigned IP
- Recovery AP state
- Meter manager startup
- Inverter state
- Control state
- Modbus failures or successes
- Panic, Guru Meditation, watchdog, spontaneous reboot, or stack failures

Update checksums.

### 2B.3 Prepare official ESP-IDF 6.0.1

The release-authoritative build toolchain is ESP-IDF `v6.0.1`.

Preferred installation:

```text
C:\Espressif\frameworks\esp-idf-v6.0.1
```

Use only official Espressif sources.

Verify:

```powershell
git describe --tags --exact-match
git rev-parse HEAD
.\export.ps1
idf.py --version
```

Do not silently fall back to ESP-IDF 5.5.4.

### 2B.4 Restore the release lockfile

Preserve the 5.5.4-generated lockfile as evidence, then restore only `dependencies.lock` from HEAD:

```powershell
Copy-Item dependencies.lock build/dependencies.lock.generated-by-idf-5.5.4 -Force
git restore --source=HEAD -- dependencies.lock
git diff -- dependencies.lock
```

Expected result: no tracked lockfile diff before the 6.0.1 build.

### 2B.5 Authoritative build

Use a dedicated build directory:

```text
build-idf601
```

Run:

```powershell
idf.py -B build-idf601 set-target esp32s3
idf.py -B build-idf601 build
```

Save full build output and calculate SHA256 for:

```text
build-idf601/automatrix_pvdg.bin
build-idf601/bootloader/bootloader.bin
build-idf601/partition_table/partition-table.bin
```

Record:

- ESP-IDF version and commit
- Python version
- Compiler version
- CMake and Ninja versions
- Application size
- Bootloader size
- Partition-table size
- Application headroom
- Compiler warning count
- Firmware hashes
- `sdkconfig` target and partition table

Acceptance:

- ESP-IDF explicitly reports v6.0.1
- Build succeeds
- Target is ESP32-S3
- All three binaries exist
- Project compiler warning count is zero
- No unexplained `dependencies.lock` mutation

Stop before flashing and update the qualification report.

---

## Phase 3 - Controlled non-destructive flash

Do not enter this phase until Phase 2B passes.

Before flashing:

- Confirm pre-flash evidence hashes
- Confirm current device remains reachable
- Confirm `control.enabled = false`
- Confirm inverter disabled
- Confirm authoritative v6.0.1 binary hash
- Confirm exact serial port
- Confirm no unrelated process owns COM5
- Confirm `dependencies.lock` is clean

Flash without NVS erase:

```powershell
idf.py -B build-idf601 -p COM5 flash monitor
```

Forbidden commands:

```text
erase-flash
esptool erase_flash
full-chip erase
NVS erase
```

Capture:

- Complete flash log
- First boot
- At least 120 seconds of serial runtime
- Reset reason
- Wi-Fi reconnection
- IP address
- Recovery AP state
- Meter initialization
- Inverter disabled state
- Control disabled state
- Heap and task health
- Panic/watchdog/reboot evidence

Immediate stop conditions:

- Automatic control enabled
- Inverter write or command occurs
- NVS/configuration lost
- Reboot loop
- Panic/watchdog
- Recovery AP activates unexpectedly on a healthy network
- Controller does not become reachable

Save under:

```text
evidence/YYYY-MM-DD/postflash/
```

---

## Phase 4 - Post-flash integrity and configuration comparison

After a successful flash:

Fetch read-only endpoints:

```text
GET /
GET /app.css
GET /app.js
GET /api/status
GET /api/config
GET /api/meters
GET /api/inverters
GET /api/telemetry
```

Compare post-flash `/api/config` semantically against the pre-flash backup.

Acceptance:

- No unapproved configuration change
- Password preservation behavior intact
- Control remains disabled
- Inverter remains disabled
- Recovery AP remains inactive
- Device remains reachable
- No read-only request causes a write or command

Save exact responses and hashes.

---

## Phase 5 - Browser and embedded asset qualification

Validate:

- `/` returns the Automatrix UI
- `/app.css` includes EM500 CSS
- `/app.js` includes all EM500 modules in dependency order
- Correct MIME types
- HTTP 200
- No embedded trailing NUL corruption
- No JavaScript syntax error
- No console error

Test desktop and mobile widths.

Validate these Meters workspace tabs:

- Live measurements
- Energy
- History
- Settings M01-M18
- Meter profiles
- CT/PT/tariff plan

Confirm:

- Legacy single-meter editor is hidden
- Meter profiles can be displayed and edited locally in the UI
- Real settings Apply remains visibly locked
- Planner clearly reports zero Modbus writes
- Unsupported or unavailable data is truthful
- Password values are masked

Save screenshots, console logs, request logs, and DOM evidence under:

```text
evidence/YYYY-MM-DD/browser/
```

---

## Phase 6 - Read-only API qualification

Test without configuration writes:

```text
GET /api/status
GET /api/config
GET /api/meters
GET /api/inverters
GET /api/telemetry
GET /api/meters/em500/snapshot
GET /api/meters/em500/history
GET /api/meters/em500/settings
```

Vary only read-only query parameters:

- Meter index
- FC03/FC04
- Direct PDU versus one-based addressing
- Snapshot scope
- History block
- Settings menu/channel

Acceptance:

- Unavailable values are `null` or explicitly unavailable
- Real zero remains numeric zero
- Unsupported registers do not appear as valid zeros
- Raw words and PDU addresses are preserved
- U64 energy values include exact raw data
- Password raw words are never exposed
- No configuration mutation
- No inverter command

Save each response and hash under:

```text
evidence/YYYY-MM-DD/api/
```

---

## Phase 7 - Meter identification and profile qualification

Do not assume slave roles.

Identify slaves `1`, `2`, and `3` using safe read-only methods:

- Function 17 where supported
- Function 43/14 where supported
- Model/fingerprint registers
- Register availability
- Front-display values
- Exact model manuals already present or obtained from authoritative manufacturer sources

Produce:

| Slave | Model | Evidence | FC03/FC04 | Address base | Proposed role | Confidence |
|---|---|---|---|---|---|---|

Do not assign grid or generator role solely by slave order.

Verify for each meter:

- Data type
- Word order
- Scale
- Sign convention
- Address base
- Poll function
- Unsupported-register behavior

Store evidence under:

```text
evidence/YYYY-MM-DD/meters/
```

---

## Phase 8 - Measurement verification

For every meter, capture at least ten stable samples of:

- Phase/equivalent voltage
- Current
- Frequency
- Total active power
- Power factor

Compare against the front display and an independent reference when available.

Expected sign convention:

Grid meter:

- Positive kW = import
- Negative kW = export

Generator meter:

- Positive kW = generator supplying plant
- Negative kW = reverse power into generator

Do not change CT wiring or meter setup to force a desired sign. Record the actual sign and propose a profile sign multiplier if required.

Calculate minimum, maximum, average, and sample spread.

Acceptance:

- Exact scale and sign are understood
- Fast-control values are stable enough for later control work
- Any mismatch is documented, not hidden

---

## Phase 9 - Register 0x2160 physical qualification

Register:

- Decimal `8544`
- Hex `0x2160`

Determine:

- Which slaves expose it
- FC03 or FC04
- Direct PDU or one-based address
- Value with source input absent
- Value with 220 V source input present
- Stability
- Transition latency
- Chatter
- Invalid values
- Read failures

Target: 100 supervised transitions when site conditions permit.

For every transition record:

- Timestamp
- Physical input condition
- Raw register value
- Voltage
- Frequency
- Current
- Active power
- Read latency
- Debounced interpretation
- Mismatch/conflict

No inverter or PV command may be generated during this test.

Save evidence under:

```text
evidence/YYYY-MM-DD/source-input/
```

---

## Phase 10 - Thirty-minute read-only soak

Run a minimum 30-minute read-only soak across all configured meters and APIs.

Track:

- Request count
- Success count
- Timeouts
- Modbus exceptions
- Reconnects
- Consecutive failures
- Data age
- Poll interval
- Heap
- HTTP latency and responsiveness
- Wi-Fi stability
- Serial errors
- Reboots
- Panics
- Watchdogs
- Configuration mutation
- Inverter commands

Acceptance:

- No diagnostic Modbus write
- No inverter command
- No automatic-control enable
- No configuration mutation
- No recovery AP under normal connectivity
- No panic/watchdog/reboot loop

Fetch `/api/config` before and after and compare semantically.

Save evidence under:

```text
evidence/YYYY-MM-DD/soak/
```

---

## Phase 11 - Wi-Fi reconnect admission qualification

Use the exact test matrix in:

```text
docs/WIFI_RECONNECT_ADMISSION_GATE.md
```

At minimum validate:

- Single reconnect cycles
- Simultaneous accepted/conflict requests
- Late duplicate around 300 ms
- Late duplicate around 450 ms
- Five concurrent sockets
- Response-drain timing
- Admission-quiet timing
- Active-scan race repeated 100 times
- Browser reconnect repeated 5 times
- Recovery AP count
- Panic/watchdog count
- Configuration mutation

Do not weaken timing expectations.

Capture exact HTTP timelines and serial evidence for every failure.

Save under:

```text
evidence/YYYY-MM-DD/wifi-reconnect/
```

---

## Phase 12 - Software defect handling

When a qualification gate fails:

1. Preserve the failing evidence before editing code.
2. Identify whether the failure is:
   - Environment/tooling
   - Test defect
   - Hardware/setup
   - Firmware defect
   - Documentation mismatch
3. Do not weaken the test merely to pass.
4. Make the smallest justified code change.
5. Add a permanent regression test or source contract.
6. Rerun all relevant local checks.
7. Rebuild with ESP-IDF v6.0.1.
8. Do not flash a changed build until the report clearly identifies the new commit and binary hash.
9. Leave changes uncommitted unless specifically instructed, or create a narrowly scoped local commit when the active instruction authorizes it.
10. Record exact changed files, reasoning, tests, and remaining risk.

ChatGPT will review repository changes and decide GitHub actions.

---

## Phase 13 - Commissioning release decision

The current milestone can only be classified as:

### PASS - commissioning and diagnostics release qualified

Only when all required read-only, browser, build, flash, meter, source-input, soak, and reconnect gates pass.

### CONDITIONAL PASS - safe for read-only commissioning with listed restrictions

Use when the firmware is safe and stable for read-only commissioning but one or more non-control hardware characterization gates remain incomplete.

### FAIL - not safe to ship

Use only when an attempted required test demonstrates an unresolved safety or reliability failure.

Do not use FAIL merely because a test is blocked or not yet attempted. Use BLOCKED for incomplete qualification.

Regardless of commissioning outcome, these remain NO until a separate control milestone is completed:

- Safe for meter setup writes
- Safe for live inverter control
- Safe for automatic PV-DG control

---

## Future milestone - guarded meter setup writes

Do not start unless explicitly authorized after commissioning qualification.

Required design gates:

- Service/admin authorization
- Exact meter fingerprint
- Control disabled
- Inverter writes inhibited
- Pre-write snapshot
- Range validation
- Transactional write
- Readback
- Reconnect verification
- Rollback on mismatch
- Audit log
- Separate destructive maintenance workflow

No write is considered qualified until tested physically on the exact meter model.

---

## Future milestone - simulation-only source detector and PV-DG policy

Before live control, implement and validate in simulation:

- `GRID`
- `GENERATOR`
- `TRANSFER`
- `NONE`
- `CONFLICT`
- `FAULT`

Required evidence:

- Raw and debounced source input
- Voltage/frequency/current/kW cross-check
- Stale-data handling
- Startup and transfer stabilization
- Chatter and mismatch alarm behavior
- Fail-safe PV-zero policy
- Grid zero-export
- Limited export
- Limited import
- Generator minimum loading
- Reverse-power warning/trip response
- Generator overload support
- Asymmetric PV ramps
- PI anti-windup
- Bumpless transfer
- Simulator regression scenarios

Live inverter writes remain disabled during this milestone.

---

## Future milestone - live inverter and automatic-control qualification

This is a separate release and requires explicit approval.

Required before enabling:

- Manufacturer-specific register verification
- Command readback
- Mismatch detection
- Failed-channel capacity removal
- Per-inverter and aggregate ramp limits
- Bench test
- Site test
- Independent generator reverse-power protection
- Physical zero-export load-step tests
- Generator minimum-loading tests
- Reverse-power response tests
- Supervised recovery
- Approved hard PV inhibit where required

Never infer approval from successful read-only commissioning.

---

## Qualification report requirements

Maintain:

```text
docs/PHYSICAL_QUALIFICATION_REPORT_2026-07-27.md
```

The report must include:

1. Executive status
2. Branch and commit
3. Hardware
4. Toolchain versions
5. Working-tree state
6. Pre-flash hashes
7. Authoritative build hashes
8. Flash method
9. Serial results
10. Browser results
11. API results
12. Meter identity
13. Measurement verification
14. `0x2160` results
15. Soak statistics
16. Wi-Fi reconnect results
17. Safety evidence
18. Failures and fixes
19. Exact evidence paths
20. Final recommendation

Do not commit the report unless specifically instructed.

---

## Required progress record

After each completed or blocked phase, update the report and create or update:

```text
build/CODEX_STATUS.md
```

Use this exact structure:

```text
PHASE:
STATUS: PASS / FAIL / BLOCKED
BRANCH:
COMMIT:
COMMANDS RUN:
KEY EVIDENCE:
FILES CREATED:
ISSUES FOUND:
CHANGES MADE:
SAFETY STATE:
NEXT SAFE ACTION:
```

Final local status must use:

```text
FINAL LOCAL STATUS:
QUALIFICATION RESULT:
SAFE TO FLASH:
SAFE FOR READ-ONLY COMMISSIONING:
SAFE FOR METER SETUP WRITES:
SAFE FOR LIVE INVERTER CONTROL:
SAFE FOR AUTOMATIC PV-DG CONTROL:
UNCOMMITTED CHANGES:
LOCAL COMMITS:
RECOMMENDED NEXT ACTION:
```

This repository status file is the handoff interface. ChatGPT will read the repository rather than requiring the user to paste long console output.

---

## Current next action

Resume at **Phase 2B**:

1. Resolve exclusive access to `COM5` using process/handle evidence.
2. Capture a real pre-flash serial boot log.
3. Install or activate official ESP-IDF v6.0.1.
4. Preserve the 5.5.4-generated lockfile and restore `dependencies.lock` from HEAD.
5. Build commit `fc5cbf59196f016226bbe1584c718fd32bfeb1b2` in `build-idf601`.
6. Record hashes, sizes, warnings, tool versions, target, and partition table.
7. Recheck the unflashed device and configuration.
8. Update `docs/PHYSICAL_QUALIFICATION_REPORT_2026-07-27.md`.
9. Write the structured result to `build/CODEX_STATUS.md`.
10. Stop before flashing.

No flash is authorized by this roadmap until Phase 2B is complete and the next instruction explicitly permits Phase 3.
