# Waveshare native operational backend parity

Status: implementation staged on `work/waveshare/backend-parity`; physical recovery proof remains required.

## Authority

`components/web_server/operational_api.c` remains the single owner of operational alarm/event semantics. It owns the event ring, alarm lifecycle, acknowledgement state, shelving, suppression-by-design, out-of-service state, causality, priority rationalisation and operator wording.

The following read-only builders are the shared seam:

- `operational_api_build_events_json()`
- `operational_api_build_alarms_json()`

The existing HTTP handlers call these builders and only perform HTTP serialization. The Waveshare native provider calls the same builders in-process and serializes their returned cJSON trees into bounded PSRAM-owned buffers. There is no loopback/self-HTTP path and there is no second alarm/event state machine in the board code.

## Memory/failure policy

The native operational slots are bounded at 49,152 bytes for events and 32,768 bytes for alarms. They are presentation transport buffers, so they are allocated from PSRAM only. If PSRAM cannot provide them, the native read model fails unavailable instead of consuming scarce internal DRAM needed by Product Core, Wi-Fi or HTTP tasks.

`screen_api.c` remains responsible for projecting the full authoritative payload into the LCD's bounded display rows. Unknown/unavailable state must remain unknown; the screen must not fabricate zero alarms or empty event history.

The shared builders still allocate their temporary cJSON tree while a snapshot is produced. The native operations refresh only runs while the Alarms page is active, but exact-board soak evidence must still show that repeatedly opening/holding that page does not cause minimum-heap collapse or fragmentation. If it does, the next optimization is a bounded Core snapshot/projection seam, not a return to duplicate alarm logic.

## Remaining acceptance

Software CI must prove the shared builder ownership, product build and parser contracts at the exact head. Hardware acceptance still requires healthy/offline/stale transitions and loss/recovery to match the web/Core authority on the physical Waveshare board without reset, heap collapse, control starvation or stale UI recovery.