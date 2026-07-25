# Embedded PV-DG Web Application

This directory contains the controller's framework-free browser application.
It is embedded directly into the ESP-IDF firmware and served by the
`web_server` component.

## Files

- `index.html` — semantic application shell and page markup.
- `app.css` — responsive industrial UI system.
- `app.js` — API client, hash router, application state, page rendering,
  validation and controller actions.

No package manager, build tool, external CDN or runtime dependency is used.
This keeps the firmware build deterministic and the browser payload suitable
for an embedded controller.

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
- `POST /api/wifi/rescan`
- `POST /api/system/restart`

## Safety and data rules

1. Never display missing or failed meter data as a current `0.00 kW` value.
2. Clearly distinguish fresh, stale and unavailable readings.
3. Keep exported password values masked. Empty password fields preserve stored
   credentials through the configuration importer.
4. Do not add an automatic-control enable action without a separately reviewed
   commissioning workflow and safety confirmation.
5. Label Modbus addresses as **PDU addresses**; do not silently apply FUXA's
   one-based display convention.
6. Do not infer PV, generator or facility-load telemetry from control requests.
   Show `Unavailable` until dedicated telemetry is implemented.
7. The current configuration importer accepts only a subset of exported fields.
   Controls that are not yet writable are deliberately rendered read-only.

## Development standard

- Keep browser code dependency-free and split by responsibility inside
  `app.js` until file size or testing requirements justify additional embedded
  assets.
- Prefer DOM `textContent` and element creation over injecting untrusted HTML.
- Validate operator inputs before posting configuration.
- Preserve the complete configuration object when updating one section.
- Keep control disabled by default and never issue commands from page rendering
  or status polling.
