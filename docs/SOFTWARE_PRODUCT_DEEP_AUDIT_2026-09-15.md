# Software Product Deep Audit — 2026-09-15

## Scope and authority

Baseline audited: `dev` `ac436b18f1f5e0437a2ad523c352ac8fac0591c6` after PR #202.

This audit covers work that can be completed and validated remotely in the firmware/software repository: embedded browser architecture, UI ownership, route behavior, responsive/theme behavior, controller-resident reporting, OTA browser integration, resource configuration and software regression coverage.

It **does not** create or infer physical evidence. In particular, it does not close Waveshare hardware acceptance (#174), generator-transition bench proof (#80), real-site commissioning (#81), manufacturer inverter qualification (#82), secure-OTA physical interruption/rollback qualification (#86), integrated FAT/endurance/SAT (#83), or final evidence manifest #91.

## Executive verdict

The remaining actionable software-product gaps identified in the current `dev` baseline were real but concentrated in presentation ownership and reporting resilience rather than control/safety logic. This consolidation closes those remote gaps without changing the mature backend safety boundary.

After this change is merged to current `dev` with exact-head CI green and zero-behind, the remote software-product scope should be treated as **software-complete**. Further software changes should be driven by a reproduced defect or by evidence from the remaining physical gates, not by speculative churn.

Overall product/release completion remains blocked by the genuine physical/external gates listed above.

## Audit findings and disposition

| ID | Finding | Severity | Disposition |
|---|---|---:|---|
| A1 | Reports existed as a dynamic route but was not represented in Product Experience V2 page metadata. | Medium | Closed. Reports now receives the same operator masthead, scope and page classification as the rest of the product. |
| A2 | `#/grid` was documented as an operator route alias but the shell/router path was not canonicalized consistently. | Medium | Closed. Product Shell canonicalizes the legacy alias to `#/meters`; Product Experience resolves the same alias for route context. |
| A3 | `shell-current-fixes.js/css` formed a separate compatibility/repair layer that patched behavior already owned by Product Shell V2. | High maintainability | Closed. Valid behavior was absorbed into Product Shell V2; both repair assets were deleted and removed from CMake, linker asset declarations and composite serving. |
| A4 | Product Experience V2 still contained dark-first hardcoded/translucent colors that could bypass theme semantics. | High UX | Closed for the audited ownership layer. Scope/borders/cards/Engineering emphasis now use theme tokens and `color-mix()` rather than the known dark-only values. |
| A5 | Header health duplicated authoritative Industrial UI alarm/freshness information on native 800x480-class HMI layouts, consuming scarce vertical/header space. | Medium UX | Closed in software. The duplicate shell health control is hidden in the small-height HMI breakpoint while authoritative role/alarm/freshness status remains. Physical 800x480 acceptance is still #174. |
| A6 | Reports fetched history, events and alarms sequentially under one request controller; one endpoint failure made the whole report unavailable. | High reliability | Closed. The three read-only sources load concurrently with independent timeout/abort handling via `Promise.allSettled`; successful sections remain usable when another source fails. |
| A7 | Reports exports did not expose source-level data quality and the report UI did not make generation/range/controller freshness metadata first-class. | Medium evidence quality | Closed. Visible metadata was added; JSON schema 2 records per-source availability/error; exports explicitly mark non-billing/non-historian/estimated timestamps/no physical qualification claim. |
| A8 | Reporting lacked a portable human-readable evidence artifact independent of the live controller UI. | Medium operations | Closed. Added a self-contained sanitized HTML report export in addition to CSV, JSON and print. |
| A9 | Earlier July audit reported PSRAM disabled on the N16R8 target. | Former critical resource issue | Already closed before this change. Current `sdkconfig.defaults` enables octal PSRAM at 80 MHz, malloc integration and Wi-Fi/lwIP PSRAM allocation. No speculative setting change was made in this audit. |
| A10 | Earlier July audit reported browser socket starvation with default HTTPD socket count and no LRU purge. | Former critical availability issue | Already closed before this change. Current web server uses 10 open HTTP sockets with LRU purge; `CONFIG_LWIP_MAX_SOCKETS=16` leaves non-httpd headroom. |
| A11 | Older operator/browser layers historically initiated work outside the active route/access scope. | High load/UX | No new regression found in the audited shell/experience/reporting changes. Existing route/access contracts remain in CI, and the new consolidation adds no Engineering endpoint to Reports or shell presentation code. |
| A12 | Secure OTA browser flow was recently reworked and was a tempting target for additional churn. | Safety-sensitive | Intentionally unchanged at the backend/safety level. Existing product/chip/secure-version validation, safe-zero-before-write, inactive-slot staging, explicit reboot, first-boot validation and rollback contracts remain authoritative. Physical #86 remains open. |

## Detailed architecture review

### 1. Navigation and route ownership

The embedded application still has a static base router in `app.js`. Product Shell V2 owns header health, overflow/service actions, route context and shell-level responsiveness. Product Experience V2 owns page mastheads and route-aware Operator/Engineering scope. Industrial UI v1 remains the authoritative global navigation grouping/order and final industrial presentation layer.

The audit removed one competing ownership layer: Product Shell/Product Experience no longer attempt to create a second global navigation hierarchy, and `shell-current-fixes` is gone.

Reports is the one dynamic operator route not declared in the original static HTML/router table. To avoid expanding a second router, it owns only:

1. creation/placement of its own Reports navigation link after the authoritative Industrial UI reorder pass; and
2. a narrow activation bridge for `#/reports`.

This is acceptable for the current firmware scope and is regression-guarded. If future work adds several more dynamic product routes, the correct next architectural change would be an explicit route-registration API in `app.js`, not additional per-feature router bridges.

### 2. Theme and presentation consistency

The July 2026 visual audit identified severe light/dark contrast defects and undersized mobile controls in older layers. The current product stack had already fixed much of that work through Industrial UI tokens and 44 px controls; this audit addressed the remaining known Product Experience dark-only overrides and preserved the token-based final Industrial UI layer.

Product Shell now also absorbs the valid responsive fixes that were previously applied later by `shell-current-fixes`. This makes the CSS cascade easier to reason about and removes a source of presentation drift.

Physical visual acceptance remains necessary because source-contract and browser syntax checks cannot prove real Waveshare color, touch, DPI, rendering or sunlight/field usability.

### 3. Native HMI and phone behavior

The shell preserves 44 px minimum interactive controls and the mobile bottom-sheet action model. At small-height HMI dimensions, the extra Product Shell health summary is suppressed to avoid duplicating Industrial UI role/alarm/freshness controls.

This is a software layout improvement only. The frozen PR #179 800x480 hardware acceptance remains the physical authority for touch targets, role visibility, alarm interaction and sustained runtime.

### 4. Controller-resident Reports

Reports remains strictly read-only. It uses only the existing public operational endpoints:

- `GET /api/operator/history?range=...`
- `GET /api/operator/events`
- `GET /api/operator/alarms`

No new URI handler or write endpoint was added.

Reliability changes in this audit:

- concurrent source requests;
- independent timeout/abort handling;
- partial-data rendering instead of all-or-nothing failure;
- explicit source quality state;
- visible generation time, requested window, controller state and controller-data freshness;
- estimated sample timestamps are labelled as estimates because samples carry age rather than an authoritative wall-clock timestamp;
- CSV remains sample/history oriented;
- JSON includes the full available operational bundle plus data-quality/limitations metadata;
- self-contained HTML provides portable human-readable evidence;
- print layout has explicit A4 landscape page settings and break control.

Reports still must not be represented as billing-grade energy accounting or a long-term historian. Persistent high-resolution history would require a separate storage/wear/resource design rather than being implied by browser exports.

### 5. Secure OTA

The audit reviewed the current OTA boundary but deliberately avoided backend safety churn. Software already provides the intended rollback-safe sequence: validate identity/security/version/slot capacity, force control safe-zero before first firmware write, stream the inactive slot, finish and revalidate, require explicit authenticated reboot, then validate or roll back on first boot.

The guided browser flow remains an operator/engineer aid only. Browser reconnect, staged-image status and JSON evidence cannot substitute for physical interruption, power-loss and rollback proof on the intended release hardware. Issue #86 therefore remains release-critical.

### 6. Resource and availability configuration

Two critical findings from the July audit were rechecked against current source rather than blindly reimplemented:

- **PSRAM:** current `sdkconfig.defaults` enables the intended ESP32-S3 N16R8 octal PSRAM and malloc/lwIP integration.
- **HTTP sockets:** current web server config uses `max_open_sockets = 10` and `lru_purge_enable = true`; lwIP socket capacity is 16.

Because those fixes already exist on current `dev`, this audit does not introduce duplicate or speculative hardware configuration changes.

### 7. Regression/CI coverage

The software-product workflow now syntax-checks:

- `web/reports.js`
- `web/ota.js`
- `web/product-shell-v2.js`
- `web/product-experience-v2.js`
- `web/industrial-ui-v1.js`

Source contracts additionally prevent regression of:

- the retired `shell-current-fixes` layer;
- broken `#/grid` alias behavior;
- known Product Experience dark-only overrides;
- loss of the small-height HMI header de-duplication;
- all-or-nothing Reports loading;
- Reports write/config authority;
- missing report data-quality/no-physical-claim metadata;
- secure OTA browser/backend coupling assumptions.

The normal firmware/web and browser-resilience workflows remain required in addition to this focused gate.

## Residual software architecture — non-blocking for current scope

One design debt remains visible: the base router is static while Reports is dynamically supplied. The current narrow Reports activation bridge is intentionally bounded and tested. It should not be generalized by copy/paste.

If the embedded product gains additional dynamic operator workspaces, refactor `app.js` to expose a single route-registration mechanism and migrate Reports to it. That would be a maintainability improvement, not evidence that the current route is unsafe or incomplete.

No additional remote software work is justified solely to eliminate this debt before physical qualification.

## Physical/external blockers intentionally unchanged

- #174 — frozen Industrial UI/Waveshare native 800x480 physical acceptance.
- #80 — generator source-transition physical bench.
- #81 — real-site source commissioning and meter/source-map proof.
- #82 — installed manufacturer/model/firmware inverter-profile qualification and controlled write/readback proof.
- #86 — secure OTA interruption/power-loss/pending-verify/rollback/NVS physical qualification.
- #83 — integrated FAT, endurance and signed SAT.
- #91 — final exact-identity evidence manifest and release traceability.
- Rev-A intended-fabricator DFM and H4 fabrication/physical qualification remain independent hardware work.

## Done rule

For the **remote software-product scope**, completion requires this consolidation to land on current `dev` with exact-head CI green and the branch zero-behind at merge time.

For the **overall product/release**, completion still requires genuine accepted physical evidence for every mandatory gate and final traceability. No software test in this audit changes that rule.