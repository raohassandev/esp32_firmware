# Deep Code Audit — 2026-07-28

**Scope:** full read of the firmware and embedded web UI at `ed765b0`
(`feature/multibrand-inverter-profiles`), ~15,400 lines across 100+ files.
**Method:** line-by-line review of every C component and every served JavaScript module, with each
headline finding re-verified by hand against the source and the ESP-IDF v6.0.1 headers before
publication. Findings that could not be justified from the code were dropped.
**Nothing was modified, built, flashed, or run on hardware for this audit.** The only repository
change accompanying it is `.gitignore` housekeeping (§8).

**Immediate operational note:** finding **W1** explains the "Page Unresponsive" the operator hit on
the Meters page. It is a one-line-class bug with a large blast radius and should be fixed first.

---

## Severity summary

| # | Severity | Area | Finding |
|---|---|---|---|
| W1 | **Critical** | Web UI | Unbounded MutationObserver loop freezes the browser on any `ESP_ERR_*` |
| C1 | **Critical** | Config | Build provisioning overwrites commissioned Wi-Fi credentials on schema-2→3 migration |
| C2 | **Critical** | Config | Provisioning opt-in flipped to `default y`; code comment still claims "off by default" |
| S1 | **Critical** | Control | NaN meter reading is clamped **up** to full power, not rejected |
| S2 | **Critical** | Inverter | Identity verification latched once; never re-checked after offline→online |
| H1 | **Critical** | Web API | Authentication compiled out — every write/control endpoint is open |
| H2 | **Critical** | Web API | Unbounded JSON nesting → stack overflow → unauthenticated remote reboot |
| H3 | **Critical** | Web API | `malloc` inside a spinlock, up to ~13,000 times per request |
| N1 | High | Network | `NETWORK_WIFI_DISCONNECTED` is a terminal dead state with no retry |
| N2 | High | Network | Two tasks issue concurrent blocking `esp_wifi_scan_start()` |
| N3 | High | Network | `strlcpy` truncates legal 32-char SSIDs / 64-char PSKs by one byte |
| M1 | High | Modbus | No transaction deadline: one slow peer stalls a poll for minutes |
| M2 | High | Modbus | `getaddrinfo` has no timeout and runs on every transaction under two locks |
| I1 | High | Inverter | Capacity snapshot races per-inverter eligibility → fleet can exceed the cap |
| I2 | High | Inverter | Staleness only enforced by the same task that can itself stall |
| I3 | High | Inverter | Profile committed to NVS *before* control is disabled |
| X1 | High | Config | `import_json` bypasses every bound the HTTP API enforces |
| X2 | High | Config | Migration `malloc` failure silently destroys the stored configuration |
| W2 | High | Web UI | Second infinite loop on the Alarms route |
| W3 | High | Web UI | Failed meter reading displayed as a valid `0.00 kW` |
| W4 | High | Web UI | Overlapping 5 s pollers on the most expensive endpoint |

Plus 20 Medium/Low findings detailed in the sections below.

---

## 1. The Meters page freeze (verified end to end)

### W1 — Critical — unbounded MutationObserver feedback loop
`web/engineering-errors.js:20-43` and `:52-64`

```js
function improveError(node) {
    if (!(node instanceof HTMLElement) || node.dataset.errorFriendly === 'true') return;  // :21 guard
    ...
    node.dataset.errorFriendly = 'true';        // :24  flag set on the PARENT only
    node.replaceChildren();                      // :26
    const detail = document.createElement('small');
    detail.textContent = `Diagnostic code: ${translated.code}`;   // :34  re-injects "ESP_ERR_TIMEOUT"
    node.append(title, message, detail);         // :35
}
```

The observer at `:52-64` watches `#mainContent` with `{childList:true, subtree:true}` and calls
`improveError()` on **every** added element. The guard at `:21` is set on the node being
transformed, but the `<small>` created at `:33-34` is a brand-new node with no flag, and its text
still contains the literal `ESP_ERR_TIMEOUT` that `translate()` matches on. So:

`improveError(card)` → appends `<small>ESP_ERR_TIMEOUT</small>` → observer fires → `improveError(small)`
→ appends another `<small>` → observer fires → … unbounded, nesting one level deeper and adding
three elements per iteration.

MutationObserver callbacks are **microtasks**: the queue is drained completely before the browser
renders or accepts input, so the tab locks at 100% CPU and Chrome shows "Page Unresponsive".

**Trigger chain, verified:**
1. `components/web_server/device_api.c:198` — `cJSON_AddStringToObject(runtime, "last_error_name", esp_err_to_name(data.last_error))` emits the literal string `ESP_ERR_TIMEOUT`.
2. `web/devices.js:238,247` — renders it via `metaItem('Last error', errorLabel)` into a meter card appended to `#mainContent` on the Meters route, on load and every 5 s.
3. `components/web_server/web_server.c:110` — `web_assets_engineering_errors_js` **is** in the served bundle, and at `:110` it is concatenated *after* `devices.js` (`:95`) and `em500-core.js` (`:101`), so its observer is armed over their output.

**Consequence:** the moment the grid meter reports any Modbus error — precisely when an operator
opens the Meters page to investigate — the page freezes. `node.replaceChildren()` at `:26` also
destroys the meter diagnostics card content and the EM500 "Retry" control.

The same loop is reachable from the EM500 panels via `em500_api.c:282,331`.

---

## 2. Provisioning regression (verified against sdkconfig)

### C1/C2 — Critical — commissioned Wi-Fi credentials are overwritten on migration

`main/Kconfig.projbuild:28` now reads `default y`. The compiled configuration confirms it is live:

```
CONFIG_PVDG_APPLY_BUILD_WIFI_PROVISIONING=y
CONFIG_PVDG_WIFI_PROVISION_ID=1
CONFIG_PVDG_PRIMARY_WIFI_SSID="Rao"
CONFIG_PVDG_PRIMARY_WIFI_PASSWORD=<12 bytes, development credential>
```

`components/config_manager/config_manager.c:256` — the schema-2→3 migration forces:

```c
loaded->wifi_provision_id = 0;
```

and the monotonic gate at `:143` is:

```c
if ((uint32_t)CONFIG_PVDG_WIFI_PROVISION_ID <= c->wifi_provision_id) return false;
```

With the stored id reset to 0 and the build id at 1, `1 <= 0` is false, so provisioning **applies**.

**Failure scenario:** a device commissioned on site under schema-2 firmware is updated to this
build. On first boot the migration zeroes its provisioning generation, the gate passes, and the
site SSID/password are replaced with the development credentials `Rao` / `password123` and
persisted. The device never rejoins the site network; recovery requires the setup AP or a site
visit. Provisioning also forces `ip_mode = APP_WIFI_IP_DHCP` (`:158`), discarding a commissioned
static-IP assignment.

**Why this matters beyond the bug:** the comment at `config_manager.c:125-128` still states that
build provisioning is "opt-in and off by default" and that "a build from a fresh worktree cannot
disturb a commissioned device". That was true when the safety gate was added; the `default y` flip
has made the comment false while leaving the reassuring text in place. The migration reset is the
enabling condition — a device whose credentials were set by an operator is assigned the *lowest
possible* generation, guaranteeing any non-zero build id wins.

**Minimum fix:** restore `default n`, and carry the stored `wifi_provision_id` across the schema-2→3
migration instead of zeroing it.

---

## 3. Control-path safety

### S1 — Critical — a NaN meter reading is clamped to **maximum** power
`components/control_engine/control_engine.c:19-22`, `components/modbus_tcp/modbus_decoder.c:37,40`,
`components/inverter_manager/inverter_manager.c:379-380`

```c
static float clampf(float value, float low, float high) { return fmaxf(low, fminf(high, value)); }
```

C99 defines `fminf(x, NaN) == x` and `fmaxf(x, NaN) == x` — the NaN operand is discarded. Therefore
`clampf(NaN, 0, total_rated_kw)` returns **`total_rated_kw`**. The clamp, which is the last line of
defence, converts "invalid" into "full power". The identical pattern at `inverter_manager.c:379`
turns a NaN percentage into `maximum_percent`.

The value reaches it unchecked. `modbus_decoder.c:37` does
`memcpy(&value, &raw, sizeof(value))` for `MODBUS_DATA_FLOAT32` and `:40` computes
`value * scale + offset` with **no finiteness test** — while `modbus_decode_u64_be_scaled` directly
below it at `:50-51` *does* check `isfinite`. `meter_manager.c` contains no `isfinite`/`isnan`
anywhere (verified by grep), so at `:218-226` a NaN is stored as `active_power_kw`, stamped with a
fresh timestamp, and marked `online = true`.

`safety_manager.c:25-28` only checks `online` and staleness, so no alarm is raised.
`control_engine.c:39` computes `error_kw = NaN`; `fabsf(NaN) <= deadband` is false, so the deadband
does not absorb it; the integral and request both clamp to `total_rated_kw`.

**Failure scenario:** a FLOAT32-configured meter or ZLAN gateway returns a well-formed frame whose
data registers are `0xFFFF 0xFFFF` (a common "value unavailable" pattern). The frame passes every
MBAP, transaction-id and byte-count check. The controller then ramps the inverter fleet to 100% of
rated output while the true grid import is unknown — reverse power into the generator or export
across the utility boundary, the exact hazard this controller exists to prevent. **The failure
direction is the unsafe one.**

**Minimum fix:** reject non-finite values at `modbus_decode_scaled`, treat them as a decode failure
in `meter_manager`, and make `clampf` NaN-safe.

### S2 — Critical — identity verification never re-runs
`components/inverter_manager/inverter_manager.c:249-256`, `115-142`

`identity_checked` is set true in `verify_identity()` and is never reset — not on read failure, not
on staleness, not on `online = false`, not on reconnect. Meanwhile `modbus_tcp.c:133-139` closes the
TCP connection after **every** transaction, so each poll re-resolves and re-connects to
`endpoint.host`.

**Failure scenario:** a verified inverter powers down; its DHCP lease is reassigned to a different
device (another inverter model, a data logger, any host exposing Modbus). A later poll of the
active-power register succeeds with plausible values, so `poll_active_power` sets
`telemetry_valid/online = true` while eligibility still passes on the **stale** `identity_verified`
flag. The controller then writes power-limit values into an arbitrary holding register of an
unidentified device.

### I1 — High — capacity snapshot races per-inverter eligibility
`inverter_manager.c:358,366-377`. The share denominator `total_rated_kw` is a snapshot taken before
the loop, but eligibility is re-evaluated **inside** the loop, which blocks on a Modbus write per
inverter (up to `timeout_ms` each). If an inverter becomes eligible mid-loop, two 100 kW units can
each be commanded `100 × 100/100` = 100%, delivering 200 kW against a 100 kW cap.

### I2 — High — staleness enforced only by a task that can itself stall
`inverter_manager.c:220-232`. `telemetry_stale` is computed nowhere else, and the command gate reads
a flag that only this sequential, blocking loop maintains. If one inverter's poll blocks (see M2,
`getaddrinfo` with no timeout), later inverters never have their staleness re-evaluated and keep
being commanded from data far older than the stale timeout. Staleness for a safety gate must be
time-derived at the point of use.

### I3 — High — profile persisted before control is disabled
`inverter_profile_store.c:106-127` commits the new profile — a different register map and scaling —
to NVS **first**, then attempts to disable automatic control. A failure or reset between the two
leaves the device booting with the new profile and control still enabled, writing to a new
`power_limit_address` with new scaling that was never validated for that unit. The interlock must
precede the commit.

### I4 — High — raw command silently clamped instead of rejected
`inverter_manager.c:382-399`. No validation that `raw_units_per_percent` is finite and > 0 (the
default profile leaves it 0, yielding `raw = 0` — minimum limit — while reporting 100%); a negative
or NaN product casts through `uint32_t` and is clamped to `0xFFFF`, i.e. **maximum**, rather than
failing; and `profile->power_limit_words` is declared and populated but never used, so a profile
declaring a 32-bit limit register receives half its value.

### Medium/Low (control)
- **I5** `inverter_manager.c:390-413` — command bookkeeping (`commanded_percent`, `has_command`) is updated even when the write failed or was unsupported, producing false readback mismatches and a UI that reports a limit the inverter never received.
- **I6** `control_engine.c:60-62` — ramp state advances on a failed write, so a later successful write jumps by the accumulated difference, defeating the ramp limit.
- **I7** `control_engine.c:15,96` — control config is cached at boot and never re-read. The APIs that clear `control.enabled` in NVS report "control disabled" while the running loop keeps commanding until reboot. A safety interlock that requires a manual restart is not an interlock.
- **I8** `control_engine.c:59-66` — disabling control stops writing but never releases the last limit, leaving inverters latched at (say) 30% indefinitely while the UI shows 0 kW applied.
- **I9** `inverter_manager.c:88` — `(!identity_supported || identity_verified)` is vacuously true for a write-capable profile authored without an identity probe; `inverter_profile_allows_write` does not require one.
- **I10** `safety_manager.c:6,24-29` — `s_alarm_flags` is cleared then repopulated with no lock; a concurrent HTTP reader can observe `0` ("no alarm") during an active curtailment.

---

## 4. Web API and HTTP server

### H1 — Critical — authentication is compiled out
`components/web_server/engineering_auth.c:17,33-43`

```c
#define AUTH_TEMPORARY_FIELD_BYPASS 1
bool engineering_auth_is_authorized(httpd_req_t *request) { (void)request; return AUTH_TEMPORARY_FIELD_BYPASS != 0; }
esp_err_t engineering_auth_require(httpd_req_t *request) { (void)request; return ESP_OK; }
```

**In fairness this is deliberate and documented** — the file header states it is disabled while
commissioning workflows stabilise and "MUST be restored to the production authentication
implementation before any customer, resale or unattended deployment image is released". It is
recorded here because it is an absolute release blocker and because it multiplies the severity of
every other finding: `POST /api/config`, `/api/wifi/config`, `/api/meters/config`,
`/api/inverters/config`, `/api/inverter-profile-assignment`, `/api/system/restart` and the rest are
reachable by any host on the network with a single `curl`.

Related structural defect — **H5 (High)**: `engineering_guard.c:270` routes denial to
`engineering_auth_require()`, which returns `ESP_OK` **without emitting any HTTP response**. Even
after the bypass is removed, a denied request yields a zero-byte `ESP_OK`: the client hangs until
its own timeout while holding one of the server's few sockets. The gateway is currently incapable of
denying anything.

### H2 — Critical — unbounded JSON nesting → remote reboot
`web_server.c:125` sets `config.stack_size = 8192`. The bundled cJSON has `CJSON_NESTING_LIMIT 1000`
and checks it *inside* the recursion, so it recurses ~1000 frames before refusing. Reachable at
`web_api.c:356`, `web_api.c:132`, `meter_config_api.c:275`, `inverter_config_api.c:205`,
`em500_settings_plan_api.c:399`, `inverter_profile_api.c:182,239`.

`curl -X POST http://device/api/config --data "$(python -c "print('['*1200)")"` costs 1,200 bytes of
body and blows an 8 KB stack — an unauthenticated remote reboot loop. Body-size caps do not help,
because nesting depth is cheap. This is the same defect class as the project's earlier
stack-overflow history.

### H3 — Critical — heap allocation inside a spinlock
`operational_api.c:295-306`

```c
portENTER_CRITICAL(&s_lock);
...
for (uint16_t i = 0; i < count; ++i) { ... add_sample_json(items, &ring[index], current); }
add_summary(root, ring, capacity, head, count);
portEXIT_CRITICAL(&s_lock);
```

`add_sample_json` (`:221-232`) calls `cJSON_CreateObject()` plus eight `cJSON_Add*ToObject()`, each a
`malloc` plus a key `strdup`. `portENTER_CRITICAL` disables interrupts on the core.

`GET /api/operator/history?range=24h` sets `count` up to `MINUTE_SAMPLE_COUNT = 1440`, so roughly
**13,000 `malloc` calls execute with interrupts disabled**. `CONFIG_ESP_INT_WDT_TIMEOUT_MS=300`
guarantees an interrupt-watchdog panic. Independently, `malloc` takes the heap mutex — blocking on a
mutex while holding a spinlock is a hard deadlock if contended. The same pattern is at `:364-382`.

### H4 — Critical — automatic control can be enabled by an unauthenticated POST
`web_api.c:126-138` → `config_manager.c:527-537`. `POST /api/config
{"control":{"enabled":true,"kp":1e300}}` is persisted with no bounds (`read_float` has no finiteness
check) and no interlock. Note the inconsistency: `meter_config_api.c:346` and
`inverter_config_api.c:264` deliberately force `control.enabled = false` after a mapping change;
`/api/config` is the hole that re-enables it.

### High/Medium (HTTP)
- **H6** `em500_api.c:534-542` — `?scope=all` performs nine sequential blocking Modbus transactions on the single server task: up to 9 × (5 s mutex wait + configured timeout, max 60 s). One unauthenticated GET can freeze the entire UI for tens of seconds and simultaneously steals `io_mutex` from the meter poll task, ageing the measurement the control loop depends on.
- **H7** Same pattern, smaller: `em500_history_api.c:156`, `em500_settings_api.c:438`, `em500_settings_plan_api.c:241,372`, `inverter_profile_api.c:254`.
- **H8 (Medium)** `CMakeLists.txt:116-121` — `operational_api.c` is deliberately compiled outside the authorization gateway, so `/api/operator/history` and `/api/operator/events` will remain unguarded after H1 is fixed. `engineering_guard.c` needs that exemption; `operational_api.c` does not.
- **H9 (Medium)** `web_api.c:75,111,120` — handlers returning non-`ESP_OK` without emitting a response cause esp_http_server to close the socket with no status line; under heap pressure `/api/status` silently drops instead of returning 500.
- **H10 (Medium)** Slow-POST: every body-reading loop blocks the single task until the receive timeout, a trivial unauthenticated UI denial.
- **H11 (Low)** `engineering_guard.c:293` leaks one `calloc` per route on stop/start; `web_server.c:124` has exactly 32 URIs against `max_uri_handlers = 32` — zero headroom.

**Verified clean in this area** (checked explicitly, no findings): memory leaks, double-free and
use-after-free across all 15 files — every `cJSON_Create*`/`malloc`/body buffer is freed on every
return path; `snprintf`/`strlcpy` bounds; array indices from query/JSON are all range-checked.

---

## 5. Network manager

- **N1 (High)** `network_manager.c:302-306,421-424` — `NETWORK_WIFI_DISCONNECTED` is terminal. Every other branch re-arms `CONNECT_REQUEST_BIT`; this one does not, and the task then blocks on `portMAX_DELAY` with no remaining wakeup source. Reached whenever the fallback AP is disabled and any transient `esp_wifi_*` error occurs. Device is off-network with no recovery AP until power-cycled — and repeats.
- **N2 (High)** `network_scan.c:55` vs `network_manager.c:193` — two tasks can call blocking `esp_wifi_scan_start()` concurrently. The guard at `network_scan.c:222-227` rejects `SCANNING` but accepts `AP_FALLBACK`, which is exactly the state `choose_and_connect()` sets while it scans. One scan fails, or one task frees the driver AP list the other is reading.
- **N3 (High)** `network_manager.c:166-167,230-231` — `wifi_sta_config_t.ssid` is `uint8_t[32]` (verified in `esp_wifi_types_generic.h:554`) and our storage is `char ssid[33]`. `strlcpy(dst, src, 32)` writes at most **31** chars + NUL, so a legal 32-character SSID is silently truncated and never found (reason 201); a 64-character PSK likewise. The log at `:179` prints the untruncated source, so it actively misleads.
- **N4 (Medium)** `:452-458,380` — the gate commits under the lock, releases it, then `begin_operator_reconnect()` re-reads the phase. A `GOT_IP` in that window resets the phase, the already-cleared `connect_request_pending` is gone, and the operator's accepted reconnect is silently dropped. Same read-then-act class as the previously-fixed defect.
- **N5 (Medium)** `:175-176,495-496` — `s_retry_count`, `s_using_fallback`, `s_failed_sweeps` are shared across two tasks on different cores with no lock; a lost `s_retry_count = 0` makes a single transient disconnect drop the device to the setup AP.
- **N6 (Medium)** `:411-419` — up to 240 s of `vTaskDelay` in the manager loop; an admitted operator reconnect can wait four minutes despite a 500 ms drain budget.
- **N7 (Low)** `:649-656` a polling client can hold the admission gate open indefinitely; `:580-600` init leaks the event group/netifs on error and leaves the reconnect API accepting requests nobody services; `:614` `network_manager_get_ip()` returns an unsynchronised pointer into locked state; `network_scan.c:63-71` leaks the driver AP list on the allocation-failure path; `:495` `uint8_t` retry counter wraps if `max_retries_per_profile` is 255 (reachable via `import_json`), making the recovery-AP branch unreachable.

**Verified clean:** no blocking/logging/allocation inside any `portENTER_CRITICAL` in these two
files (all 16 sections checked); no event-group bit lost to clear-on-exit; no buffer overflow.

---

## 6. Meter and Modbus

- **M1 (High)** `modbus_tcp.c:42-52` — `SO_RCVTIMEO` applies per `recv()` and restarts on every byte, with no cumulative deadline. A peer dribbling one byte per `timeout_ms - 1` stretches a single transaction to `259 × timeout_ms` — ~388 s at the 1500 ms default, hours at the API-allowed 60 s. The meter goes offline and PV is curtailed to 0 kW throughout.
- **M2 (High)** `modbus_tcp.c:103` — `getaddrinfo` has no timeout and, because the socket is closed after every transaction, runs on **every poll** while holding both `io_mutex` and `c->lock`. A vanished DNS server blocks the meter task for the full lwIP retry schedule before `timeout_ms` even applies.
- **M3 (Medium)** `modbus_tcp.c:110-115` — `timeout_ms == 0` means "block forever" for `SO_RCVTIMEO` and "return immediately" for `select()`. Not validated here, and `config_manager.c`'s `valid()` does not check it, so a legacy blob carrying 0 is accepted verbatim.
- **M4 (Medium)** `modbus_tcp.c:176-177` — exception responses and garbage both return `ESP_ERR_INVALID_RESPONSE`, discarding `pdu[1]`. Exception 0x02 (illegal address, i.e. wrong register configured) and 0x0B (gateway target failed to respond, i.e. dead RS-485 device) are operationally different faults rendered as one opaque "Modbus protocol error", which stalls commissioning diagnosis.
- **M5 (Medium)** `meter_manager.c:120-132` — the backoff cap is applied to the product rather than the increment, so a meter configured with a 30 s poll interval is polled **every 10 s** once degraded: three times *faster* than when healthy, exactly when the operator asked for less traffic.
- **M6 (Medium)** `meter_manager.c:47-60` — `legacy_em500_scale_fingerprint` silently substitutes scale `0.00001` for a configured `0.01` when a heuristic matches (INT32/ABCD, register 57 or 58). A non-EM500 meter that legitimately matches reads 1000× low, with no runtime indication and no field in `meter_data_t` recording the override; the commissioning wizard cannot detect it either.
- **M7 (Medium)** `modbus_tcp.c:133-139` — closing the connection per transaction makes the ESP the active closer every time, leaving a PCB in TIME_WAIT for 2×MSL (120 s). At 1 Hz per meter across up to 4 meters and 12 inverters this permanently saturates the 16-entry TIME_WAIT pool, forcing lwIP to reclaim PCBs — which can reach HTTP server connections and drop operator UI requests mid-flight.
- **M8 (Low)** `meter_manager.c:272` unclamped `s_meter_count`; `modbus_tcp.c:188,215,237` dereference `c->lock` with no NULL check; `:143` early-returns before `memset`, leaving `socket_fd = 0` (a valid fd) for a caller-zeroed struct; `:117` only tries the first `getaddrinfo` result; `:74` treats `EINTR` as fatal.

**Verified clean:** PDU bounds and hostile-response handling (every length/byte-count path checked —
no overflow is reachable), transaction/unit/protocol-ID matching, FC06/FC16 echo validation, socket
and fd leaks on every error path, non-blocking connect logic, lock ordering, counter/timer wrap.
Stale data is genuinely blocked from producing a non-zero command — the gap is S1, where the sample
is fresh and successful but not a number.

---

## 7. Web UI (beyond W1)

- **W2 (High)** `operator-operations.js:265` — `new MutationObserver(() => setTimeout(enhanceCurrentPage, 10)).observe(mainContent, {childList:true, subtree:true})`, and on the Alarms route `enhanceCurrentPage` does `replaceChildren()` + ~45 appends into that same subtree with no guard. Permanent 100% CPU on `#/alarms`.
- **W3 (High)** `operator-view.js:226-235`, `app.js:67-70` — `meter_manager.c:225` only assigns `active_power_kw` on success and never invalidates it, and it starts at `0.0f`; `web_api.c:97` always emits it as a number. A meter that has never responded therefore displays **"0.00 kW"** with the caption **"Near-zero exchange"**, and `pushTrend` records that fabricated zero every 5 s into a believable flat trend line.
- **W4 (High)** `ui-enhancements.js:37-76` + `em500-core.js:455-460` — two independent 5 s pollers hit the EM500 snapshot endpoint; `checkPowerConsistency` has no timeout and no in-flight guard while `em500-core` budgets 6.5 s for the same call, so overlapping requests accumulate without bound against a server with very few sockets.
- **W5 (High)** `wifi.js:159-188` — the 800 ms scan poll is started unconditionally on any route with no attempt cap, deadline, or route/visibility gate; if the controller is left reporting `running` it polls at 1.25 Hz forever from the Meters page.
- **W6 (High)** `em500-core.js:346-390,462-466` — the tab bar is built once from the `tabs` map, but all assets are concatenated into one deferred `/app.js` and `start()` runs before `em500-profiles.js` and `em500-plan.js` register their tabs. **"Meter profiles" and "CT/PT/tariff plan" — including the 1000× scale-correction control — are unreachable in shipped firmware.**
- **W7 (Medium)** Every poller except `em500-core` uses a boolean in-flight guard with no fetch timeout; one stalled socket latches the guard and that module never updates again (the Meters "Refresh diagnostics" button stays disabled reading "Refreshing…").
- **W8 (Medium)** Aggregate load: sitting on `#/meters` issues ≈14 requests per 5 s from uncoordinated modules, two of them multi-second Modbus reads. `devices-refresh.js:11-13` fires the whole set at once by synthesising a `hashchange`, which also aborts any in-flight EM500 request and surfaces a spurious timeout error.
- **W9 (Medium)** `devices.js:348` — `JSON.parse` on an unchecked body; a truncated response (common under socket pressure) surfaces as "Unexpected end of JSON input" rather than a transport error. `app.js:102-106` handles this correctly.
- **W10 (Low)** `engineering-session-resilience.js` is embedded via CMake but **absent from the served bundle** in `web_server.c:86-115` — its 401-retry/session-keepalive logic never runs.

---

## 8. Repository housekeeping

Untracked content at audit time: `build-idf601/` (1,762 files, 158 MB), `build-tenda/` (1,614 files,
150 MB), `evidence/` (19 files, 2 MB including three flashed `.bin` images), and
`docs/PHYSICAL_QUALIFICATION_REPORT_2026-07-27.md`.

`.gitignore` has been extended in this commit to cover `build-*/` and binary artefacts under
`evidence/`, removing ~308 MB of noise from `git status`. The JSON/log evidence and the
qualification report are left tracked/untracked as-is for their author to decide — they are small
and reviewable, and are not mine to commit.

Credential hygiene check: `evidence/2026-07-27/preflash/api-config.json` contains two masked
password fields and **zero** plaintext credential hits. Note separately that the development Wi-Fi
password is stored in plaintext in `sdkconfig` and is compiled into the flashed image — a
supply-chain exposure rather than a runtime leak, and another reason C1/C2 matter.

The audit was performed on a detached HEAD; it has been anchored to
`audit/deep-code-audit-2026-07-28` at the same commit `ed765b0` so nothing dangles. No existing
branch was moved.

---

## 9. Suggested order of work

1. **W1** — one bug, unfreezes the Meters page. Guard the generated children, or observe a narrower target.
2. **C1/C2** — restore `default n` and preserve `wifi_provision_id` across migration, before any device is updated.
3. **S1** — reject non-finite decodes and make `clampf` NaN-safe. Small change, removes a fail-to-maximum path.
4. **H3, H2** — move allocation out of the spinlock; bound JSON nesting.
5. **H1/H5** — restore authentication and make the guard actually able to deny, before any unattended deployment.
6. **S2, I1–I4** — the inverter-command correctness cluster.
7. **N1–N3, M1–M2** — availability and long-tail stalls.
8. **W2–W6** — remaining UI hangs, the fabricated `0.00 kW`, and the unreachable meter-profile tabs.

Findings are stated with file:line and a concrete failure scenario so each can be confirmed or
rejected on its merits. Where a claim rests on library semantics (C99 `fminf`/`fmaxf` NaN handling,
ESP-IDF field widths, cJSON nesting limits, MutationObserver microtask scheduling) the source has
been cited so it can be checked independently.
