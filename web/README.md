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
- `devices-utils.js` — pure formatting and state-classification helpers for
  meters, inverter command channels and operational readiness.
- `devices.js` — read-only Dashboard, meter and inverter runtime diagnostics.
- `devices-refresh.js` — connects the common top-bar refresh action to the
  active read-only diagnostics route.
- `devices.css` — responsive diagnostics cards, summaries and tables.
- `tests/wifi-utils.test.js` — dependency-free Wi-Fi validation tests.
- `tests/devices-utils.test.js` — dependency-free null, stale and command-state
  rendering tests.

No package manager, build tool, external CDN or runtime dependency is used.
This keeps the firmware build deterministic and the browser payload suitable
for an embedded controller.

The server streams the common, Wi-Fi and device CSS modules as one `/app.css`
response. The common application, Wi-Fi utilities, credential guard,
commissioning module, device utilities, device diagnostics and refresh bridge
are streamed as one `/app.js` response. ESP-IDF's trailing text-asset NUL byte
is excluded from every streamed part.

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
- `POST /api/wifi/rescan` — returns its accepted/conflict response before the
  synchronized admission gate allows radio teardown.
- `GET /api/meters` — detailed read-only meter configuration and runtime health.
- `GET /api/inverters` — detailed read-only inverter command-channel health.
- `GET /api/telemetry` — read-only operational summary for the Dashboard.
- `POST /api/system/restart`

The three device telemetry endpoints do not save configuration, write Modbus
registers or enable automatic control.

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

## Safety and data rules

1. Never display missing or failed meter data as a current `0.00 kW` value.
   A real measured zero remains valid, while missing values are JSON `null` and
   render as `Unavailable`.
2. Clearly distinguish fresh, stale, unavailable and initialization-failed
   meter states.
3. A successful inverter command write is not measured inverter production and
   is not proof of continuous inverter availability.
4. The reported commanded kW must be derived from the final clamped percentage
   actually written, not from the unconstrained requested share.
5. Measured inverter power, generator power and facility load remain `null`
   until dedicated register mappings and runtime acquisition exist.
6. Keep exported password values masked. Password inputs are always blank when
   configuration is loaded.
7. Do not add an automatic-control enable action without a separately reviewed
   commissioning workflow and safety confirmation.
8. Label Modbus addresses as **PDU addresses**; do not silently apply FUXA's
   one-based display convention.
9. The generic configuration importer accepts only a subset of exported fields.
   Controls that are not yet writable remain read-only.

## Development and validation standard

- Keep browser code dependency-free and modular by responsibility.
- Prefer DOM `textContent` and element creation for API-derived values.
- Validate operator inputs in both the browser and firmware endpoint.
- Preserve all unrelated configuration when updating one subsystem.
- Keep control disabled by default and never issue commands from page rendering,
  status polling or Wi-Fi commissioning.
- Run before hardware validation:

```text
node --check web/app.js
node --check web/wifi-utils.js
node --check web/wifi-guard.js
node --check web/wifi.js
node --check web/devices-utils.js
node --check web/devices.js
node --check web/devices-refresh.js
node web/tests/wifi-utils.test.js
node web/tests/devices-utils.test.js
python3 tests/telemetry_source_contract.py
```

- Hardware qualification must verify reconnect admission timing, scan
  concurrency, repeated scans, complete HTTP 202/409 responses, password
  masking, recovery-AP behavior, DHCP and static-profile validation, no NVS
  erase, read-only telemetry polling and no inverter command generation from
  diagnostics pages.
