# Sample configuration

Two samples live in `config-samples/`:

| File | Purpose | Ready to apply? |
|---|---|---|
| `lab-simulator.json` | Points the controller at the SolTrix Modbus simulator so the control loop can be exercised without real equipment | **Yes** |
| `site-template.json` | Commissioning template for a real plant | **No, by design** — every value that must be measured is `null` |

The site template is deliberately incomplete. The firmware rejects an incomplete
configuration rather than run on a plausible guess, and several values in the lab
sample would be actively wrong on real equipment.

---

## 1. Why this is not one file

Configuration is applied through several authenticated endpoints, not one import:

| Section | Endpoint |
|---|---|
| `meters` | `POST /api/meters/config` |
| `inverters` | `POST /api/inverters/config` |
| `profile_assignment` | `POST /api/inverter-profile-assignment` |
| `solar_grid` | `POST /api/solar-grid/config` |
| `control` | `POST /api/config` |

Each is separately validated, and two of them deliberately disable automatic
control as a side effect (see §4). The `_comment` / `_notes` / `_endpoint` keys in
the samples are documentation — strip them before posting.

All of these require an authenticated Engineering session. Obtain the password
from the product owner; it is never stored in this repository.

## 2. Applying the lab sample

```bash
CTRL=http://192.168.100.14
# 1. Authenticate (the session cookie is required by every call below)
curl -s -c /tmp/jar -X POST "$CTRL/api/engineering/login" \
     -H 'Content-Type: application/json' \
     --data-binary '{"password":"<from the product owner>"}'

# 2. Meters, then inverters, then the profile assignment, then policy, then control
curl -s -b /tmp/jar -X POST "$CTRL/api/meters/config"  --data-binary @meters.json
curl -s -b /tmp/jar -X POST "$CTRL/api/inverters/config" --data-binary @inverters.json
curl -s -b /tmp/jar -X POST "$CTRL/api/inverter-profile-assignment" --data-binary @profile.json
curl -s -b /tmp/jar -X POST "$CTRL/api/solar-grid/config" --data-binary @solargrid.json

# 3. Restart: a profile assignment takes effect on the next start
curl -s -b /tmp/jar -X POST "$CTRL/api/system/restart" --data-binary '{}'

# 4. Verify the verdict BEFORE enabling control
curl -s -b /tmp/jar "$CTRL/api/commissioning/gate"
```

`scripts/lab_run.py` automates exactly this sequence, then measures the loop with
the simulator read directly as an independent witness to what the firmware
reports.

**Check the gate before enabling control.** A satisfied gate is not enough on its
own — read its `scope`:

| `scope` | Meaning |
|---|---|
| `none` | Not commissioned. Automatic control inhibited. |
| `lab_simulator_only` | Commissioned, but at least one commanded inverter is a **declared simulator**. Nothing observed is evidence about physical equipment. |
| `production` | Every commanded inverter passed physical readback qualification. |

## 3. The fields that are easy to get wrong

**`data_type` on grid active power must be signed** (`3` = int32). Grid power goes
negative when exporting. An unsigned type decodes export as a large *import* —
the wrong sign at precisely the moment the controller must reduce PV. This is the
error class that has bitten this project more than once.

**`word_order`** (`0` ABCD high-word-first, `1` CDAB, `2` BADC, `3` DCBA). Prove
it against a known non-zero power. A wrong order can read plausibly at some values
and absurdly at others, so a single spot check is not proof.

**`poll_ms: 0`** means "issue the next read as soon as the previous transaction
completes" — the fastest the device and network allow. It is not zero latency: the
floor is one round trip. Measure the gateway first. One site gateway sustained only
about **11 requests per second**, with a ~300 ms stall on roughly a quarter of
transactions. Any positive value is honoured exactly, for equipment that cannot
sustain the rate.

**`timeout_ms` must clear the MEASURED worst case, not the typical one.** A timeout
inside the latency tail records healthy responses as failures, which feeds the
quality window that marks a meter degraded — and a degraded meter **blocks control
input**. On the measured site link, 300 ms did exactly that to ~3 % of good reads;
800 ms clears it. See `docs/ACQUISITION_TIMING_MEASUREMENTS.md`.

**`interval_ms`** (control period, 10–10000) is **fixed**, not poll-on-completion.
A jittering period gives a jittering integral term, and determinism matters more
here than the last few milliseconds. A fast loop does not mean fast Modbus writes:
commands are issued on change plus a keepalive.

**`meter_stale_timeout_ms`** must be ≥ `interval_ms` and should exceed the measured
worst-case acquisition latency, or fresh data is intermittently judged stale and
control is inhibited for no real reason.

## 4. Two settings that disable automatic control when you change them

This is deliberate, not a bug: neither may take effect underneath a running loop.

- Assigning an **inverter profile**
- Setting or clearing a **lab target**

Re-enable control afterwards, and only after re-reading the gate.

## 5. `lab_target` — read this before setting it

`lab_target: true` is what unlocks commanding through a profile that has not
passed physical qualification. It grants **lab authority only**, never production,
and one declared simulator anywhere makes the whole commissioning verdict `LAB`.

Setting it against **real equipment** is a false statement about physical reality.
It is the one thing this design cannot defend against, and it is the reason the
banner, the status API and the gate all report lab mode prominently.

## 6. Ramp behaviour

`grid_ramp` disabled means the command reaches the allowed target in a **single
cycle** — the grid-mode requirement. Disabling it removes a *rate limit only*: the
export/import target, the generator minimum-loading limit and every safety clamp
are applied before the rate limiter and still hold.

`generator_ramp` is a **true rate** in percent of fleet capacity per second,
independent of the loop period, so it stays meaningful if the period changes. Keep
`down_pct_s` greater than `up_pct_s`: give power away slowly, take it back quickly,
because reducing PV is the direction that protects the generator.

Note that some inverters have their **own** internal ramp limiter (Huawei
documents one at register 42017 in %/s). Two rate limiters in series means the
slower dominates, so a controller ramp faster than the machine's gradient will
silently not be achieved and will look like a tracking failure. Reconcile them on
site.

## 7. What a correctly filled site template still does not give you

A complete, valid site configuration yields a **monitoring, commissioning and
protection** installation — not closed-loop control — because on this commit **no
manufacturer profile has passed physical readback qualification**. Automatic
control remains structurally inhibited until one does.

Several profiles are refused outright, and the live API is the authority rather
than this list:

- **GoodWe** — the command register is flash-backed ("does not support
  high-frequency write operations") with no documented write rate, so commanding it
  would wear out the inverter's memory while every write reported success.
- **Growatt** — power-on write lock, password redacted in the manual, re-arms after
  five minutes.
- **Chint/CPS**, **Knox/AISWEI** — a prerequisite enable register whose
  *readability* is not established by any citation. An enable written blind cannot
  be verified, and an unverified enable means the setpoint is accepted, echoed back
  and ignored.

Call `GET /api/inverter-profiles` for the current permission and reason per
profile; it is generated from the same rule the firmware enforces.
