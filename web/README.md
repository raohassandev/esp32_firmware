# Embedded PV-DG Web Application

This directory contains the controller's framework-free browser application.
It is embedded directly into the ESP-IDF firmware and served by the
`web_server` component.

## Files

- `index.html` — semantic application shell and page markup.
- `app.css` — responsive industrial UI system.
- `app.js` — API client, hash router, application state and common pages.
- `wifi-utils.js` — pure IPv4, netmask, security and signal helpers shared by
  the browser and Node tests.
- `wifi-guard.js` — capture-phase protection against changing an enabled SSID
  without a suitable new credential.
- `wifi.js` — Wi-Fi scan, commissioning, validation and restart workflow.
- `wifi.css` — responsive Wi-Fi scan and commissioning styles.
- `devices-utils.js` — pure meter/inverter state and formatting helpers.
- `devices.js` — read-only runtime diagnostics for meter and inverter pages.
- `devices.css` — responsive device health and diagnostics styles.
- `tests/wifi-utils.test.js` — dependency-free Wi-Fi utility tests.
- `tests/devices-utils.test.js` — dependency-free device-state utility tests.

No package manager, build tool, external CDN or runtime dependency is used.
This keeps the firmware build deterministic and the browser payload suitable
for an embedded controller.

The server streams the common, Wi-Fi and device CSS modules as one `/app.css`
response. It streams the common application, Wi-Fi modules and device modules
as one `/app.js` response. ESP-IDF's trailing text-asset NUL byte is excluded
from every streamed part.

## Routes

Navigation uses URL hashes and does not require server-side route handling:

- `#/dashboard`
- `#/wifi`
- `#/meters`
- `#/inverters`
- `#/control`
- `#/system`

## Current API dependencies

- `GET /api/status`
- `GET /api/config`
- `POST /api/config`
- `GET /api/wifi/scan` — cached asynchronous scan snapshot.
- `POST /api/wifi/scan` — starts a non-blocking radio survey.
- `POST /api/wifi/config` — dedicated validated Wi-Fi configuration write.
- `POST /api/wifi/rescan` — disconnects and retries saved profiles.
- `GET /api/meters` — configuration plus read-only meter poll diagnostics.
- `GET /api/inverters` — configuration plus read-only command-channel history.
- `POST /api/system/restart`

## Wi-Fi commissioning rules

1. Radio scans execute in a dedicated low-priority task. HTTP handlers only
   request a scan or read its cached snapshot.
2. Scan results expose SSID, RSSI, channel and security mode. BSSIDs and saved
   credentials are never returned.
3. Duplicate SSIDs are collapsed to the strongest visible access point.
4. Unsupported security modes are visible but cannot be selected.
5. Primary and fallback profiles cannot use the same enabled SSID.
6. Static IPv4 settings require a contiguous netmask, a usable host address and
   a gateway in the same subnet.
7. A blank or masked password preserves the credential only while the SSID is
   unchanged. Changing an SSID without a new password clears the old
   credential, preventing cross-network credential carry-over.
8. The browser blocks an enabled SSID change without a new password unless the
   latest scan identifies that SSID as Open or OWE.
9. Changing the recovery-AP SSID requires a new recovery password.
10. Selecting an open network explicitly clears the station password.
11. Wi-Fi changes use the dedicated `/api/wifi/config` endpoint and require an
    operator confirmation before the controller restarts.
12. The reconnect action is separate from a non-disruptive network scan and
    requires its own confirmation.

## Device telemetry rules

1. Meter diagnostics expose configured acquisition settings and runtime poll
   health without creating a configuration write path.
2. A meter value is returned as `null` until a valid Modbus sample has been
   decoded. A retained stale value remains available only with an explicit
   `stale=true` flag and age.
3. Poll attempts, successful samples, total errors, consecutive failures and the
   latest ESP error are recorded independently for every configured meter.
4. Disabled meters are reported as disabled, not stale or failed.
5. Inverter diagnostics describe the command channel only. The current firmware
   has no inverter production register mapping, so measured power remains
   explicitly unavailable.
6. A successful inverter write must never be displayed as proof that the
   inverter is online, generating or producing the commanded power.
7. Browser device pages are read-only and never invoke a Modbus write or change
   automatic-control state.

## Safety and data rules

1. Never display missing or failed meter data as a current `0.00 kW` value.
2. Clearly distinguish fresh, stale and unavailable readings.
3. Keep exported password values masked. Password inputs are always blank when
   configuration is loaded.
4. Do not add an automatic-control enable action without a separately reviewed
   commissioning workflow and safety confirmation.
5. Label Modbus addresses as **PDU addresses**; do not silently apply FUXA's
   one-based display convention.
6. Do not infer PV, generator or facility-load telemetry from control requests.
   Show `Unavailable` until dedicated telemetry is implemented.
7. The generic configuration importer accepts only a subset of exported fields.
   Controls that are not yet writable remain read-only.

## Development and validation standard

- Keep browser code dependency-free and modular by responsibility.
- Prefer DOM `textContent` and element creation for device- and scan-derived
  values.
- Validate operator inputs in both the browser and firmware endpoint.
- Preserve all unrelated configuration when updating one subsystem.
- Keep control disabled by default and never issue commands from page rendering,
  status polling, device diagnostics or Wi-Fi commissioning.
- Run before hardware validation:

```text
node --check web/app.js
node --check web/wifi-utils.js
node --check web/wifi-guard.js
node --check web/wifi.js
node --check web/devices-utils.js
node --check web/devices.js
node web/tests/wifi-utils.test.js
node web/tests/devices-utils.test.js
```

- Hardware qualification must verify scan concurrency, repeated scans, busy
  responses during reconnect, password masking, device API JSON semantics,
  meter error counters, stale-value labeling, inverter command-state wording,
  recovery-AP operation, no NVS erase and no unintended inverter commands.
