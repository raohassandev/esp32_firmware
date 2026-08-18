# Waveshare Screen Workspace

Branch: `board/waveshare-esp32-s3-touch-lcd-5`

Purpose: keep all Waveshare 5-inch local screen/HMI work isolated from the site-tested product core.

Rules:
- Use the existing backend/application APIs and authoritative state already provided by the firmware core.
- Do not duplicate backend, control, safety, commissioning, meter/inverter, alarm, auth, or persistence logic here.
- Screen code may present data and send only already-supported commands through the existing backend/API contract.
- No new product feature/functionality is authorized in the current milestone.
- Board/screen-specific code stays inside this board workspace and must not leak into generic core components.
- Any generic backend bug discovered while integrating the screen must be fixed in the canonical core and then propagated to supported boards.

Planned contents:
- `pages/` — screen/page composition
- `components/` — reusable local UI widgets
- `assets/` — board-local UI assets
- `drivers/` — display/touch glue only where required by this screen target
- `api/` — thin client/adapters to existing backend APIs; no business logic

Current status: workspace created; implementation not started.
