# ChatGPT execution brief — Automatrix PV-DG Controller

This is the authoritative instruction set for the remaining implementation phases.
Read it fully before writing any code. It is written to be unambiguous; where something is
genuinely undecided it says so explicitly rather than leaving you to guess.

**Last updated:** 2026-07-29
**Working branch:** `dev`
**Verified baseline:** `9754c8c` — built, flashed to real ESP32-S3 hardware, and boot-verified.

---

## 1. Who does what

| Role | Owner | Scope |
|---|---|---|
| Code implementation | **You (ChatGPT)** | All firmware, web and test code for the phases below |
| Hardware verification | **Claude Code** | Builds, COM5 flashing, serial capture, live device tests, evidence |
| Product decisions, credentials, manufacturer documentation | **The user** | Anything requiring physical access or a commercial decision |

You do **not** have access to the controller, a serial port, or the network it sits on.
Never claim a change is verified on hardware. Say "requires hardware verification" and hand it over.

**OTA is out of scope.** It is being implemented separately on `feature/secure-web-ota`.
Do not edit OTA code, and do not branch from that branch.

---

## 2. Repository rules

- Base every change on `dev`. Create `feature/<topic>` or `fix/<topic>` off `dev`.
- `main` tracks `dev` and represents tested-but-not-production-qualified work. Do not commit to it.
- Rebase onto `dev` before handing work back. Do not merge `dev` into your branch repeatedly.
- **This repository is public.** Never commit a password, PSK, token, serial setup code, or any
  real credential. `web/product-mode.js` has `DEV_DEFAULT_ENGINEERING_PASSWORD`; it must stay `''`
  in every commit.
- CI (`.github/workflows/esp-idf-build.yml`) runs on `main`, `dev`, `feature/**`, `fix/**` and all
  pull requests. It must be green. It fails on **any** compiler warning.

### The verification suite you must keep green

```
node --check on every web/*.js and tools/*.js listed in the workflow
node web/tests/*.test.js
node tools/soltrix_modbus_simulator.js --self-test && node tools/soltrix_modbus_simulator_test.js
gcc -std=c11 -Wall -Wextra -Werror  (host C unit + integration tests)
python3 tests/*_source_contract.py  (37 contracts)
python3 tests/production_release_gate.py
idf.py set-target esp32s3 && idf.py build     # zero warnings, produces build/automatrix_pvdg.bin
```

**Every phase must add or extend contract tests.** A phase with no new test coverage is not done.
The contracts are the project's real specification — they are what stops a regression shipping.

---

## 3. NON-NEGOTIABLE safety rules

These come from the system owner. They are not style preferences.

1. Never run or script `idf.py erase-flash`, `esptool erase_flash`, `nvs_flash_erase`, or any
   whole-flash erase.
2. Preserve commissioned NVS, Wi-Fi credentials and existing configuration. Schema changes must
   **migrate**, never reset.
3. Automatic control stays disabled unless commissioning prerequisites are explicitly satisfied.
4. Never issue a physical inverter write through an unqualified manufacturer profile.
5. Huawei, GoodWe, Solis, FoxESS/Knox and every other profile stay **fail-closed** until exact
   manuals, model mappings, simulator evidence, bench tests and physical readback qualification exist.
6. A passing build is **not** physical qualification. Never report production completion while
   FAT/SAT or hardware tests are incomplete.
7. HTTP handlers must not perform blocking Modbus transactions. Use the background acquisition
   queue and cached responses.
8. Browser `AbortController` cancels only the browser request. It does **not** cancel an ESP-side
   Modbus transaction. Never claim otherwise in code comments or UI text.
9. **Do not invent** any register address, inverter command, breaker indication, ATS state,
   generator feedback, timing value or manufacturer protocol detail. If it is not in a manual you
   were given, it does not exist. Leave the path fail-closed and say what documentation is needed.
10. Do not copy register addresses from SolTrix or any third-party source into production profiles.

---

## 4. Current state you must build on

### 4.1 What already exists and works

- Config persistence with schema versioning and NVS migration (`components/config_manager`).
- Modbus TCP client with bounded cumulative deadlines and non-finite rejection.
- Meter manager with a bounded background read-job queue.
- Control engine with fail-closed gating, grid evidence, ramp and deadband policy.
- `source_mode` policy layer: `SOURCE_MAX_GENERATORS 3`, `generator_channel_evidence_t`,
  `source_mode_aggregate_generators()`, `source_mode_generator_safe_pv_kw()`,
  `SOURCE_MODE_GENERATOR_ONLY`, `SOURCE_MODE_GRID_GENERATOR_SYNC`. **Unit-tested and sound.**
- Production engineering authentication with PBKDF2, lockout, and session cookies.
- Web UI with operator/engineering separation.

### 4.2 The two gaps that define Phases 2 and 3

**Gap A — meters have no role.** `meter_config_t` in
`components/config_manager/include/config_types.h` has no `role`/`type` field. The control engine
identifies the grid meter purely by array position:

```c
/* components/control_engine/control_engine.c */
bool have_grid = meter_manager_get_data(0, &grid);   /* index 0 IS the grid meter */
```

The `name` field is never compared anywhere — it is a display label only. Reordering the meter
array would silently change which physical instrument the control loop regulates against.

**Gap B — generator evidence is hardcoded false.** The policy layer is real, but nothing feeds it:

```c
/* components/control_engine/control_engine.c */
.generator_running = false,
.generator_breaker_closed = false,
.grid_generator_synchronized = false,
```

So `source_mode` can never observe a generator. This is why the controller logs
`Automatic Solar-Grid control remains fail-closed: explicit grid availability and breaker
evidence are not configured` on every boot. That message is correct and honest — do not silence it.

### 4.3 Hard-won gotchas — read these, they have each caused a real failure

- **`httpd_resp_set_hdr()` stores the pointer and does not copy.** A stack-local header buffer is
  dangling by the time the response is sent. This produced an empty `Set-Cookie` that made
  engineering authentication impossible while login still reported success. Fixed in `9754c8c`;
  do not reintroduce the pattern anywhere.
- **`portENTER_CRITICAL` disables interrupts.** No `ESP_LOG*`, no `malloc`, no `cJSON_*`, no
  blocking calls inside a critical section. Build JSON outside the lock.
- **`EMBED_TXTFILES` appends a NUL terminator.** Serving `end - start` bytes ships a trailing NUL
  and corrupts the asset. Use `end - start - 1`.
- **C99 `fminf`/`fmaxf` discard NaN.** `clampf(NaN, lo, hi)` returns `hi` — fail-to-maximum, the
  worst possible direction for a power command. Guard with `isfinite()` before clamping.
- **`xEventGroupWaitBits` with `pdTRUE` clears every waited bit,** not just the one that fired.
- **MutationObserver callbacks are microtasks.** They drain before render; a self-triggering
  observer freezes the page. This caused a real "Page Unresponsive" fault.
- **`wifi_sta_config_t.ssid` is `uint8_t[32]` and is not NUL-terminated.** `strlcpy` with `sizeof`
  truncates a legitimate 32-character SSID.
- **Modbus PDU addressing is zero-based**; FUXA and many vendor documents use 1-based display
  addresses. State which convention a profile uses, every time.

---

## 5. Phases

Work them in order. Phase 3 depends on Phase 2. Phase 4 depends on Phase 3.

### Phase 1 — Endurance and stability

**Goal:** prove the controller is stable under sustained Modbus polling and browser load.

- Add a soak-test harness driving the simulator over many hours: continuous meter polling plus
  repeated browser-style API polling.
- Instrument and expose: free heap, minimum-ever free heap, per-task stack high-water marks,
  Modbus success/failure counters, HTTP handler durations.
- Assert no monotonic heap decline, no stack high-water approaching zero, no handler blocking.

**Acceptance:** a documented long-run result with resource trend data, plus a contract test that
the telemetry fields exist and are bounded. Hardware soak is Claude Code's to run — you deliver
the harness and the instrumentation.

### Phase 2 — Solar + generator configuration model (up to 3 generators)

**Goal:** close Gap A and give generators a first-class configuration representation.

Required:
1. Add an explicit role to `meter_config_t` — e.g. `METER_ROLE_GRID`, `METER_ROLE_GENERATOR`,
   `METER_ROLE_LOAD`, `METER_ROLE_PV`, with `METER_ROLE_UNASSIGNED` as the default.
2. Add a generator index for generator-role meters (0..2, `SOURCE_MAX_GENERATORS` is 3).
3. Replace `meter_manager_get_data(0, ...)` with **role-based lookup**. Exactly one enabled meter
   may hold `METER_ROLE_GRID`; reject a configuration with zero or multiple grid meters, and keep
   control fail-closed while the assignment is invalid.
4. Add generator configuration: rated kW, minimum loading percent, and the evidence source for
   running/breaker/synchronised — with the evidence source defaulting to "not configured".
5. **Schema migration.** Bump the config schema version. Existing deployed configuration must
   migrate cleanly: an existing single meter at index 0 becomes `METER_ROLE_GRID`. Never reset
   configuration, never clear Wi-Fi credentials. There is a real commissioned unit in the field.
6. Extend `/api/meters/config` and `/api/config` for the new fields, keeping operator-view
   redaction intact.

**Acceptance:** new contract tests covering role validation, the exactly-one-grid rule, migration
from the current schema, and fail-closed behaviour on an invalid assignment.

**Do not** infer role from the meter name string.

### Phase 3 — Live generator source engine

**Goal:** close Gap B by feeding real evidence into the existing `source_mode` policy layer.

- Build the plumbing from configured evidence sources into `generator_channel_evidence_t` and on
  into `source_mode_aggregate_generators()`.
- Apply `source_mode_generator_safe_pv_kw()` so PV is curtailed to respect generator minimum
  loading when generators carry the load.
- Add staleness handling: evidence older than its timeout must be treated as absent, not as `false`.

**Critical constraint.** Breaker position and grid/generator synchronisation **cannot be derived
from a power measurement**. Non-zero kW does not prove a breaker is closed. No combination of
power readings proves synchronisation. These require real indications — dry contacts, or
documented registers from the actual genset controller and breaker.

**As of this writing that documentation has not been supplied.** Therefore:
- Implement the full plumbing, configuration surface and policy wiring.
- Leave the evidence inputs **fail-closed and clearly reported as unconfigured**.
- Write down precisely which document and which signal you need.
- Do **not** invent register addresses or infer breaker state from power. A plausible guess here
  can close a breaker onto an unsynchronised source.

### Phase 4 — Generator transitions

**Goal:** safe behaviour across source changes: grid→generator, generator→grid, generator start
and stop, and synchronised operation.

- Define the state machine explicitly, with a fail-closed state for every unknown or stale input.
- Enforce dwell/stability timers so a flapping input cannot chatter the PV command.
- On any transition into an unknown state, command PV to a safe value immediately.

**Blocked on Phase 3 evidence.** Until real indications exist, deliver the state machine plus
exhaustive host-side unit tests against synthetic evidence. Do not enable it on hardware.

**All timing values must come from documentation or the user.** Do not invent dwell times,
ramp rates or synchronisation windows.

### Phase 5 — Simulator and automated tests

**Goal:** make Phases 2–4 testable without hardware.

- Extend `tools/soltrix_modbus_simulator.js` to model up to 3 generator meters plus a grid meter,
  with scriptable scenarios: generator start/stop, breaker open/close, grid loss and return,
  stale/absent evidence, and Modbus faults (timeout, exception codes 0x02/0x04/0x0B).
- Add host C integration tests covering the full evidence → source mode → power policy chain.
- Every fail-closed path needs a test proving it fails closed.

### Phase 6 — Web commissioning and diagnostics

**Goal:** make the new model configurable and observable by a commissioning engineer.

- Meter role assignment UI with live validation of the exactly-one-grid rule.
- Generator configuration UI.
- A diagnostics view showing, per source: evidence state, freshness, why control is blocked.
- The "why is control disabled" reason must be shown verbatim from the firmware, not re-worded in
  the browser.

Respect rule 7: no blocking Modbus from an HTTP handler. Respect rule 8 in any UI copy about
cancelling requests.

### Phase 7 — Inverter qualification

**Blocked on documentation.** For each manufacturer, qualification requires: the exact manual, the
model-specific register map, simulator evidence, a bench test, and physical readback verification.

Until all five exist for a given model, its profile stays `simulator_only` and fail-closed. Do not
promote a profile to production-approved to make a test pass.

---

## 6. How to hand work back

For each phase, report:

1. **What changed** — files and the reasoning, not a diff dump.
2. **Test results** — the actual command output for the suite above. If you could not run
   something, say so plainly; do not infer a pass.
3. **What is NOT verified** — specifically anything needing hardware.
4. **What you need** — documentation, decisions or credentials that are blocking you.
5. **Safety statement** — confirm no invented register/timing/protocol values, and that control
   remains fail-closed where evidence is unavailable.

If you believe a requirement in this brief is wrong, say so and explain why **before**
implementing it. A wrong requirement caught early is cheap; a plausible-looking guess wired into
a power controller is not.
