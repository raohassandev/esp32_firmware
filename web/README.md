# Embedded Automatrix PV-DG Web Application

This directory contains the framework-free browser application embedded directly into the ESP32-S3 firmware and served by `components/web_server` as `/`, `/app.css`, and `/app.js`.

## Product areas

### Operator workspace

Operator pages remain usable without Engineering authentication:

- `#/dashboard` — plant overview
- `#/grid` / `#/meters` — grid measurement and operational meter view (`#/grid` is canonicalized to `#/meters`)
- `#/inverters` — solar fleet operational view
- `#/alarms` — active alarms and event history
- `#/reports` — controller-resident trends, alarm/event chronology, CSV/JSON/self-contained HTML export and print-ready operational reports
- `#/readiness` — controlled-test readiness

Operator pages use sanitized, read-only APIs only. They must never expose Wi-Fi credentials, Modbus endpoint details, raw register maps, setup registers, or command actions. Reports are explicitly controller-resident service/operations records; they are not represented as billing-grade or long-term historian data. Sample timestamps exported by Reports are estimates reconstructed from controller-reported sample age.

Reports treats history, events and alarms as independent data sources. A failure or timeout in one source must not erase usable evidence from the others. JSON and HTML exports expose source-availability/data-quality state and explicitly do not claim physical qualification.

### Engineering workspace

Engineering authentication is required for:

- `#/engineering`
- `#/commissioning`
- `#/wifi`
- `#/control`
- `#/system`
- detailed meter/inverter configuration and diagnostics

The System page includes the rollback-safe OTA maintenance workflow: software preflight, image selection, bounded inactive-slot upload, staged-image review, explicit reboot, automatic browser reconnect and first-boot/rollback status reporting. The browser assists the workflow but does not replace backend image validation or physical OTA qualification.

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
- `GET /api/operator/alarms`

### Engineering-authenticated reads and writes

- Wi-Fi scan/configuration and static addressing
- meter endpoint configuration
- inverter profile/endpoint configuration
- EM500 raw snapshot, historical register blocks, settings and setup diagnostics
- commissioning wizard operations
- control configuration
- secure OTA status/upload/reboot
- system restart, import/export and service operations
- Engineering password management

### Safety boundary

- Operator polling and reporting must never write Modbus registers or enable control.
- Detailed EM500 register APIs are Engineering-only because they expose PDU addresses, raw words, setup registers and communication metadata.
- Operator charts and reports use controller-resident operational history rather than raw EM500 register endpoints.
- Unverified inverter profiles remain read-only and cannot contribute to commandable capacity.
- Automatic control remains disabled until physical qualification is complete.
- OTA upload remains backend-gated by product/chip/secure-version identity, safe-zero confirmation, whole-image validation, inactive-slot staging and rollback-safe first boot.
- Browser-reported OTA success is software evidence only; physical interruption/power-loss/rollback qualification remains a separate release gate.

## Active browser ownership

The embedded bundle is modular, but responsibilities must remain singular:

- `app.js` — base static router and shared application state.
- `product-mode.js` — single owner of Engineering authentication state and protected-route enforcement.
- `product-shell-v2.js` / `product-shell-v2.css` — header health, overflow/service actions, route context, responsive shell and duplicate-intro cleanup. It does **not** reorder the global navigation.
- `product-experience-v2.js` / `product-experience-v2.css` — route-aware page mastheads, operator/Engineering scope and page composition. It does **not** inject a second navigation hierarchy.
- `industrial-ui-v1.js` / `industrial-ui-v1.css` — authoritative final navigation grouping/order, role/status presentation and industrial HMI visual layer.
- `operator-operations.js` / `operator-product-suite.js` — operator dashboards, history and alarms.
- `reports.js` / `reports.css` — read-only operational reporting, partial-source resilience, export and print composition. Because the base router is static, Reports owns only a narrow activation bridge and placement of its own dynamic navigation link after the authoritative Industrial UI reorder pass.
- `ota.js` / `ota.css` — Engineering OTA maintenance workflow and browser-side post-reboot verification.
- `commissioning-release-v3.js` — active seven-step commissioning workflow.
- `network-commissioning-fix.js` — resilient Wi-Fi save/restart/reconnect flow.
- `em500-core.js` and related EM500 modules — Engineering-only detailed meter diagnostics.
- inverter modules — Engineering configuration plus read-only operational telemetry.

The former `shell-current-fixes.js` / `shell-current-fixes.css` compatibility repair layer was retired after its valid behavior was absorbed into Product Shell V2. Older compatibility modules may remain embedded only while required by active routes or source contracts. New behavior must not be added to multiple generations of the same responsibility.

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

- browser syntax checks, including Reports, OTA, Product Shell, Product Experience and Industrial UI modules
- software product-polish/source-ownership contracts
- secure OTA baseline and behavior contracts
- production access-policy contract
- Engineering auth-loop prevention contract
- operator telemetry/reporting boundary tests
- Wi-Fi commissioning and mobile-layout contracts
- inverter write-gate and read-only probe contracts
- complete ESP-IDF v6.0.1 build with zero compiler warnings

Physical acceptance must additionally prove:

- logout never traps Overview, Grid, Solar, Alarms, Reports or Readiness on `#/engineering`;
- operator pages remain stable for at least five minutes after logout;
- protected routes request Engineering authentication;
- browser refresh preserves a valid session, while controller restart invalidates only the session and not the stored password;
- recovery AP remains usable;
- no NVS erase is performed;
- automatic control and physical inverter writes remain locked until qualification;
- OTA interruption, power-loss, pending-verify and rollback behavior on the intended release hardware.