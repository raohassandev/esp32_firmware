# Factory acceptance test — Automatrix PV-DG Controller

**Audience:** a test engineer who did **not** write this firmware, working at a
bench with the unit, a laptop and a power source. No firmware knowledge assumed
beyond flashing a supplied binary.
**Executed:** before the unit ships. No plant, no inverter, no generator, no meter
that matters is attached.
**Written against:** `phase1-fix` (see `git log`), ESP-IDF v6.0.1, target
ESP32-S3-DevKitC-1 N16R8.

---

## 0. What this document is

An **ordered, repeatable procedure with an explicit pass criterion and a recording
blank for every step.** A step is either PASS, FAIL or NOT RUN. There is no
"looked fine".

Every step is tagged:

| Tag | Meaning |
|---|---|
| **[OBS]** | Observe only. Nothing is written to the unit. |
| **[CFG]** | Writes controller configuration or credentials. |
| **[PWR]** | Power-cycles, resets or reflashes the unit. |
| **[SIM]** | Requires the Modbus simulator. Skip and record NOT RUN if unavailable. |

**This document contains no manufacturer values, no timing tolerances, no
generator ratings and no register addresses of its own.** Where a number appears
it is a **firmware constant cited to its source file**, and it is quoted so the
tester can check the firmware against itself — not because the value has been
validated against anything. Where a value must come from the bench, the step says
**measure and record**.

**Credentials.** The Engineering password, the Wi-Fi credentials for the bench
network and any per-unit secret are **obtained from the product owner**. Nothing
of that kind is recorded in this document or anywhere in this repository.

### 0.1 What must be true before starting

- [ ] The firmware binary under test is the **exact artefact intended to ship**,
      identified by commit: `______`, binary SHA-256: `______`
- [ ] CI is green for that commit (see `docs/RELEASE_CHECKLIST.md` §1). Run URL:
      `______`
- [ ] Serial console available and logging to a file. Log file name: `______`
- [ ] A laptop able to join both the bench Wi-Fi network and the unit's own
      setup access point
- [ ] `curl` (or equivalent) available; a browser for the UI steps
- [ ] The Engineering password, obtained from the product owner
- [ ] **A previous firmware build that writes an older configuration schema**, if
      §7 (NVS migration) is to be executed at all. Which build: `______`
- [ ] Modbus simulator available? **Yes / No:** `______` (`tools/soltrix_modbus_simulator.js`)

### 0.2 Record sheet header

| Field | Value |
|---|---|
| Unit serial | `______` |
| Firmware commit | `______` |
| Binary SHA-256 | `______` |
| Tester | `______` |
| Date started / finished | `______` / `______` |
| Overall verdict | PASS / FAIL / INCOMPLETE: `______` |

---

## 1. Power-on and boot integrity [PWR]

### 1.1 First boot from an erased device

- [ ] Erase flash **including NVS**, then flash the binary under test. Record the
      exact commands used: `______`
- [ ] Console shows no panic, no `Guru Meditation`, no backtrace, and no reboot
      loop. Observed: `______`
- [ ] Record the reset reason reported at boot: `______`
      **Pass:** a power-on reset. Anything else on a first boot is a finding.
- [ ] With no stored configuration the log states that safe defaults were loaded.
      Line observed (`config_manager.c` emits `No valid stored configuration; safe
      defaults loaded`): `______`
- [ ] The `storage` partition is provisioned on first boot and the log says the
      alarm journal is durable. Line observed: `______`
      **Pass:** the line appears exactly once, on this first boot only.
- [ ] Record free heap and largest free block at the end of boot, from
      `GET /api/system/resources` once the interface is up: `______` / `______`
      **Pass criterion:** these are **recorded**, not compared to a threshold.
      No memory budget is documented in this repository, so a pass/fail number
      would be invented. Record them so a trend exists across units.

### 1.2 Boot repeatability

- [ ] Power-cycle the unit **five** times, at least 10 s off each time. Record the
      boot result and reset reason for each:

  | # | Booted to a serving web interface? | Reset reason | Panic / backtrace? |
  |---|---|---|---|
  | 1 | `______` | `______` | `______` |
  | 2 | `______` | `______` | `______` |
  | 3 | `______` | `______` | `______` |
  | 4 | `______` | `______` | `______` |
  | 5 | `______` | `______` | `______` |

- **Pass:** 5 of 5 boot to a serving interface with no panic. **Any** panic or
  reboot loop fails the whole FAT — stop and report.

### 1.3 Sustained uptime

- [ ] Leave the unit powered and idle. Record uptime reached without a reset:
      `______` minutes.
- [ ] **Pass criterion:** the tester and the product owner agree the soak duration
      **before** starting, and record it here: agreed duration `______`, agreed by
      `______`. This document does not set one, because no soak requirement is
      documented in this repository and inventing one would give a false pass.
- [ ] Free heap at start and at end of soak: `______` / `______`.
      **Pass:** no monotonic decline. If it declines, record the rate and raise it
      as a defect — do not judge it acceptable here.

---

## 2. Wi-Fi provisioning and the recovery access point [CFG]

The unit is a station on a site network, and it carries a **fallback access
point** so a unit that cannot reach any configured network is still reachable.
The fallback AP is enabled by default (`components/config_manager/config_manager.c`,
`defaults()`).

> **Credential finding to check, not to copy.** `config_manager.c` sets a
> **hard-coded default fallback-AP SSID and password** in `defaults()`. A default
> shared across every unit is a shipped credential. Whether it is acceptable is a
> product-owner decision and it is tracked in `docs/RELEASE_CHECKLIST.md` §6. Do
> **not** transcribe the value into this record sheet — cite the file and line.

### 2.1 The recovery AP appears when no configured network is reachable

- [ ] Starting from the erased first-boot state of §1.1, with no configured SSID
      present, wait for the station attempts to be exhausted.
- [ ] Console reports that no configured station is available and the setup AP is
      active. Line observed: `______`
- [ ] The AP is visible to the laptop under the default SSID from
      `config_manager.c` `defaults()`. Visible: `______`
      Time from power-on to AP visible: `______` s (**measure and record**; no
      target is documented)
- [ ] Join the AP and load `http://<unit IP>/`. Loads: `______`
- **Pass:** the AP appears without operator action, and the interface serves over
  it.

### 2.2 Provisioning a station profile

- [ ] Over the recovery AP, `POST /api/wifi/config` (authenticated — see §4) with
      the bench network's primary SSID and password, obtained from the product
      owner. HTTP status: `______`
- [ ] `GET /api/wifi/scan` returns the bench SSID before provisioning: `______`
- [ ] The unit associates with the bench network. Record the IP obtained: `______`
      and the RSSI reported: `______` dBm
- [ ] `GET /api/config` (authenticated) reports the primary SSID as configured and
      the **password masked, never echoed**. Masked: `______`
- **Pass:** association succeeds and no password is returned in any response body.
  A returned password fails this step outright.

### 2.3 The recovery AP returns after the station is lost

- [ ] With the unit associated, remove the bench network (power the bench AP down,
      or change its passphrase).
- [ ] Time until the unit's recovery AP is visible again: `______` s
      (**measure and record**)
- [ ] Interface reachable over the recovery AP while the station is down: `______`
- [ ] Restore the bench network. The unit reassociates without intervention:
      `______`, time taken `______` s
- **Pass:** the unit is reachable at every point in this sequence — either as a
  station or over its own AP — and it recovers without a power cycle.

### 2.4 A stored station profile is not overwritten by the build

- [ ] `main/Kconfig.projbuild`: `PVDG_APPLY_BUILD_WIFI_PROVISIONING` default is
      `n` and the shipping `sdkconfig` does not enable it. Confirmed for the
      artefact under test: `______`
- [ ] Power-cycle the unit. The provisioned SSID from §2.2 is still in force and
      was **not** replaced by a build-time value: `______`
- **Pass:** the commissioned SSID survives. This is the same property §7 tests
  across a firmware change; here it is tested across a power cycle.

---

## 3. The web interface loads [OBS]

- [ ] `http://<unit IP>/` returns 200 and renders. Status: `______`
- [ ] `/app.css` and `/app.js` return 200: `______` / `______`
- [ ] `GET /api/status` returns 200 with valid JSON **unauthenticated** — it is a
      public route (`components/web_server/engineering_guard.c`, `public_uri()`).
      Status: `______`
- [ ] Record from that response: `control_enabled` `______`, `inhibit_reason`
      `______`, `commissioned` `______`, `commissioning_scope` `______`
      **Pass:** `control_enabled` false, `commissioning_scope` `none`, and a
      non-empty inhibit reason. See §10.
- [ ] Browser console shows no uncaught JavaScript error on load. Observed:
      `______`
- [ ] Every navigation group in the shell opens without error. Pages visited:
      `______`

> **Limit of this section.** CI checks JavaScript **syntax** and computes WCAG
> contrast arithmetic on token values; it does not render anything, and
> `docs/RELEASE_READINESS.md` §4 records the last visual audit as **invalid** and
> not repeated. Loading without error is therefore the strongest claim available
> here. A visual review is a separate activity and is not this FAT's pass
> criterion.

---

## 4. Engineering authentication, including lockout [CFG]

Constants below are cited from `components/web_server/engineering_auth.c` so the
tester can check the built firmware against its own source. They are firmware
values, not validated security requirements.

| Property | Value in source | Where |
|---|---|---|
| Failures before lockout | `AUTH_MAX_FAILURES` = 5 | `engineering_auth.c` |
| Lockout duration | `AUTH_LOCKOUT_MS` = 30 000 ms | `engineering_auth.c` |
| Session idle timeout | `AUTH_SESSION_TIMEOUT_MS` = 30 min | `engineering_auth.c` |
| Minimum password length | `AUTH_MIN_PASSWORD_LENGTH` = 10 | `engineering_auth.c` |
| KDF iterations | `AUTH_PBKDF2_ITERATIONS` = 20 000 | `engineering_auth.c` |
| Session cookie | `eng_session` | `engineering_auth.c` |

- [ ] Confirm the values above match the source in the artefact under test —
      a FAT that quotes stale constants proves nothing. Match: `______`

### 4.1 Setting the password

- [ ] On an unconfigured unit, `GET /api/engineering/session` reports that no
      password is configured. Observed: `______`
- [ ] Record how the initial credential is established on this build (setup code
      / `POST /api/engineering/password`) and the exact sequence used: `______`
- [ ] Attempt a password **shorter than the minimum length**. Rejected: `______`,
      status `______`
- [ ] Set the password supplied by the product owner. Status: `______`
      **Do not write the password into this record sheet.**

### 4.2 Login and session

- [ ] `POST /api/engineering/login` with the **wrong** password → status `______`
      **Pass:** not 200, and the response body does not distinguish "wrong
      password" from "no such user" in a way that leaks state. Body: `______`
- [ ] `POST /api/engineering/login` with the correct password → status `______`,
      `Set-Cookie: eng_session=...` present `______`
- [ ] `GET /api/engineering/session` with the cookie reports an authorised
      session: `______`
- [ ] `POST /api/engineering/logout`, then repeat a protected GET. Refused:
      `______`
- **Pass:** all four.

### 4.3 Lockout

- [ ] Submit **five** consecutive wrong passwords. Record the status of each:
      1 `______` 2 `______` 3 `______` 4 `______` 5 `______`
- [ ] On the sixth attempt — **with the correct password** — the request is
      refused because the lockout is engaged. Status: `______`, body: `______`
      **Pass:** the correct password is refused during the lockout. If the correct
      password succeeds immediately after five failures, the lockout is not in
      force and this step FAILS.
- [ ] Measure the wall-clock time until a correct password is accepted again:
      `______` s.
      **Pass:** it is at least the source value of `AUTH_LOCKOUT_MS` and the
      measured value is recorded. Do not round it to the constant — record what
      you measured.
- [ ] Record whether the lockout attempt was written to the audit trail:
      `GET /api/system/audit-log` (authenticated) contains an entry for the
      refused attempt: `______`
- [ ] Repeat the five-failure sequence a second time to confirm the counter reset
      after a successful login. Locked again on the fifth: `______`

### 4.4 Session expiry

- [ ] Log in, then leave the session idle. Record the time at which a protected
      request is first refused: `______`.
      **Pass:** refusal occurs, and the measured idle time is recorded. The source
      value is 30 minutes; the pass criterion is that the session **does** expire,
      because a session that never expires is the defect this checks for.

---

## 5. Every protected endpoint refuses unauthenticated access [OBS]

The gateway is `components/web_server/engineering_guard.c`. It has **two**
behaviours and a tester must know both, or a correct unit will look broken and a
broken unit will look correct.

1. **Protected (default).** Any registered route that is not public is wrapped and
   returns **401** unauthenticated.
2. **Public.** `/`, `/favicon.ico`, `/app.css`, `/app.js`, `/api/status`,
   `/api/telemetry` and everything under `/api/engineering/` are served
   unauthenticated by design (`public_uri()`).
3. **Reduced "safe" payload — not 401.** Four GET routes return a
   deliberately **reduced operator payload** instead of 401 when unauthenticated:
   `GET /api/config`, `GET /api/meters`, `GET /api/inverters`,
   `GET /api/inverter-telemetry`. Each safe payload sets `"operator_view": true`,
   and `safe_config()` blanks the Wi-Fi SSIDs and passwords outright.

> So "every protected endpoint returns 401" is **not** the correct pass criterion
> for this build. The correct criterion is: **no unauthenticated response
> contains engineering detail or any credential**, and every mutating route is
> refused.

### 5.1 Unauthenticated sweep

With **no cookie**, call each route and record the status and whether the body
contains any credential, SSID, or engineering-only field.

| Route | Method | Expected | Status | Credential/SSID in body? |
|---|---|---|---|---|
| `/api/status` | GET | 200 public | `______` | `______` |
| `/api/telemetry` | GET | 200 public | `______` | `______` |
| `/api/config` | GET | 200 **safe payload**, `operator_view: true`, SSIDs blank | `______` | `______` |
| `/api/config` | POST | 401 | `______` | `______` |
| `/api/meters` | GET | 200 safe payload | `______` | `______` |
| `/api/meters/config` | GET | 401 | `______` | `______` |
| `/api/meters/config` | POST | 401 | `______` | `______` |
| `/api/inverters` | GET | 200 safe payload | `______` | `______` |
| `/api/inverters/config` | GET | 401 | `______` | `______` |
| `/api/inverters/config` | POST | 401 | `______` | `______` |
| `/api/inverter-telemetry` | GET | 200 safe payload | `______` | `______` |
| `/api/inverter-profiles` | GET | 401 | `______` | `______` |
| `/api/inverter-profile-assignment` | POST | 401 | `______` | `______` |
| `/api/inverter-probe` | POST | 401 | `______` | `______` |
| `/api/inverters/write-confirmation` | GET | 401 | `______` | `______` |
| `/api/commissioning/gate` | GET | 401 | `______` | `______` |
| `/api/solar-grid/config` | GET | 401 | `______` | `______` |
| `/api/solar-grid/config` | POST | 401 | `______` | `______` |
| `/api/solar-grid/status` | GET | 401 | `______` | `______` |
| `/api/source-detection` | GET | 401 | `______` | `______` |
| `/api/source-detection` | POST | 401 | `______` | `______` |
| `/api/wifi/config` | POST | 401 | `______` | `______` |
| `/api/wifi/scan` | GET | 401 | `______` | `______` |
| `/api/wifi/scan` | POST | 401 | `______` | `______` |
| `/api/wifi/rescan` | POST | 401 | `______` | `______` |
| `/api/system/audit-log` | GET | 401 | `______` | `______` |
| `/api/system/identity` | GET | 401 | `______` | `______` |
| `/api/system/resources` | GET | 401 | `______` | `______` |
| `/api/system/restart` | POST | 401 | `______` | `______` |
| `/api/meters/em500/cache` | GET | 401 | `______` | `______` |
| `/api/meters/em500/snapshot` | GET | 401 | `______` | `______` |
| `/api/meters/em500/history` | GET | 401 | `______` | `______` |
| `/api/meters/em500/settings` | GET/POST | 401 | `______` | `______` |
| `/api/meters/em500/settings/plan` | GET/POST | 401 | `______` | `______` |
| `/api/operator/history` | GET | 200 by design (operator scope) | `______` | `______` |
| `/api/operator/events` | GET | 200 by design | `______` | `______` |
| `/api/operator/alarms` | GET | 200 by design | `______` | `______` |
| `/api/operator/alarms/journal` | GET | 200 by design | `______` | `______` |
| `/api/operator/alarms/ack` | POST | **401** | `______` | `______` |
| `/api/operator/alarms/shelve` | POST | **401** | `______` | `______` |
| `/api/operator/alarms/unshelve` | POST | **401** | `______` | `______` |

- [ ] **Enumerate the routes from the firmware, not from this table.** Grep the
      registered URIs in `components/web_server/` for the artefact under test and
      confirm every route appears above. Any route in the firmware and not in this
      table is a gap in this document — record it: `______`
- [ ] **Pass criteria, all three:**
      (a) every mutating route (POST) is refused unauthenticated;
      (b) no unauthenticated response body contains a password, a Wi-Fi SSID, or
      an engineering-only field;
      (c) every route expected 401 returned 401.
- [ ] Repeat the four safe-payload GETs **with** a valid session and confirm the
      payload is richer than the unauthenticated one, i.e. the gateway is actually
      distinguishing the two: `______`

---

## 6. Configuration persistence across a power cycle [CFG][PWR]

- [ ] Authenticate. Apply a distinctive, harmless configuration and record exactly
      what was set: device name `______`, meter name(s) `______`, inverter
      `rated_kw` `______`, control `interval_ms` `______`,
      `meter_stale_timeout_ms` `______`
- [ ] `GET /api/config` (authenticated) reads back **every** value just written,
      byte for byte as accepted. Matches: `______`
- [ ] Save the full authenticated `GET /api/config` response to a file. File:
      `______`
- [ ] Hard power-cycle (remove power, not a software restart). Done: `______`
- [ ] After boot, `GET /api/config` again and **diff against the saved file**.
      Differences found: `______`
      **Pass:** no differences other than fields documented as runtime state.
      List any field that differs and why: `______`
- [ ] Repeat with a software restart (`POST /api/system/restart`). Diff result:
      `______`
- [ ] `GET /api/system/resources` reports `schema_version_stored` equal to
      `schema_version_supported`. Values: `______` / `______`
- **Note on the persistence path:** `config_manager_save()` writes the blob, then
  **reopens NVS, reads it back and byte-compares**, returning
  `ESP_ERR_INVALID_CRC` if it disagrees. A save that reports success has already
  been verified once by the firmware. This step tests that the *stored* value
  survives power removal, which the firmware cannot test for itself.

---

## 7. NVS migration — a commissioned configuration must survive a firmware update [PWR]

**Why this section exists.** The repository carries explicit migrations for
configuration schemas 1, 2, 3 and 4 up to the current schema
(`APP_CONFIG_VERSION` = 5, `components/config_manager/include/config_types.h`).
They exist to stop a firmware update from discarding a commissioned
configuration. **The specific failure mode they prevent is losing the Wi-Fi
credentials** — a unit that comes back from an update on a different network, or
on none, is a unit that must be visited. Two of the migration paths carry a
comment saying exactly that: they preserve `wifi_provision_id` so an upgrade
cannot replay build credentials over what an operator set.

Migration is selected by the **size of the stored blob**, so it triggers only when
an older build actually wrote that blob. It cannot be exercised by editing a
config file, and it cannot be exercised at all without an older build.

- [ ] Is an older build available that writes a legacy schema? **Yes / No:**
      `______`
      **If No:** record §7 as **NOT RUN**, state that migration is therefore
      **untested on hardware for this release**, and carry it into
      `docs/RELEASE_CHECKLIST.md` as an open item. Do not mark it PASS by
      inspection.

### 7.1 Procedure, per legacy schema available

Repeat for each older build available. Record which schema each writes.

- [ ] Erase flash including NVS. Flash the **older** build. Build/commit: `______`,
      schema it writes: `______`
- [ ] Commission it far enough to be representative: provision Wi-Fi (§2.2),
      configure at least one meter and one inverter, set a distinctive device
      name. Record everything set: `______`
- [ ] Save the older build's authenticated `GET /api/config` to a file: `______`
- [ ] Note the station SSID in force and the IP obtained: `______` / `______`
- [ ] Flash the **new** build **without erasing NVS**. Record the exact command
      used and confirm it does not erase the NVS partition: `______`
      **A step that erases NVS invalidates this entire section.**
- [ ] Console shows the migration line for the expected schema, e.g. `Migrated
      configuration schema <N> to schema 5`. Line observed verbatim: `______`
      **Pass:** the line names the schema the older build wrote. If instead the
      log says `No valid stored configuration; safe defaults loaded`, or
      `Stored configuration blob is <n> bytes but no known schema matches`, or
      `Schema <N> migration produced an invalid configuration; discarding it`,
      the migration **FAILED** — record which line appeared: `______`

### 7.2 What must have survived

- [ ] **Wi-Fi:** the unit rejoins the same SSID with no operator action, at the
      same IP mode. SSID after update: `______`, IP: `______`
      **Pass:** identical SSID. This is the headline criterion of §7.
- [ ] Device name preserved: `______`
- [ ] Meter configuration preserved (count, names, unit ids, endpoints):
      `______`
- [ ] Inverter configuration preserved (count, names, `rated_kw`, endpoints):
      `______`
- [ ] `GET /api/system/resources`: `schema_version_stored` now equals
      `schema_version_supported`: `______` / `______`
- [ ] **Automatic control is OFF after the upgrade.** `GET /api/status`
      `control_enabled` = `______`
      **Pass:** false. `upgrade_control_from_v4()` sets `enabled = false`
      deliberately — *an upgrade never arms automatic control*. If control came
      back enabled, that is a serious defect: report it and stop.
- [ ] Diff the post-upgrade authenticated `GET /api/config` against the saved
      pre-upgrade file. Every difference must be **explainable by the migration**
      (a new schema-5 field taking its default). List each difference and its
      explanation:

  | Field | Before | After | Explanation |
  |---|---|---|---|
  | `______` | `______` | `______` | `______` |

- [ ] Power-cycle once more and confirm the migrated configuration persists (it is
      re-saved at the new schema): `______`
- [ ] Confirm the migration does **not** repeat on the second boot — no migration
      line on the following boot: `______`

### 7.3 Migration paths not exercised

- [ ] List every legacy schema for which **no** older build was available, and
      therefore was **not tested**: `______`
      These remain proven only by the `_Static_assert` blob-size constraints in
      `config_manager.c` and by review. Say so in the report; do not describe them
      as verified.

---

## 8. Alarm lifecycle, including RTN-unacknowledged [OBS][CFG]

**Why this matters.** `docs/ALARM_MANAGEMENT_RESEARCH.md` §5 records
**RTN-unacknowledged** as gap A1, "the most consequential gap": a fault that
appears and clears while nobody is watching must stay visible, or an overnight
fault leaves no trace an operator will see. This section proves the fixed
behaviour on the bench.

Published behaviour, from `components/web_server/operational_api.c` — the values
are also returned in the `GET /api/operator/alarms` body, so check the response
against the source rather than against this table:

| Property | Source constant | Value |
|---|---|---|
| On-delay | `ALARM_ON_DELAY_MS` | 1 000 ms |
| Off-delay | `ALARM_OFF_DELAY_MS` | 2 000 ms |
| Stale threshold | `ALARM_STALE_THRESHOLD_MS` | 86 400 000 ms (24 h) |
| Shelf minimum / maximum | `ALARM_SHELF_MIN_MS` / `ALARM_SHELF_MAX_MS` | 60 000 ms / 28 800 000 ms |

- [ ] `GET /api/operator/alarms` reports `on_delay_ms`, `off_delay_ms`,
      `stale_threshold_ms`, `shelf_minimum_ms`, `shelf_maximum_ms` and they match
      the source in the artefact under test: `______`
- [ ] `summary.state_model` is `ISA-18.2`, `summary.priority_model` is
      `EEMUA-191`, and `summary.suppression_model` states that only shelving is
      implemented: `______`

### 8.1 Raise a condition

A bench-safe way to raise a real alarm is to configure a meter pointing at an
endpoint that does not answer, so it goes offline. Record the method used —
**do not induce a fault by any means the tester has not recorded**: `______`

- [ ] Condition appears in `GET /api/operator/alarms`. Alarm `id`: `______`
- [ ] `state` is `unacknowledged`, `present` true: `______`
- [ ] `occurrences` is 1, `first_raised_age_ms` and `last_raised_age_ms` present:
      `______`
- [ ] `role` is `primary` or `consequential`; if consequential, `caused_by` names
      another alarm: `______`
- [ ] Measure the delay between the physical condition and the alarm appearing:
      `______` ms. **Pass:** it is at least the published `on_delay_ms` — the
      on-delay consumes operator response time and must be real, not nominal.

### 8.2 Acknowledgement

- [ ] `POST /api/operator/alarms/ack` **unauthenticated** → status `______`
      **Pass:** 401 with `engineering_authentication_required`.
- [ ] Authenticated ack of the specific code → status `______`
- [ ] `state` becomes `acknowledged`, and `present` is **still true**: `______`
      **Pass:** acknowledgement did **not** clear the condition. Only the plant
      clears a condition.
- [ ] Ack with no code / an unknown code / a non-alarm code → each rejected 400:
      `______`
- [ ] `acknowledged_age_ms` becomes non-null: `______`
- [ ] **Attribution limit, record it:** the acknowledgement records that an
      authenticated engineering session did it, **not which person** — there is no
      operator identity model (gap A8). Confirmed in the response: `______`

### 8.3 RTN-unacknowledged — the headline test

- [ ] Raise the condition again and **do not acknowledge it**. Raised: `______`
- [ ] Confirm `state` is `unacknowledged`: `______`
- [ ] Remove the cause so the condition clears **while nobody acknowledges it**.
      Method and time: `______`
- [ ] Wait past the published `off_delay_ms`, then re-read
      `GET /api/operator/alarms`.
- [ ] **Pass criteria, all of them:**
      - the alarm is **still present in the list**: `______`
      - `state` is exactly `rtn_unacknowledged`: `______`
      - `present` is false: `______`
      - `acknowledged` is false: `______`
      - `summary.unacknowledged` still counts it: `______`
- [ ] Now acknowledge it. `state` becomes `normal`: `______`
      **Pass:** a returned-to-normal alarm **can** be acknowledged. (Commit
      `aade4c6` exists specifically to allow this; if the ack is refused, the
      artefact under test predates that fix.)
- [ ] **Fail condition to watch for:** if the alarm disappears from the list when
      the condition clears, gap A1 is not closed in this artefact. Record it as a
      FAIL and stop this section.

### 8.4 Chatter suppression

- [ ] Cycle the condition on and off faster than the published on/off delays,
      several times. Method and cycle period: `______`
- [ ] `suppressed_transitions` for that alarm increases: `______`
      **Pass:** the counter rises and the alarm list does not gain a new entry per
      cycle. Zero is the healthy value in steady state; a rising number here is
      the evidence the delay is working.

### 8.5 Shelving

- [ ] `POST /api/operator/alarms/shelve` unauthenticated → 401: `______`
- [ ] Authenticated shelve, duration **below** `shelf_minimum_ms` → rejected:
      `______`
- [ ] Authenticated shelve, duration **above** `shelf_maximum_ms` → rejected:
      `______`
- [ ] Valid shelve → `shelved` true, `suppression` is `shelved`,
      `shelf_remaining_ms` counts down, and the row is **still in the list**:
      `______`
- [ ] `summary.shelved` / `summary.shelved_active` count it, and
      `summary.active` / `summary.unacknowledged` no longer do: `______`
      **Pass:** shelved work is reported, never hidden.
- [ ] `POST /api/operator/alarms/unshelve` restores it: `______`
- [ ] Let a short shelf **expire** and confirm the alarm returns to the triage
      counts by itself: `______`, measured expiry `______`

### 8.6 Root-cause grouping

- [ ] Induce a single physical event that raises more than one condition (losing
      the meter link raises several — record which): `______`
- [ ] Record `summary.active`, `summary.primary_active`,
      `summary.consequential_active`: `______` / `______` / `______`
- [ ] **Pass:** `primary_active` is smaller than `active`, and every
      consequential row names a `caused_by`. This is gap A5's grouping; it is what
      keeps one physical event from reading as several distinct faults.
- [ ] Count the alarms raised in the first 10 minutes after that single event:
      `______`
      **Reference, not a bench pass criterion:** EEMUA 191 targets no more than 10
      alarms in the first 10 minutes after a major upset
      (`docs/ALARM_MANAGEMENT_RESEARCH.md` §2). A bench meter unplug is not a
      plant upset, so **record the count and carry it forward** — the real
      measurement belongs in `docs/SITE_ACCEPTANCE_TEST.md`.

---

## 9. Alarm journal durability across a reset [PWR]

The journal is `components/web_server/alarm_journal.c`. From its header: it never
formats and never truncates the storage partition; every record carries its own
magic, version and CRC; capacity is `ALARM_JOURNAL_CAPACITY` = 16 384 records, and
the ring wraps oldest-first.

- [ ] `GET /api/operator/alarms/journal` reports the journal ready and a status
      string. `ready` `______`, status `______`
- [ ] Record `stored`, `next_sequence`, `invalid_skipped`, `write_failures`:
      `______` / `______` / `______` / `______`
- [ ] Generate a known number of lifecycle transitions using §8 (raise,
      acknowledge, clear, shelve, unshelve). Number generated: `______`
- [ ] `stored` increases by that number and `next_sequence` advances accordingly.
      New values: `______` / `______`
- [ ] Read a page and confirm the transitions appear **newest first**, with
      transition names from `alarm_journal_transition_name()`: `______`
- [ ] **Hard reset the unit** (power removal, not a software restart). Done:
      `______`
- [ ] After boot, re-read the journal. **Pass criteria, all of them:**
      - `stored` is **at least** the pre-reset value — no records lost: `______`
      - `next_sequence` **continues** from where it was rather than restarting at
        1: `______`
      - every sequence number written before the reset is still readable and in
        order: `______`
      - `invalid_skipped` is 0: `______`
      - `write_failures` is 0: `______`
      - the storage-partition provisioning line from §1.1 **does not** reappear:
        `______`
- [ ] Repeat the reset once more with further transitions in between, to show
      continuity across two resets: `______`

### 9.1 What this step does not establish

- [ ] The ring's **wrap** and its corruption recovery are **not** exercised here.
      `docs/RELEASE_READINESS.md` §4 item 9 records them as proven on a host
      compiler at 16 384 records while the board has written a few dozen. Reaching
      a wrap on a bench would take an artificial write rate this procedure does
      not define. Record NOT RUN and carry it forward: `______`

---

## 10. Automatic control is inhibited, and the stated reason matches [OBS]

**This is the most important safety observation in the FAT**, because it is the
one property the product currently rests on. `docs/RELEASE_READINESS.md` §1: no
manufacturer profile is write-qualified, zero profiles are production-approved,
and automatic control is structurally inhibited against physical equipment by
design.

- [ ] `GET /api/status`: `control_enabled` = `______`
      **Pass:** false.
- [ ] `inhibit_reason` is **non-empty** and is a sentence, not a code: `______`
- [ ] `commissioned` = `______` (**pass:** false),
      `commissioning_scope` = `______` (**pass:** `none`)
- [ ] `commissioning_unmet_count` = `______`, `commissioning_first_unmet` =
      `______`
- [ ] Read the gate detail (`GET /api/commissioning/gate`, authenticated) and
      confirm the `inhibit_reason` in `/api/status` **is the message for
      `first_unmet`** — not a different or more generic sentence. Match: `______`
      **Pass:** they agree. `commissioning_gate_summary()` is the single source of
      that sentence; a mismatch means the operator is being told something other
      than the actual blocker.
- [ ] **Attempt to enable control anyway**: `POST /api/config` with
      `control.enabled` true, authenticated. Record the response: `______`
- [ ] Re-read `GET /api/status`. **Pass:** either the request was refused, or it
      was accepted and `control_enabled` is *still* effectively inhibited with the
      same reason — record which of the two happened, verbatim: `______`
      Either is acceptable; what is not acceptable is a unit that reports control
      as engaged with no qualified profile.
- [ ] `GET /api/inverter-profiles` (authenticated). For **every** profile record
      the reported permission and reason:

  | Profile id | Qualification | Permission | Reason |
  |---|---|---|---|
  | `______` | `______` | `______` | `______` |

- [ ] **Pass:** **no** profile reports production write permission. Count of
      profiles reporting production authority: `______` — must be **0**.
- [ ] The count of profiles returned by the API equals the count in the compiled
      catalogue (`components/inverter_manager/inverter_profiles.c`): `______`
- [ ] Every profile reporting `forbidden` carries a non-empty reason: `______`

---

## 11. The commissioning gate reports every unmet prerequisite [OBS][CFG]

The gate is `components/commissioning_gate/commissioning_gate.c`, declared in
`components/commissioning_gate/include/commissioning_gate.h`. It is **pure** and
**fail-closed**: any prerequisite whose state could not be read is UNMET, never
assumed satisfied. There are nine, evaluated in a fixed order.

### 11.1 The fully fail-closed state

- [ ] On the erased, unconfigured unit of §1.1, read `GET /api/commissioning/gate`
      (authenticated). Save the raw response: `______`
- [ ] **Pass:** `commissioned` false, `scope` `none`, `unmet_count` = 9, and
      **all nine** prerequisites are reported individually as unmet, each with an
      `id`, a `title` and a `reason`. Unmet count observed: `______`

### 11.2 Each of the nine is reported by name, with a reason

Record all nine rows exactly as the API returns them. The gate must never report
a bare count; a prerequisite without a machine reason code is a FAIL.

| # | `id` | `title` | Satisfied? | `reason` |
|---|---|---|---|---|
| 1 | `meter_roles` | `______` | `______` | `______` |
| 2 | `inverter_profile_qualified` | `______` | `______` | `______` |
| 3 | `write_readback` | `______` | `______` | `______` |
| 4 | `fleet_capacity` | `______` | `______` | `______` |
| 5 | `ramp_policy` | `______` | `______` | `______` |
| 6 | `source_detection` | `______` | `______` | `______` |
| 7 | `grid_policy` | `______` | `______` | `______` |
| 8 | `generator_limits` | `______` | `______` | `______` |
| 9 | `control_tuning` | `______` | `______` | `______` |

- [ ] Confirm the nine `id` values and their order match
      `commissioning_prereq_t` in the header. Match: `______`
- [ ] Confirm each `reason` slug is one of `commissioning_reason_t`'s names and is
      not the empty string. Any unrecognised slug: `______`

### 11.3 Distinct reasons are actually distinguished

Drive specific reason codes and confirm the gate names the right one. Each is a
configuration change through an authenticated endpoint; none commands anything.

| Step | What to configure | Expected reason | Observed reason |
|---|---|---|---|
| a | No meter with the grid role | `grid_meter_missing` | `______` |
| b | Two enabled meters both claiming the grid role | `grid_meter_ambiguous` | `______` |
| c | Two meters claiming the same generator slot | `generator_slot_duplicate` | `______` |
| d | No enabled inverter | `no_enabled_inverter` | `______` |
| e | An enabled inverter whose profile is not production-approved | `profile_not_write_qualified` | `______` |
| f | Generator ramp **disabled** | `ramp_policy_invalid` | `______` |
| g | Generator ramp down rate **below** the up rate | `ramp_policy_invalid` | `______` |
| h | `generator_rated_kw` zero | `generator_rating_unknown` | `______` |
| i | `generator_minimum_loading_percent` zero | `generator_loading_unknown` | `______` |
| j | `meter_stale_timeout_ms` below `interval_ms` | `control_tuning_invalid` | `______` |

- [ ] **Note on (f) and (g):** the reason code `ramp_policy_invalid` covers
      disabled, out-of-range **and** down < up, and does **not** distinguish them.
      That is stated in `docs/SITE_COMMISSIONING_RUNBOOK.md` §9.2. Both (f) and
      (g) are therefore expected to report the same slug — this is not a defect,
      but it means an engineer must check all three conditions. Confirmed both
      report the same slug: `______`
- [ ] **Note on (h)/(i):** a zero generator rating is treated as *not
      commissioned* and holds PV at zero. No default rating exists anywhere in
      this repository and none may be entered on the bench. The bench test is that
      the gate **refuses**, not that a rating works.
- [ ] `first_unmet` is always the **lowest-numbered** unmet prerequisite. Verified
      across at least three of the cases above: `______`

### 11.4 The gate cannot reach a production verdict on the bench

- [ ] Satisfy as many prerequisites as a bench allows. Record the best verdict
      reached: `commissioned` `______`, `scope` `______`
- [ ] **Pass:** `scope` is never `production`. It is not reachable on this commit
      — `docs/RELEASE_READINESS.md` §1.1 states so, and
      `tests/lab_target_write_gate_source_contract.py` and
      `tests/inverter_write_permission_test.c` enforce it in CI.
- [ ] Any observed `production` scope is a **critical defect**. Observed: `______`

---

## 12. The lab-simulator loop [SIM]

Skip and record NOT RUN if no simulator is available. This is the only way the
control loop can be exercised at all on a bench, and what it demonstrates is
bounded — see §13.

- [ ] Start `tools/soltrix_modbus_simulator.js`. Record host and port: `______`
- [ ] Run its self-test (`--self-test`) and `tools/soltrix_modbus_simulator_test.js`.
      Both pass: `______`
- [ ] Provision the controller against the simulator. `scripts/lab_run.py`
      automates the sequence; record whether it or a manual sequence was used, and
      the exact steps: `______`
      The Engineering password is passed as an argument and must not be written
      into this record sheet.
- [ ] The inverter endpoint is **explicitly declared a lab target** via
      `POST /api/inverter-profile-assignment`. Declared: `______`
- [ ] Restart, because a profile assignment takes effect on the next start:
      `______`
- [ ] `GET /api/commissioning/gate`: `commissioned` `______`,
      `scope` `______`
      **Pass:** `scope` is exactly `lab_simulator_only`. If it is `production`,
      stop — that is a critical defect (§11.4).
- [ ] The UI displays lab mode prominently, unmistakably, on the operator-facing
      view: `______`
- [ ] Enable control. `GET /api/inverters/write-confirmation` progresses
      `pending` → `confirmed`. States and timings observed: `______`
- [ ] Read the simulator's registers **directly**, independently of the firmware,
      and confirm the setpoint the firmware claims to have written is the setpoint
      the simulator holds: firmware says `______`, simulator holds `______`
      **Pass:** they agree. This independent read is the point of the step — the
      firmware confirming itself is not evidence.
- [ ] Record the measured time from command to confirmation: `______` ms.
      **Pass criterion:** it is **recorded**, and it is below the profile's
      `power_limit_settle_ms`. Do not compare it to any manufacturer value —
      none exists. See `docs/RELEASE_READINESS.md` §3.1.
- [ ] Force a **mismatch** (change the simulator's stored value out from under the
      controller). Confirmation goes to `mismatched` and a safe-zero is demanded:
      `______`
- [ ] Force a **comms loss** (stop the simulator mid-command). Confirmation goes
      to `unverified` after the deadline and a safe-zero is demanded: `______`,
      measured time to `unverified` `______` ms
- [ ] Restore the simulator. Recovery is clean and control does not resume at a
      stale setpoint: `______`
- [ ] **Revoke the lab-target declaration** before the unit ships, and confirm
      automatic control is disabled as a side effect: `______`
      **Pass:** no unit ships with a lab target declared. Verify from
      `GET /api/commissioning/gate` that `scope` is back to `none`: `______`

### 12.1 A note the tester must carry into the report

`docs/RELEASE_READINESS.md` §3.2 draws a line that must not be blurred: the
simulator's register behaviour has been proven **by a direct Modbus client**, but
at the time that document was written **no run existed in which the controller
itself closed the loop**. If §12 above completed, this FAT is the first such
evidence — say so explicitly, and say it is evidence about a **model**, not about
any inverter.

- [ ] Did §12 complete with a firmware-driven confirmed write? `______`
- [ ] Recorded in the report as lab-only evidence: `______`

---

## 13. What a factory acceptance test cannot establish

State this section in the shipped report. A FAT that is silent about its limits
will be read as broader evidence than it is.

1. **Nothing about any real inverter.** No manufacturer profile has passed
   physical qualification (`docs/RELEASE_READINESS.md` §1). A FAT cannot change
   that, and passing this FAT does not authorise commanding a real machine.
2. **Nothing about a real generator.** Rated kW, minimum loading %, reserve kW and
   reverse-power margin have never been supplied to this project
   (`docs/RELEASE_READINESS.md` §6). A bench has no engine, no minimum loading and
   no reverse-power protection to protect.
3. **Which register a real inverter honours.** For Huawei, 40125 vs 40199 is
   explicitly open (`docs/RELEASE_READINESS.md` §4 item 4, decision D8). A bench
   cannot decide it.
4. **The setpoint settle time of any real machine.** The firmware default is a
   firmware-side window with no manufacturer value behind it, and a wrong one has
   already caused a false fault against the simulator
   (`docs/RELEASE_READINESS.md` §3.1). A simulator's settle time is a property of
   the simulator.
5. **Readback semantics** — whether a real machine's readback register reports the
   *requested* or the *active* limit. Unresolved in the repository.
6. **Whether a logger or gateway sits in the Modbus path**, which can re-map unit
   ids and addresses and invalidate every transcribed address.
7. **What a real inverter does when it loses its Modbus master.** Undocumented.
   At least one manufacturer's documented comms-loss fall-back can *raise* the
   plant limit rather than lower it — see `docs/SITE_ACCEPTANCE_TEST.md` §7.
8. **Real acquisition latency at the site.** The measurements in
   `docs/ACQUISITION_TIMING_MEASUREMENTS.md` were taken from a PC on the site
   network, not from the controller, and §4 of that document lists what remains
   unmeasured. A bench link is not the site link.
9. **Control loop tuning.** The gate range-checks `kp`, `ki`, `deadband_kw`,
   `interval_ms` — those are range checks, not a tuning verdict. Nothing has been
   tuned against any plant.
10. **Inverter operational state.** Every inverter reports
    `INVERTER_STATE_UNKNOWN`, and `fleet_synchronised()` is not wired into the
    control engine, because the Huawei Device Status code table is in a document
    the project does not have (`docs/RELEASE_READINESS.md` §4 item 8).
11. **Alarm-rate performance under a real upset.** §8.6 counts alarms after a
    bench fault. EEMUA 191's peak-rate target is about a plant upset and can only
    be measured on site.
12. **Journal wrap and corruption recovery on real flash** (§9.1).
13. **Visual rendering of the UI** (§3).
14. **Any migration path for which no older build was available** (§7.3).

**A passing FAT means: this unit boots reliably, is reachable and recoverable,
refuses unauthenticated access, keeps its configuration across a power cycle and a
firmware update, manages alarms to the state model it claims, and refuses to
control anything.** That is the whole claim.

---

## 14. Verdict and sign-off

### 14.1 Section results

| § | Section | PASS / FAIL / NOT RUN | Notes |
|---|---|---|---|
| 1 | Power-on and boot integrity | `______` | `______` |
| 2 | Wi-Fi provisioning and recovery AP | `______` | `______` |
| 3 | Web interface loads | `______` | `______` |
| 4 | Engineering authentication and lockout | `______` | `______` |
| 5 | Unauthenticated access control | `______` | `______` |
| 6 | Configuration persistence | `______` | `______` |
| 7 | NVS migration | `______` | `______` |
| 8 | Alarm lifecycle incl. RTN-unacknowledged | `______` | `______` |
| 9 | Alarm journal durability | `______` | `______` |
| 10 | Control inhibited, reason matches | `______` | `______` |
| 11 | Commissioning gate reports all nine | `______` | `______` |
| 12 | Lab-simulator loop | `______` | `______` |

### 14.2 Blocking rules

A FAT **fails** — the unit does not ship — if any of the following is true:

- [ ] Any panic, backtrace or reboot loop was observed (§1).
- [ ] Any unauthenticated response contained a credential or a Wi-Fi SSID (§5).
- [ ] Any mutating endpoint was reachable unauthenticated (§5).
- [ ] A configuration was lost across a power cycle (§6).
- [ ] A commissioned Wi-Fi credential was lost across a firmware update (§7).
- [ ] Automatic control came back **enabled** after an upgrade (§7.2).
- [ ] An alarm that cleared unacknowledged disappeared from the list (§8.3).
- [ ] Journal records or sequence continuity were lost across a reset (§9).
- [ ] Any profile reported **production** write authority (§10).
- [ ] The commissioning gate reported **production** scope (§11.4).
- [ ] A lab target was left declared on the unit (§12).

### 14.3 Open items carried forward

Every NOT RUN and every measured value with no documented target goes into
`docs/RELEASE_CHECKLIST.md` rather than being quietly dropped.

| Item | Why not closed | Carried to |
|---|---|---|
| `______` | `______` | `______` |

### 14.4 Signatures

| Role | Name | Date | Signature |
|---|---|---|---|
| Test engineer (executed this FAT) | `______` | `______` | `______` |
| Reviewer (checked the record sheet against this procedure) | `______` | `______` | `______` |
| Product owner (accepted the residual risk and the NOT RUN list) | `______` | `______` | `______` |

- [ ] Record sheet, serial console log, and every saved API response archived
      where they can be found in two years: `______`

**An incomplete FAT is a legitimate outcome and must be reported as incomplete.
Marking a step PASS by inspection, or by reading this document instead of running
it, defeats the entire purpose.**
