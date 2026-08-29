# Waveshare ESP32-S3 Touch LCD 5 — Stabilization Acceptance Matrix

Parent branch: `board/waveshare-esp32-s3-touch-lcd-5`

Parent PR: #20

Field baseline date: 2026-08-29

Lifecycle: `ACTIVE_STABILIZATION — NOT RELEASE READY`

This document is the release evidence contract for the Waveshare 800x480 product build. A compile pass, a vendor demo, or one successful boot is not product acceptance.

## Evidence rules

1. Every hardware claim must name the exact board branch/head SHA and firmware image that was flashed.
2. Every CI claim must be fresh for that exact head.
3. A hardware-dependent row cannot be closed by source review or CI alone.
4. Any reset, watchdog, visible flicker, display corruption, lost touch, backend stall, persistence mismatch, or control starvation resets the affected soak gate.
5. Product Core control/safety policy must not be weakened to make an HMI test pass.
6. Unknown backend values remain unknown; zero is never substituted for unavailable measurement data.
7. Automatic control must remain fail-closed throughout commissioning, persistence failures, backend loss and display faults.

## A. Display and touch stability

| ID | Test | Required evidence | Pass condition |
|---|---|---|---|
| DISP-01 | Idle overview | 5 min video + serial log | No visible flicker, shake, tearing or spontaneous viewport movement. |
| DISP-02 | Live-value updates | 10 min video while grid/solar values change | Value changes do not move/redraw unrelated cards and no periodic flash occurs. |
| DISP-03 | Grid/Solar lists | 10 min with device values updating | Existing rows update in place; no list flash/rebuild artifact. |
| DISP-04 | Navigation | 100 repeated page changes | No blank frame, stale page overlay, double-tap requirement or touch loss. |
| DISP-05 | Wi-Fi load | Browser/API traffic while LCD updates | No scanout shake or display corruption under network activity. |
| DISP-06 | Commissioning UI | Navigate/edit without saving, then with valid saves | Keyboard/forms/navigation remain visually stable. |
| DISP-07 | Touch soak | >=500 deliberate touches across all pages | No stuck press, ghost navigation or touch-controller loss. |

## B. Native backend/read-model parity

| ID | Test | Required evidence | Pass condition |
|---|---|---|---|
| DATA-01 | `/api/live` parity | LCD vs Core snapshot/log | Grid, solar, requested/applied PV and mode reflect Core authority. |
| DATA-02 | Status parity | LCD vs Core status | Network, source attribution, meter state, controller state and alarms agree. |
| DATA-03 | Meter parity | Healthy/stale/offline scenarios | LCD preserves online/stale/unavailable semantics and never fabricates zero. |
| DATA-04 | Inverter parity | Healthy/offline/not-tested scenarios | LCD state and telemetry/command evidence match Core. |
| DATA-05 | Alarm parity | Raise/clear/acknowledge representative alarms | LCD alarm lifecycle matches authoritative operator alarm state. |
| DATA-06 | Event parity | Generate network/control/device events | LCD recent events match authoritative event order/state. |
| DATA-07 | Recovery | Backend/device loss then recovery | Each affected surface recovers without reboot and unrelated surfaces remain valid. |
| DATA-08 | Bounded failure | Force provider allocation/serialization/read failure where practical | Failure is visible as unavailable; stale/fabricated data is not presented as current. |

## C. Commissioning and persistence

| ID | Test | Required evidence | Pass condition |
|---|---|---|---|
| CFG-01 | Engineering unlock/lockout | Serial/UI evidence | Same credential authority and lockout semantics as protected web commissioning. |
| CFG-02 | Site save/readback | >=20 cycles | Readback equals submitted value every cycle. |
| CFG-03 | Meter save/readback | >=20 cycles over representative fields | Persisted fields exactly match and automatic control is disabled. |
| CFG-04 | Inverter/profile save/readback | >=20 cycles | Profile/config survives readback; invalid profile is refused. |
| CFG-05 | Plant/control save/readback | >=20 cycles | Limits/ramps/generator settings persist exactly; invalid combinations fail closed. |
| CFG-06 | Restart persistence | >=20 save-reboot-read cycles | Intended config survives reboot; no NVS corruption/reset loop. |
| CFG-07 | Interrupted/failed save | Inject available failure cases | Automatic control cannot remain armed after failed/partial persistence. |
| CFG-08 | ARM gate | Unsatisfied then satisfied prerequisites | ARM is refused until canonical Core commissioning gate passes. |
| CFG-09 | Re-entry | Reopen commissioning after restart | UI reloads persisted authoritative state without hidden defaults. |

## D. Resource and control isolation

Capture before UI activation, after activation, under steady UI, during network load and after soak.

| ID | Measurement | Pass condition |
|---|---|---|
| RES-01 | Internal DMA free/largest block | No progressive collapse; qualified RGB path retains required headroom. |
| RES-02 | Internal heap free/minimum | No monotonic leak and no entry into unsafe/reset-prone range. |
| RES-03 | PSRAM free/minimum | Stable after bounded UI/provider allocations. |
| RES-04 | LVGL task stack HWM | Measured margin remains acceptable under worst tested page. |
| RES-05 | Screen refresh task stack HWM | No near-overflow under largest device/alarm payload. |
| RES-06 | Flash-dispatcher stack/queue behavior | Repeated saves do not overflow, deadlock or starve callers. |
| RES-07 | Watchdog/reset reason | No WDT, brownout, panic or unexpected reset. |
| RES-08 | Control cadence/jitter | Active UI/network/persistence does not materially starve the existing control cadence. |

## E. Integrated soak

Minimum integrated soak before PR #20 can leave Draft: **4 continuous hours** on the exact 800x480 board with the Product Core, LCD/touch, Wi-Fi, native backend refresh and representative operator navigation active.

During the soak:

- exercise Overview, Grid, Solar, Alarms, Ready, Commission and Source pages;
- periodically create live-value changes and device/network recovery events;
- execute representative authenticated commissioning reads and bounded save cycles;
- record reset reason, heap/PSRAM/DMA headroom and control timing at start/end and after stress actions;
- record any flicker or UI/backend stall as a failure, not as a cosmetic note.

Final pass requires zero unexpected resets, zero watchdogs, zero visible recurring flicker/shake, zero persistent backend stalls, zero persistence mismatches and no evidence that UI work starved the Product Core.

## Release decision

PR #20 may leave Draft only when Lane A (#24), Lane B (#25), Lane C (#26) and Lane D (#27) are all closed with exact-head evidence satisfying this matrix.
