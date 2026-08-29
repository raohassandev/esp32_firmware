# Waveshare native operational backend parity

Status: implementation active on `work/waveshare/backend-parity`; the dedicated software parity gate has passed on the shared-builder architecture. All source contracts that inspect the read-side event/alarm payload now follow the shared builders rather than the thin HTTP wrappers, while retaining their existing lock, lifecycle, shelving, suppression, causality, priority and alarm-rate assertions. The Waveshare screen contract also follows the shared source-detection status boundary: board code consumes `source_detection_get_status()` and its authoritative `attributed_to` result rather than re-evaluating source evidence. Full exact-head CI and physical recovery proof remain required. This lane stays Draft until those gates are current at the same head.

## Authority

`components/web_server/operational_api.c` remains the single owner of operational alarm/event semantics. It owns the event ring, alarm lifecycle, acknowledgement state, shelving, suppression-by-design, out-of-service state, causality, priority rationalisation and operator wording.

The following read-only builders are the shared seam:

- `operational_api_build_events_json()`
- `operational_api_build_alarms_json()`

The existing HTTP handlers call these builders and only perform HTTP serialization. The Waveshare native provider calls the same builders in-process and serializes their returned cJSON trees into bounded PSRAM-owned buffers. There is no loopback/self-HTTP path and there is no second alarm/event state machine in the board code.

The migrated source contracts still require the event ring/alarm table to be snapshotted under the operational lock, require cJSON/allocation work outside critical sections, keep cause-attributed/shelved/suppressed alarms visible, and compute rate/priority evidence from the same Core state. Only the inspected read ownership boundary changed when `events_get()` and `alarms_get()` became wrappers.

Source attribution follows the same rule: `source_detection_attributed_to()` remains the Core fail-closed decision (configured + fresh + non-conflicting evidence), while the native provider only consumes the `source_detection_status_t` snapshot and renders its `attributed_to` result. The board does not implement a second attribution rule.

## Memory/failure policy

The native operational slots are bounded at 49,152 bytes for events and 32,768 bytes for alarms. They are presentation transport buffers, so they are allocated from PSRAM only. If PSRAM cannot provide them, the native read model fails unavailable instead of consuming scarce internal DRAM needed by Product Core, Wi-Fi or HTTP tasks.

If an authoritative payload exceeds its bounded native slot, `cJSON_PrintPreallocated()` failure invalidates that LCD read model. The provider must not truncate the JSON into a syntactically-valid but incomplete alarm/event picture. Unknown/unavailable remains preferable to a believable partial state.

`screen_api.c` remains responsible for projecting the full authoritative payload into the LCD's bounded display rows. Unknown/unavailable state must remain unknown; the screen must not fabricate zero alarms or empty event history.

The shared builders still allocate their temporary cJSON tree while a snapshot is produced. The native operations refresh only runs while the Alarms page is active, but exact-board soak evidence must still show that repeatedly opening/holding that page does not cause minimum-heap collapse or fragmentation. If it does, the next optimization is a bounded Core snapshot/projection seam, not a return to duplicate alarm logic.

## Remaining acceptance

Full software CI must prove shared builder ownership, product build, parser contracts, source-attribution ownership and the complete pre-existing operator/alarm safety suite at the exact head. Hardware acceptance still requires healthy/offline/stale transitions and loss/recovery to match the web/Core authority on the physical Waveshare board without reset, heap collapse, control starvation or stale UI recovery.