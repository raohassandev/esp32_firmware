# Wi-Fi Reconnect Admission Gate

## Purpose

`POST /api/wifi/rescan` runs over the same station connection that the operation
will intentionally disconnect. The controller must therefore finish every
admitted `202 Accepted` or `409 Conflict` response before the Wi-Fi manager is
allowed to touch the radio.

A fixed delay from request arrival is not sufficient. Hardware qualification
showed response latency approaching that deadline while a scan was active, and
a first completion-aware implementation still had a read-then-act race at the
final gate decision.

## State machine

The manager uses four synchronized phases:

- `IDLE`: no operator reconnect is pending.
- `DRAINING`: accepted and conflicting HTTP handlers may be admitted and are
  counted. The phase cannot advance until every admitted handler has completed
  and 500 ms has elapsed since the latest completion.
- `QUIESCING`: no handler is currently in flight. A second 500 ms admission-quiet
  interval runs. A request entering during this phase atomically reopens
  `DRAINING` and receives a tracked `409` response.
- `DISCONNECTING`: the admission gate is closed and the existing manager task is
  committed to the radio transition.

All phase changes, request admission and in-flight accounting use the same
`portMUX_TYPE` lock. At the final boundary, either the HTTP handler acquires the
lock first and reopens the drain, or the manager acquires it first and closes the
gate. There is no unsynchronized read-then-act window.

## HTTP ordering contract

Every admitted handler follows this order:

1. `network_manager_operator_reconnect_response_begin()`
2. Select `202` or `409`
3. Send the complete JSON response
4. Capture the send result
5. `network_manager_operator_reconnect_response_complete()`
6. Return the original send result

No `esp_wifi_*` call is permitted in the HTTP handler or either response API.
The existing Wi-Fi manager task remains the sole radio-transition owner.

## Radio-transition contract

Only `DISCONNECTING` may stop a scan or disconnect the station. The validated
single-owner behavior remains in force:

- the intentional disconnect event is acknowledged without consuming retries;
- the event handler does not switch profiles or start the recovery AP;
- exactly one normal connection-selection wakeup follows;
- spontaneous disconnect retries and genuine recovery-AP behavior remain
  unchanged.

A pending response drain is not itself an intentional disconnect. This matters
when an unrelated connection attempt fails while an HTTP response is still
being delivered.

## Host-side checks completed

The branch was checked with:

- C syntax compilation against ESP-IDF interface stubs;
- direct tests of the production gate functions by including
  `network_manager.c` in a host test translation unit;
- late-request reopening of the full drain and quiet intervals;
- five concurrently admitted handlers blocking progress until the last
  completion;
- exact final-boundary serialization;
- `TickType_t` wraparound timing;
- static checks that the web handler contains no radio calls and preserves
  begin → send → complete ordering.

These checks do not replace an ESP-IDF build or physical-board qualification.

## Required hardware qualification

Before propagation into Batch 2, validate on the ESP32-S3 with the production
`sdkconfig`:

- ESP-IDF v6.0.1 build and flash without NVS erase;
- 10 single reconnects;
- 50 simultaneous two-socket reconnect cycles;
- 20 late duplicates at +300 ms;
- 30 late duplicates at +450 ms;
- 30 five-socket cycles;
- timing proof of at least 500 ms response drain plus 500 ms admission quiet;
- 100 active-scan/reconnect race cycles;
- 5 browser reconnect cycles;
- zero recovery-AP activations, duplicate reconnects, panics, watchdogs or
  unexpected resets;
- final `/api/config` byte-identical, meter address 58 unchanged, control and
  inverter disabled.

Do not mark the branch or Batch 2 ready based only on host-side checks.
