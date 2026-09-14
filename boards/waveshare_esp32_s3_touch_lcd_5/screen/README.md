# Waveshare 5-inch Local HMI Workspace

This workspace contains the board-local LVGL HMI used by the dedicated Waveshare product target. The shared Product Core remains authoritative for control, safety, source attribution, configuration, authentication, meters/inverters, alarms, persistence and OTA behavior.

## Current integration lane

The active integration is PR #179 on branch `work/waveshare/industrial-ui-v1-integration`. It starts from live `dev` and reuses the historical Waveshare board source only as a reviewed hardware/UI baseline. Historical physical evidence does not transfer to the current Industrial UI candidate.

The root/default firmware project remains separate. The dedicated 800x480 product project under `product_800x480/` composes the shared current Product Core with this board-local screen component.

## Safety and authority rules

- The screen must not create a second control, safety, source-detection, alarm, authentication or persistence implementation.
- Unknown, stale or unavailable measurements remain visibly unknown; they are never converted to a measured zero.
- Source attribution comes from current Core fail-closed authority. The LCD never guesses source state from electrical values.
- Automatic control changes remain fail-closed: configuration writes force running control disabled, and arming is persisted for the next restart. Current Core starts fail-safe at zero PV command and grants command authority only when its own evidence gates pass.
- Production qualification is not inferred by the LCD. Runtime command authority and physical/governance qualification are separate concepts.
- Engineering mutations require the same runtime Engineering setup-code/permanent-password authority and shared lockout state used by the protected web workspace. There is no board-local PIN and no compile-time credential-prefill option in the production candidate.
- Alarm acknowledgement mutates only the authoritative Core alarm lifecycle and requires an unlocked local Engineering session.

## Current structure

- `api/` — bounded screen-owned DTOs/parsers for the existing operator/status contracts; no control policy.
- `components/` — reusable LVGL presentation widgets.
- `pages/` — Overview, Grid, Solar, Alarms/Events, Runtime Readiness, Engineering Commissioning and Source Evidence commissioning.
- `drivers/` — reviewed Waveshare RGB/touch profiles and hardware port.
- `screen_app.*` — lazy-created native navigation/page shell.
- `screen_runtime.*` — provider-injected refresh bridge; scheduling is owned by the product integration task.
- `product_800x480/` — dedicated flashable current-Core + 800x480 Waveshare product target.
- `hil_800x480/` and `qualification/` — isolated hardware/qualification projects; they are not substitutes for product-image acceptance.
- `SCREEN_TODO.md` — current software/physical acceptance status.

## Data integration

The native product target uses an in-process local provider to project the same authoritative Core state consumed by the web/operator contracts. It does not create loopback HTTP ownership or a second backend.

Read models cover:

- `/api/live`
- `/api/status`
- `/api/meters`
- `/api/inverters`
- `/api/telemetry`
- `/api/operator/events`
- `/api/operator/alarms`

The event/alarm provider reads the same Core event ring and ISA-18.2-style lifecycle table used by the HTTP routes. Per-alarm acknowledgement calls the same bounded Core acknowledgement primitive after local Engineering authorization.

## Native operator/Engineering surfaces

- **Overview** — Grid/source power, Solar power, requested/applied PV, control state/reason and health summary.
- **Grid** — configured meter state and measurements.
- **Solar** — inverter fleet state, telemetry and command evidence.
- **Alarms** — authoritative alarm/event state, `All / Active / Unack` filters, `Priority / State / ID` sorts and per-row Engineering-authenticated acknowledgement.
- **Ready** — runtime monitoring/command-path status and explicit separation from production qualification.
- **Commission** — authenticated current-schema site, meter, inverter, plant/control/ramp configuration. Retired schema controls are not shown or silently translated.
- **Source** — authenticated source-evidence register/timing configuration; Core source semantics remain authoritative.

## Qualified software dependency set

The dedicated screen component pins:

- ESP-IDF `>=6.0.1,<6.1.0` (candidate CI container is `espressif/idf:v6.0.1`)
- `espressif/cjson ==1.7.19~2`
- `lvgl/lvgl ==9.5.0`
- `espressif/esp_lvgl_adapter ==0.6.2`
- `espressif/esp_lcd_touch_gt911 ==1.2.0`
- product `espressif/esp_flash_dispatcher ==1.0.3`

Do not float a board dependency without producing a new exact-head build/package and repeating the physical acceptance that depends on the binary identity.

## Acceptance boundary

Software CI/build/package success is a prerequisite, not physical acceptance. The final candidate must be the immutable package emitted by the exact-head Waveshare candidate workflow and must then pass issue #174 on the actual 800x480 Waveshare 5-inch hardware, including:

- native layout/touch/role/alarm interactions;
- current embedded browser UI behavior;
- backend/history responsiveness;
- resource/stack/DMA/PSRAM stability and absence of WDT/panic/reboot/LVGL failure;
- one uninterrupted `>=4 h` run with `>=240` one-minute soak samples on the same exact image.

Any source/config/binary change after candidate freeze creates a new identity and invalidates prior physical evidence for the affected candidate.

Current lifecycle: **SOFTWARE CANDIDATE INTEGRATION — EXACT-IMAGE PHYSICAL ACCEPTANCE REQUIRED BEFORE PROMOTION**.
