# Field release — 711f579

Branch: `feature/multibrand-inverter-profiles`

Release commit: `711f57917227d619e780d9c0477c21784811e76f`

CI result: **PASS**

## Completed in this release

- Unified the legacy fast meter poll with the complete EM500 engineering scale for the observed INT32/ABCD/address 57–58/scale 0.01 fingerprint.
- Dashboard, diagnostics and control calculations now use the same corrected kW value as the complete EM500 workspace.
- A single transient poll error no longer marks a meter offline while the last valid sample is still fresh.
- Added explicit dashboard-versus-EM500 consistency monitoring.
- Completed light-theme coverage for meter, inverter, telemetry, tabs, tables, controls and diagnostic cards.
- Moved theme overrides after component CSS so light-theme rules cannot be overwritten.
- Improved desktop, tablet and mobile breakpoints.
- Added drawer navigation below 860 px.
- Added responsive stacking for dashboard metrics, power flow, meter summaries and inverter summaries.
- Added collapsible EM500 parameter groups to reduce excessive page height.
- Added regression contracts for scaling, health state, theme ordering, responsive layout and collapsible meter groups.

## Validation

- Browser syntax: PASS
- SolTrix Huawei/GoodWe/Solis simulator: PASS
- Meter contracts: PASS
- Inverter safety contracts: PASS
- Theme contract: PASS
- UI/runtime consistency contract: PASS
- ESP-IDF v6.0.1 build: PASS
- Project compiler warnings: 0

## Field test requirements

1. Pull and flash this commit without erasing NVS.
2. Confirm Dashboard Grid Power matches EM500 Active Power Total within normal refresh timing tolerance.
3. Confirm Meter status remains online during an isolated failed poll while the last sample is fresh.
4. Test light and dark themes on desktop, tablet and mobile widths.
5. Run at least a 30-minute soak with automatic control disabled.
6. Keep physical inverter writes and automatic PV-DG control locked until exact manual-backed profiles and physical qualification are complete.
