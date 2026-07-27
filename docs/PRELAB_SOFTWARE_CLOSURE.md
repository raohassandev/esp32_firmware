# Automatrix PV-DG Controller — Pre-Lab Software Closure

## Purpose

This document defines the software-complete boundary for the current development build. Work that can be verified without physical meters, inverters, generators, or site wiring is expected to pass CI before the controller returns to the lab.

## Development build identity

The current development branch intentionally includes conveniences that must not ship in a resale controller:

- Primary Wi-Fi provisioning: `Rao`
- Development Wi-Fi provisioning generation: `1`
- Engineering development auto-unlock: enabled
- Recovery access point: `Automatrix-PVDG-Setup`
- Recovery access-point password is configured in firmware defaults

The web interface exposes these development conditions on the **Pre-Lab Readiness** page so they cannot be mistaken for production security.

## Software-complete scope

### Operator product interface

- Overview focused on plant condition and power flow
- Grid Power page with operating measurement, freshness and history
- Solar page with capacity, fleet availability, measured production and history
- Control page showing monitoring/control availability and safety state
- Alarm and event center with severity, active/cleared state and recommended action
- Controller page with product and service status
- Equipment drill-down for meters and inverters
- Responsive desktop, tablet and mobile layouts
- Light/dark theme, comfortable/compact density and kiosk/full-screen behavior

### Protected engineering interface

- Server-side protected technical APIs
- Engineering session cookie and timeout
- Production unique-password mechanism retained
- Network commissioning
- Meter setup and diagnostics
- Inverter profiles, endpoints and read-only qualification
- Control engineering parameters
- Guided commissioning and sanitized report export

### Operational records

- Bounded five-second recent history
- Bounded one-minute history up to 24 hours
- Grid and solar minimum, average and maximum summaries
- Controller-resident alarm/event history
- Diagnostic and commissioning JSON export

### Pre-Lab Readiness workspace

The Readiness page automatically checks:

1. Controller API response
2. Primary or recovery network state
3. Enabled grid-meter freshness
4. Enabled solar-inverter availability
5. Operational history collection
6. Active alarm severity
7. Automatic-control lock state
8. Commandable inverter capacity exposure
9. Engineering authentication/development mode
10. Development Wi-Fi provisioning status

The page is observational only. It must not call control, restart, configuration-write or inverter-command endpoints.

## Automated release gates

The GitHub workflow must pass:

- Browser JavaScript syntax
- Operator product and readiness contracts
- Product/Engineering access-boundary contracts
- Meter and EM500 contracts
- Inverter profile, simulator and write-gate contracts
- ESP-IDF 6.0.1 ESP32-S3 build
- Zero compiler warnings

## Lab-only acceptance work

The following cannot be completed remotely and remain release evidence, not software TODOs:

- Flash the exact CI-green commit without erasing NVS
- Capture complete serial boot log
- Confirm connection to the intended development network
- Verify all operator pages on desktop, tablet and mobile
- Verify readiness checks against actual controller state
- Verify meter sign, scale, freshness and communication recovery
- Verify inverter identity and telemetry read-only against exact physical models
- Trigger and clear representative alarms
- Confirm history accumulation during the soak test
- Complete the protected commissioning workflow
- Export the readiness snapshot and commissioning report
- Run at least 30 minutes continuously

## Production conversion blockers

Before resale or a production release, all of these are mandatory:

- Disable Engineering development auto-unlock
- Remove committed development Wi-Fi credentials
- Disable build Wi-Fi provisioning or use a controlled manufacturing process
- Restore unique `AMX-XXXXXX` first-boot password behavior
- Change the recovery AP password policy from the development default
- Confirm no simulator-only profile is production approved
- Keep automatic physical writes disabled until exact model/manual/readback qualification
- Repeat the full CI and physical acceptance process on the production candidate

## Safety statement

A green software build is a field-test candidate, not evidence that a physical inverter command path is safe. No manufacturer register, scale, write sequence, readback tolerance or production approval may be inferred from simulator success.
