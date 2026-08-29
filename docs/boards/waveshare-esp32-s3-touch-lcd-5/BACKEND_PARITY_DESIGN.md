# Waveshare Native Backend Parity — Design Contract

Lane: #25 / PR #30

Parent: `board/waveshare-esp32-s3-touch-lcd-5`

Status: `IMPLEMENTATION REQUIRED`

## Problem

The native Waveshare product provider already projects `/api/live`, `/api/status`, `/api/meters`, `/api/inverters` and `/api/telemetry` in-process, but it deliberately gives `/api/operator/events` and `/api/operator/alarms` zero-capacity slots. The authoritative event ring and alarm lifecycle table are private inside `components/web_server/operational_api.c`.

The Alarms/Events page therefore cannot reach Core authority. This is a real product gap, not a display placeholder to waive.

## Non-solutions

The following are explicitly rejected:

- same-device HTTP/TCP loopback from the LCD to its own web server;
- reconstructing alarms from `safety_manager` flags in board-local code;
- rebuilding acknowledgement/shelving/suppression/priority/root-cause logic in the screen component;
- treating the persistent alarm journal as the live alarm table;
- returning empty arrays or zero counts when the authoritative state is unavailable;
- changing control/safety behavior to make UI parity easier.

Each of those creates a second authority or hides an unavailable state.

## Approved seam

Refactor the existing operational HTTP serialization into reusable Core-owned builders, then let both transports consume the same result.

Conceptual API (names may be adjusted during implementation without changing the contract):

```c
cJSON *operational_api_build_events_json(void);
cJSON *operational_api_build_alarms_json(void);
```

Requirements:

1. The builders live with the existing authoritative state in `components/web_server/operational_api.c` (or a Core module to which that state is intentionally moved).
2. Existing HTTP `events_get()` and `alarms_get()` become thin transport wrappers around those builders.
3. The Waveshare `local_backend_provider` invokes the same builders and serializes them into its bounded slots; it does not reproduce lifecycle logic.
4. Locking/snapshot semantics remain owned by the Core builder. Native LCD code must never touch `s_events`, `s_alarms` or `s_lock` directly.
5. Existing JSON contracts remain unchanged so the web UI and `screen_api_parse_*` share one schema.
6. Allocation/serialization failure is reported as unavailable. No previous or empty payload is relabeled as fresh.
7. The native provider remains read-only for these surfaces; operator mutation endpoints are outside this lane.

## Event parity acceptance

The native payload must preserve, from the same Core builder used by HTTP:

- sequence and age;
- severity;
- alarm-vs-event kind;
- active/cleared/recorded state semantics;
- title, detail and recommended action;
- summary active-critical, active-warning and stored-event counts;
- newest-first ordering.

## Alarm parity acceptance

The native payload must preserve, from the same Core builder used by HTTP:

- alarm code/id/title;
- severity and rationalised priority;
- present/acknowledged/return-to-normal state;
- primary vs consequential role and `caused_by`;
- stale state;
- shelved, suppressed-by-design and out-of-service facts;
- recommended action;
- active/unacknowledged/primary/consequential summary counts.

The screen parser may intentionally ignore fields it does not display, but the builder must not fork the HTTP contract to accommodate the screen.

## Tests

Before the lane can merge:

- `tests/waveshare_backend_parity_gate.py` turns green: operations are no longer hard-disabled and same-device HTTP is not restored;
- add a host/source contract proving HTTP and native provider reference the same exported builders;
- existing alarm lifecycle, acknowledgement, suppression, priority and journal tests stay green;
- existing Waveshare screen parser tests stay green;
- exact product 800x480 build stays green.

Hardware/HIL closure remains in #27: raise/clear representative conditions, verify LCD vs HTTP/Core state, then exercise loss/recovery without reboot.
