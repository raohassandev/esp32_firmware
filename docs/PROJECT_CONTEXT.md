# Project context — Automatrix PV-DG Controller

**Read this file first.** It is written for one purpose: an engineer or an AI
assistant opening this repository on a later day, in a fresh session, with no
memory of anything, should be able to read this one file and understand what the
product is, what is true about it today, what has been proven on real hardware,
and what must never be done to it.

Everything here was either measured on the physical plant or read out of the
source. Where something is believed but not proven, it says so. Nothing in this
file is an aspiration.

**Last updated:** 2026-08-11, branch `phase1-fix`.

---

## 1. What the product is

An **ESP32-S3 industrial controller** that keeps a solar plant from pushing power
back into the grid or overwhelming a diesel generator.

The whole loop is three steps:

1. Read the **active power** at the grid (or generator) from a Modbus energy meter.
2. Work out how much PV the plant may produce right now, as a **percentage**.
3. Write that percentage to the **inverters** over Modbus TCP.

Everything else in the repository — the web UI, the profiles, the alarms, the
commissioning flow — exists to make those three steps safe, visible and provable.

Physical facts:

- ESP32-S3, 16 MB flash, built with **ESP-IDF v6.0.1**
- Talks **Modbus TCP** only. RS-485 devices are reached through a TCP↔RTU gateway.
- Serves its own web UI **out of firmware** — there is no separate web server.
  Everything in `web/` is embedded into the binary at build time.
- Repository: `https://github.com/raohassandev/esp32_firmware` — **PUBLIC**.

---

## 2. How to build, flash and check

```
# from the repository root, in PowerShell
. C:\Espressif\frameworks\esp-idf-v6.0.1\export.ps1
idf.py build
idf.py -p COM5 flash
```

Verification, all of which must pass before anything is committed:

```
python tests/<name>_source_contract.py     # 89 of them; run them all
node tools/render_check.js                 # every UI route renders
node tools/browser_check.js                # every route in a real browser, no console errors
node tools/check_asset_order.js            # nothing in web/ orphaned, load order intact
```

The `tests/*.py` files are **source contracts**: they read the source and assert
that a specific decision is still made in it, with the reason written out in the
docstring. They are not unit tests. When you change behaviour deliberately, the
contract that guarded it must be updated deliberately too — and a contract is
only worth anything if you have checked it **fails** when you break the thing it
guards. Mutate the source, watch it fail, restore, watch it pass.

---

## 3. Absolute rules — these are the owner's, and they hold

1. **Never erase flash.** Not `idf.py erase-flash`, not `esptool erase_flash`,
   not `nvs_flash_erase`, not any whole-flash operation. The board carries
   commissioned configuration and Wi-Fi credentials that are not reproducible.
2. **Never invent a register.** Not an address, not a scaling factor, not a word
   order, not a command value, not a breaker or ATS or generator signal. If it is
   not in a manufacturer manual or proven on the physical machine, it does not go
   in. Do not copy addresses from third-party projects or simulators.
3. **A passing build is not qualification.** Neither is a passing contract.
   Physical means: it was flashed to the board and the machine's own behaviour was
   observed.
4. **Never claim physical validation you did not perform.**
5. **HTTP handlers must never block on Modbus.** Acquisition happens on background
   tasks; handlers read cached state. A browser `AbortController` cancels the
   browser's request only — it does not cancel an ESP-side Modbus transaction.
6. **Never commit a credential.** The repository is public.
7. **Do not report the product as complete** while FAT/SAT or hardware tests
   remain undone. They do — see §10.

Credentials for the physical hardware are in `docs/LOCAL_CREDENTIALS.md`, which
is git-ignored and exists only on the owner's machine. If you are in a fresh
clone and that file is absent, ask the owner rather than guessing.

---

## 4. The hardware, as commissioned

Two sites have been used. Which one is live depends on where the board is.

**Lab / office**

| What | Where |
|---|---|
| Controller | `192.168.100.14`, SSID `Automatrix-4G` |
| EM500 energy meter | `192.168.100.200:502`, unit 1 |
| Inverter endpoint | `192.168.100.5:502` — **Sungrow at unit 22** (a Huawei previously sat at unit 21) |

**Site**

| What | Where |
|---|---|
| Controller | `192.168.0.150`, SSID `Solar` |
| Meter **and** Solis inverter | **one** gateway at `192.168.0.200:502` — meter unit 1, inverter unit 21 |

The board also raises a recovery access point at `192.168.4.1` when it cannot
join a network.

The site case — two devices behind one gateway — is the one that broke things.
See §6.

---

## 5. Measured numbers you can rely on

These were measured, not estimated. They are the basis for the timeouts and
cadences in the firmware, so do not change those without re-measuring.

- **A Modbus transaction costs `31 ms + 2.1 ms × registers`.** From reads of 2,
  10, 40 and 72 registers taking 35, 47, 111 and 183 ms.
- **The board answers a small HTTP request in 84 ms** (59 min, 202 max).
- One browser tab holds **7** established sockets; two tabs hold 10. The socket
  ceiling is `max_open_sockets = 10` with `lru_purge_enable = true`, which is why
  a fourth client still succeeded 20/20. **Polling is fine. A WebSocket was built
  and then removed** — it was not needed, and the reasoning that said otherwise
  was wrong in both directions before it was measured.

Cadences as they stand:

| Path | Period |
|---|---|
| `/api/live` (the fast endpoint) | **500 ms** |
| Control decision floor | **1000 ms** (`CONTROL_MIN_DECISION_INTERVAL_MS`) |
| Inverter acquisition loop | 100 ms idle (`INVERTER_TELEMETRY_IDLE_MS`) |
| `/api/status` and most operator polls | 10 s |
| Product view rebuild, `/api/inverters` | 15 s |

`/api/live` is a ~400 byte frame carrying only the numbers that move: grid kW,
solar kW, per-inverter kW, requested and applied PV, the commanded percentage and
whether it is in force, the source, and the inhibit reason. Everything slow-moving
stays on the slow endpoints. **The browser must merge every fast field it
receives** — when `solar_kw` was published but not merged, production appeared
10–15 seconds late and the fault was invisible.

---

## 6. The two hardest bugs, and why they matter

Both were found by measuring the plant, not by reading the code. If something
behaves impossibly, measure before theorising.

### Gateway response crossing

Behind a TCP↔RTU gateway, **every device shares one RS-485 bus**. The Modbus
client held a lock per *connection*, which serialised nothing, so the meter's
reply could be handed to the inverter's request. At the site the meter qualified
**0 out of 3** and inverter writes failed.

Fix: a per-gateway mutex table in `components/modbus_tcp/modbus_tcp.c`, keyed on
host and port, taken **after** the connection lock and released **before** it.
Result: meter 0/3 → **14/14**, and writes began succeeding.

`modbus_tcp_get_last_exception()` deliberately does **not** take that lock.

### Control-loop oscillation

The loop ran at ~50 Hz on data that arrived at 1 Hz. The integral wound up and PV
slammed between 0 % and 100 %. It looked like a faulty meter; with control off the
meter read a steady 50.0 kW at 231 V / 77.4 A. **The controller was shaking the
plant.**

Fix, in `components/control_engine/control_engine.c`: step the policy only on a
measurement not acted on before, with a 1 s decision floor. Grid then settled to
±3 kW.

The same file also computes the command **while automatic control is off**, using
commissioned capacity with the integral held at zero, so the screen can show what
would be sent before anything is armed.

---

## 7. Inverter profiles — what is real

`components/inverter_manager/inverter_profiles.c`. Eleven profiles. The suffix in
the id is the honest part: `.documented` means transcribed from a manual,
`.pending` means it is not qualified.

```
huawei.sun2000.pending          goodwe.commercial.pending
solis.commercial.pending        growatt.tl3x.documented
growatt.tlx.documented          sungrow.string.documented
chint.cps.sch100_125ktl.documented   foxess.commercial.pending
knox.aiswei.asw.documented      solaredge.terramax.documented
huawei.smartlogger.plant
```

**Proven on physical hardware:**

*Sungrow* (the machine currently attached, unit 22, 80 kW):
- Active power **5031**, FC 04, U32, word order **AB**, ×0.001 → kW.
  The manual's own −1 rule does **not** apply here. `5030` with `BA` gave a
  constant `[0,1]` — a steady 65.5 kW while the machine actually made 78–82 kW.
  Found by scanning 5000–5139 and watching which register tracked reality.
- Power limit write **5007**, FC 06, **0–1000** meaning 0.0–100.0 %.
- Readback **5007**, FC 03 — and it confirms. The board logs
  `write verdict: confirmed (evidence setpoint_readback)`.
- **No prerequisite enable register.** Register 5006 returns exception `0x02`
  (Illegal Data Address) on function `0x86`. The gate was removed only after the
  device itself refused it and the owner wrote 5007 successfully with Modbus Poll.
- The identity probe returns 0 — no nameplate. Do not gate on it.

*Solis*: registers need **−1** for FC 03/06/10 and FC 04 (manual §5.6/§5.3) but
**not** for 35000 (§5.2). Reads on FC 04. `min_command_interval_ms = 300`. Its
identity register returns 0, so the identity probe is disabled — an earlier probe
expecting 1020 would have blocked a producing inverter.

Growatt ×2, Chint/CPS, Knox/AISWEI and SolarEdge still require a prerequisite
enable register. Six profiles have no write rate limit. Eight of eleven cannot
verify device identity. **Two inverters have never been driven simultaneously.**

Modbus exception codes seen in practice: `0x02` illegal data address, `0x0B`
gateway target device failed to respond. An exception response sets the function
byte to `function | 0x80` — comparing against the plain function code silently
never matches, which is a bug that has already been made once here.

---

## 8. Meters and source detection

The meter is an **EM500** (Lovato DMG610-compatible clone), unit 1, FC 03.
Per-phase voltage sits at `0x0014` / `0x0016` / `0x0018`. Register `0x2160` — the
single-input source register an earlier specification assumed — **does not exist
on these units**.

Source detection follows the owner's rule exactly, and it is not configurable:

> **One meter → tariff decides. More than one → the declared roles decide.**

There is no "1 meter / 2 meter" option in the UI; offering it was wrong, because a
site syncing solar with a single *generator* would have been forced to call it a
grid meter. The single answer lives in `config_manager_role_assignment()` in
`components/config_manager/config_manager.c`; if exactly one meter is declared and
its role is generator, it becomes the supply meter. `components/source_detection/`
counts the topology rather than being told it.

---

## 9. How the code is laid out

15 components under `components/`. The ones that carry the product:

| Component | What it owns |
|---|---|
| `modbus_tcp` | The transport, and the per-gateway serialisation (§6) |
| `meter_manager`, `meter_profiles` | Meter acquisition and register maps |
| `inverter_manager`, and `inverter_profiles.c` | Inverter acquisition, the write path, write confirmation |
| `control_engine` | The decision: grid reading → requested PV kW |
| `source_detection` | Grid or generator, from roles |
| `config_manager` | NVS-backed configuration and role assignment |
| `commissioning_gate` | What must be true before control may be armed |
| `web_server` | Every `/api/...` endpoint, plus the engineering auth gate |

`components/web_server/engineering_guard.c` deserves a warning. It re-projects
several endpoints for users who are **not** logged in as engineering. The gate
exists to withhold *how the firmware talks to a machine* — register addresses,
function codes, raw words. It must **not** withhold plant facts. It once printed
`command_tested: 0` and `last_write_ok: 0` as **literal zeroes**, so the screen an
owner actually looks at said no command had ever been proven while the machine was
confirming every setpoint. If a number there looks impossible, check whether it is
even being measured.

The web UI is ~37 plain JavaScript modules in `web/`, no build step, no framework.
`web/app.js` polls and republishes; `web/operator-view.js` renders the operator
product; `window.AutomatrixLive` and the `amx-controller-status` event carry the
fast frame to the renderers.

---

## 10. What is not done

Be honest about this list. It is the difference between "the code works" and "this
can be sold".

- **FAT and SAT have never been run.** No soak test either.
- **Two inverters have never been commanded at once.**
- **No RTU-gateway path other than the two above has been exercised.**
- **Unexplained:** in grid mode PV has been seen to fall from 78 kW to 0.6 kW. It
  was never diagnosed. If you see it, capture it rather than explaining it away.
- The documentation set was deleted on 2026-08-11 at the owner's instruction —
  operator manual, FAT/SAT procedures, register evidence, handover pack. All are
  recoverable from git history (`git log --diff-filter=D -- docs/`). Roughly 55
  code comments still cite those files by name. The register provenance in
  particular was the record proving rule §3.2 had been followed.

---

## 11. How to work on this

The habits that actually caught things here, in the order they mattered:

1. **Measure before changing.** Every real fix in §6 and §7 came from a
   measurement that contradicted a confident theory. Several of my own
   explanations were wrong — the meter was blamed for the controller's
   oscillation, the Modbus client was blamed for my own probe's interference,
   and a port was declared dead because I probed the wrong one.
2. **Never swallow an error.** A `catch {}` in `applyLive()` hid a
   `ReferenceError` while the endpoint was polled twice a second for ten minutes
   doing nothing, and the screen looked plausible the whole time. Silent failure
   is the single most common bug class in this repository — `poll_readback()`
   returned its error to a caller that discarded it, which is why a working write
   reported zero confirmations for weeks.
3. **A diagnostic that fires constantly is not a diagnostic.** The write-verdict
   log first emitted 15 lines every 30 seconds on a steady plant, because the
   setpoint is rewritten periodically and every rewrite makes the next
   confirmation a transition. Now the first confirmation speaks once and every
   non-confirmation always speaks.
4. **Round in `double`.** cJSON prints doubles; rounding in `float` or with
   `roundf` reintroduces `310.079986572266`. This has been fixed twice.
5. **Say why, in the code.** The comments in this repository state the failure a
   decision prevents, usually with the measurement. That is deliberate — it is
   what stops a later reader "simplifying" a safety decision away.
6. **The owner prefers short, plain Roman-Urdu replies** — two or three lines, no
   tables or jargon unless asked.
