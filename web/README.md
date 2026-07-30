# Embedded Automatrix PV-DG Web Application

This directory contains the framework-free browser application embedded directly into the ESP32-S3 firmware and served by `components/web_server` as `/`, `/app.css`, and `/app.js`.

## Product areas

### Operator workspace

Operator pages remain usable without Engineering authentication:

- `#/dashboard` — plant overview
- `#/grid` / `#/meters` — grid measurement and operational meter view
- `#/inverters` — solar fleet operational view
- `#/alarms` — active alarms and event history
- `#/readiness` — controlled-test readiness

Operator pages use sanitized, read-only APIs only. They must never expose Wi-Fi credentials, Modbus endpoint details, raw register maps, setup registers, or command actions.

### Engineering workspace

Engineering authentication is required for:

- `#/engineering`
- `#/commissioning`
- `#/wifi`
- `#/control`
- `#/system`
- detailed meter/inverter configuration and diagnostics

Development auto-unlock is disabled in production candidates. A `401` may redirect to Engineering sign-in only when the current route is protected; a background operator poll must never redirect the application.

## API access policy

### Public operational reads

- `GET /api/status`
- `GET /api/telemetry`
- sanitized `GET /api/config`
- sanitized `GET /api/meters`
- sanitized `GET /api/inverters`
- `GET /api/inverter-telemetry`
- `GET /api/operator/history`
- `GET /api/operator/events`

### Engineering-authenticated reads and writes

- Wi-Fi scan/configuration and static addressing
- meter endpoint configuration
- inverter profile/endpoint configuration
- EM500 raw snapshot, historical register blocks, settings and setup diagnostics
- commissioning wizard operations
- control configuration
- system restart, import/export and service operations
- Engineering password management

### Safety boundary

- Operator polling must never write Modbus registers or enable control.
- Detailed EM500 register APIs are Engineering-only because they expose PDU addresses, raw words, setup registers and communication metadata.
- Operator charts use controller-resident operational history rather than raw EM500 register endpoints.
- Unverified inverter profiles remain read-only and cannot contribute to commandable capacity.
- Automatic control remains disabled until physical qualification is complete.

## Active browser ownership

The embedded bundle is modular, but responsibilities must remain singular:

- `app.js` — base router and shared application state
- `product-mode.js` — single owner of Engineering authentication state and protected-route enforcement
- `product-shell-v2.js` / `product-experience-v2.js` — product shell, navigation and page composition
- `operator-operations.js` / `operator-product-suite.js` — operator dashboards, history and alarms
- `pvdg-chart.js` / `pvdg-chart.css` — the only time-series chart. Every trend on every screen is one instance of this component, moved between pages and reconfigured. Nothing else may draw a chart: a second implementation is how the product ended up with two disagreeing trends on one dashboard, neither of which showed a missing measurement as missing.
- `commissioning-release-v3.js` — active seven-step commissioning workflow
- `network-commissioning-fix.js` — resilient Wi-Fi save/restart/reconnect flow
- `em500-core.js` and related EM500 modules — Engineering-only detailed meter diagnostics
- inverter modules — Engineering configuration plus read-only operational telemetry

Older compatibility modules may remain embedded only while required by active routes or source contracts. New behavior must not be added to multiple generations of the same responsibility.

## Commissioning sequence

1. Site details
2. Devices
3. Communication channel
4. Modbus tuning
5. Connection qualification
6. Controller health
7. Review, report and finish

RTU devices cannot receive a Ready verdict until the real RS-485/Modbus RTU runtime is implemented and physically qualified.

## Validation gates

Every release candidate must pass:

- browser syntax checks
- production access-policy contract
- Engineering auth-loop prevention contract
- operator telemetry boundary tests
- Wi-Fi commissioning and mobile-layout contracts
- inverter write-gate and read-only probe contracts
- complete ESP-IDF v6.0.1 build with zero compiler warnings

Physical acceptance must additionally prove:

- logout never traps Overview, Grid, Solar, Alarms or Readiness on `#/engineering`;
- operator pages remain stable for at least five minutes after logout;
- protected routes request Engineering authentication;
- browser refresh preserves a valid session, while controller restart invalidates only the session and not the stored password;
- recovery AP remains usable;
- no NVS erase is performed;
- automatic control and physical inverter writes remain locked until qualification.
