# Waveshare Screen Workspace

Branch: `board/waveshare-esp32-s3-touch-lcd-5`

Purpose: keep all Waveshare 5-inch local screen/HMI work isolated from the site-tested product core.

Rules:
- Use the existing backend/application APIs and authoritative state already provided by the firmware core.
- Do not duplicate backend, control, safety, commissioning, meter/inverter, alarm, auth, or persistence logic here.
- Screen code may present data and later invoke only already-supported actions through existing authorization/safety gates.
- No new product feature/functionality is authorized in the current milestone.
- Board/screen-specific code stays inside this board workspace and must not leak into generic core components.
- Any generic backend bug discovered while integrating the screen must be fixed in the canonical core and then propagated to supported boards.
- Unknown/stale/unavailable backend values must remain visibly unknown; never coerce them to measured zero.
- Source attribution shown to the operator must come from the backend's fail-closed `source.attributed_to` status field.

Current structure:
- `api/` — thin models/parsers for existing backend contracts; no business logic.
- `pages/` — LVGL screen/page composition.
- `components/` — reserved for reusable local UI widgets.
- `assets/` — reserved for board-local UI assets.
- `drivers/` — reserved for display/touch glue only after exact board qualification.
- `SCREEN_TODO.md` — bounded implementation/QA checklist.
- `CMakeLists.txt` — isolated component definition, intentionally not wired into the current default build yet.

Current implemented slice:
- existing `GET /api/live` contract model/parser;
- existing `GET /api/status` contract model/parser;
- read-only LVGL Operator Overview skeleton;
- explicit backend unavailable / stale / unknown handling;
- no control/write callbacks.

Waveshare upstream display baseline located and pinned for qualification at `waveshareteam/ESP32-S3-Touch-LCD-5@a7b179dbfccea8121c88770d8a3c53e5a84b1024`. Its LVGL v9 demo uses LVGL 9, Espressif `esp_lvgl_adapter`, and GT911 support; it is a hardware bring-up reference only, not product logic.

Current status: `IMPLEMENTATION_STARTED — BUILD/HARDWARE_INTEGRATION_PENDING`.
