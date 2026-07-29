# UI visual and engineering audit — 2026-07-29

Automated audit of the **live controller** at `192.168.100.14` running `d71cb03-dirty`,
driven with Playwright + Chromium 151.

**Coverage:** 60 page runs, all completed, no run errors.
10 routes × {desktop 1440×900, mobile iPhone 12} × {light, dark} × {operator, engineering}.
Routes: dashboard, meters, inverters, control, alarms, wifi, system, commissioning, readiness,
engineering.

| Check | Result |
|---|---|
| Page horizontal overflow | **0** |
| Elements overflowing viewport | **0** |
| Wide content not scrollable | **0** |
| Text clipped by container | **0** |
| Inputs without a label | **0** |
| Controls without accessible name | **0** |
| Images without alt | **0** |
| Duplicate element ids | **0** |
| Low-contrast text | **728** (57 distinct) |
| Tap targets below 44×44 | **211** (mobile) |
| Failed HTTP requests | **299** |
| Console errors / warnings | **296 / 60** |

**The layout engineering is sound.** Responsive behaviour, semantic labelling and accessible names
are clean across every route and both viewports — including mobile, where nothing overflows. The
defects are concentrated in colour theming, hit-target sizing, and backend behaviour under real
browser load.

---

## S1 — Critical: a single browser can lock every client out of the controller

`components/web_server/web_server.c:132` takes `HTTPD_DEFAULT_CONFIG()` and overrides only
`max_uri_handlers` and `stack_size`. The socket configuration is therefore left at ESP-IDF defaults:

```
max_open_sockets   = 7     /* ESP-IDF reserves 3 internally -> only 4 client sockets */
lru_purge_enable   = false /* full pool is never reclaimed */
backlog_conn       = 5
```

A browser opens up to 6 keep-alive connections per origin. Four usable sockets cannot serve one
browser tab, and with LRU purge disabled the server does not reclaim the oldest connection — it
simply stops answering.

**Reproduced.** While Chromium held connections, `curl` to `/api/status` returned `000` after a
10 s timeout and `/app.js` after 60 s, while ICMP continued to reply normally. The moment Chromium
was killed, the same request returned **HTTP 200 in 0.176 s**.

Raw `curl` at 12 concurrent connections passes cleanly (12/12 OK) because curl closes each
connection immediately. It is **held idle keep-alive sockets**, not request volume, that causes this.

Operationally: a commissioning engineer with one browser tab open can make the controller
unreachable to the operator UI, to a second engineer, and to any monitoring client.

`CONFIG_LWIP_MAX_SOCKETS=12`, so there is headroom to raise `max_open_sockets`, and
`lru_purge_enable` should be enabled so a stale connection can never wedge the pool.

## S2 — Critical: PSRAM is disabled on a board that has 8 MB

```
CONFIG_SOC_SPIRAM_SUPPORTED=y      /* the chip supports it */
# CONFIG_SPIRAM is not set          /* but it is switched off */
```

Nothing in the tracked `sdkconfig.defaults` enables it either. The hardware is an ESP32-S3
DevKitC-1 **N16R8** — 16 MB flash, **8 MB octal PSRAM** — and none of that PSRAM is used.

Meanwhile the controller reports:

```json
"free_heap_bytes": 185660,
"minimum_free_heap_bytes": 40396,
"thresholds": { "free_internal_warning_bytes": 65536, "free_internal_critical_bytes": 32768 }
```

Minimum free heap has already fallen to **40,396 bytes** — below the firmware's own warning
threshold and within 8 KB of its critical threshold. That is the underlying cause of S3.

Enabling PSRAM on the S3 requires correct octal-mode and flash configuration and **must be verified
on hardware**, not merely switched on. It is not a substitute for fixing S1, which is a socket-count
problem, not a memory problem.

## S3 — High: `/api/operator/history` returns HTTP 500 in normal browser use

Observed **120 times** across the audit — on essentially every page load, because the operator
shell polls it. The operator dashboard degrades to:

```
Operator history/events unavailable: Controller returned an incomplete response
```

Requested standalone with `curl`, the same URL returns **HTTP 200 with a 26,498-byte body** in
0.17 s — the largest response the firmware produces. It fails only when the browser issues it
alongside its other requests, which is consistent with heap exhaustion during JSON assembly (S2)
and socket pressure (S1).

This is on the **operator** view — the customer-facing screen.

## S4 — High: the operator view requests engineering-only endpoints it can never be authorised for

Every JS module initialises eagerly on every route, so the operator dashboard fires requests that
are guaranteed to fail with 401:

| Endpoint | 401s observed |
|---|---:|
| `/api/inverter-profiles` | 80 |
| `/api/wifi/scan` | 44 |
| `/api/solar-grid/config` | 40 |
| `/api/meters/em500/cache` | 7 |

Three consequences: console errors on a customer-facing screen, wasted requests consuming the
scarce four-socket pool (S1), and needless load on a memory-constrained device (S2).

Modules should request only what the active route and the current authorisation level need.

## S5 — High: hardcoded colours bypass the theme tokens, producing unreadable text

The stylesheets mix themed variables with hardcoded hex colours, so one theme or the other breaks.
Worst measured cases:

| Element | Ratio | Detail |
|---|---:|---|
| `button.button.secondary` "Refresh" | **1.17** | `rgb(23,36,54)` on `rgb(23,48,77)` — light theme |
| `p` "Controller status API is unavailable." | **1.10** | `rgb(238,245,255)` on `rgb(255,255,255)` — dark theme, readiness page |
| `.eyebrow` headings | 2.23 | `rgb(242,138,43)` on `rgb(238,243,248)`, 10 px |
| `.signal-bars` Wi-Fi strength | 1.57 | `rgb(183,197,212)` on `rgb(238,243,248)` |
| muted body text | 3.41–3.63 | `rgb(113,133,154)`, widespread |

A ratio of 1.17 or 1.10 is effectively invisible text, not merely low contrast.

Confirmed root cause — `web/app.css:156`:

```css
.button.secondary { color: var(--text); border-color: var(--line); background: #17304d; }
```

The background is hardcoded dark navy while the foreground follows the theme. In light theme
`var(--text)` resolves to a dark colour, giving dark-on-dark.

The same pattern appears in both directions:

- Hardcoded **dark** backgrounds that break light theme — `app.css:63` `.status-strip` `#0a192b`,
  `app.css:141` `.switch span` `#071522`, `app.css:156-157` `.button.secondary`,
  `app.css:170` `.toast` `#10243a` (same `var(--text)` on hardcoded dark pattern — not directly
  observed during the audit because no toast was on screen, but it is the identical defect).
- Hardcoded **white** backgrounds that break dark theme — `product-shell-v2.css:66` and `:120`
  `background: #ffffff`, which is where the white-on-white readiness message comes from.

The application appears to have been designed dark-first with light theme retrofitted, and the V2
stylesheets then hardcoded white. The fix is to route every colour through theme tokens.

Note: contrast figures are computed against the nearest opaque ancestor background, which is an
approximation where translucency or imagery is involved. The two severe cases above were confirmed
against the CSS source; the muted-text figures are close to the 4.5 threshold and are worth
confirming visually before large-scale restyling.

### S5a — The Pre-Lab Readiness page is unreadable in dark theme

The worst single visual defect in the audit, and it is not marginal. In dark theme every one of the
ten readiness cards renders with a **hardcoded white background** while the text keeps the dark
theme's near-white foreground. The result is near-white text on white — the entire page content,
including the blocker explanations an engineer needs, is effectively invisible.

See `evidence/ui-audit-2026-07-29/shots/desktop-dark-operator-readiness.png`. Card titles such as
"Controller API", "Grid measurement" and "Solar fleet" and all of their explanatory text are
present in the DOM and rendered, but cannot be read.

This is the `product-shell-v2.css` `background: #ffffff` rule meeting a dark-theme text colour, and
it is the same defect class as S5 in the opposite direction.

### S5b — The readiness page's own health checks fail because the browser exhausted the sockets

In the same screenshot the page reports **"Controller API — Controller status API is unavailable"**
and **"Primary network — No active network connection"**, and declares "Lab testing is blocked,
3 blocker(s)". Both statements were false at the time: the controller was online and its API was
healthy when queried without a browser attached.

The readiness page could not reach the controller because the browser rendering that page had
already consumed the four available sockets (S1). A diagnostics screen that reports the controller
as unreachable *because it is itself the load* will mislead a commissioning engineer into chasing a
network fault that does not exist. Fixing S1 should be validated specifically against this page.

## S6 — Medium: mobile tap targets below the 44×44 minimum

The four persistent header controls are undersized on **every** page:

| Control | Size |
|---|---|
| `#menuButton` ☰ | 40×40 |
| `#refreshButton` ↻ | 40×40 |
| `#shellHealthButton` | 40×42 |
| `#shellOverflowButton` ⋮ | 40×42 |
| `.op-range-button` "15 min" / "1 hour" | 105×**30** |

The range selectors at 30 px high are the worst. Several full-width buttons measure 42 px high —
2 px under, marginal, and lower priority than the icon buttons and range selectors.

## Not defects

- **`/api/operator/history` silently substitutes 15m for unrecognised ranges** such as `6h`
  (`operational_api.c:281-309` supports only `15m`, `1h`, `24h`). The UI only ever requests the
  three supported values, so this is unreachable from the interface — an API robustness nit, not a
  user-facing bug.

---

## Suggested order of work

1. **S1** — socket configuration. One-line fix class, removes a total-lockout failure mode.
2. **S3/S2** — the 500 is user-visible now; the socket and heap fixes should be verified together
   against it.
3. **S4** — reduces load caused by S1/S2 and removes console noise from the customer-facing view.
4. **S5** — theme tokens; user-visible and mechanical, but touches many files.
5. **S6** — tap targets.

Reproduce with the harness in the session scratchpad (`audit2.js`, `checks.js`, `summarize.js`).
Full machine-readable results and 60 full-page screenshots were produced by that run.

**None of these findings are firmware safety defects.** Automatic control remained disabled and
fail-closed throughout, and no inverter command was issued.

---

## Device state note — the meter is offline for an unrelated reason

At the time of writing the controller reports `meter_online: false`, `state: "unavailable"`, with
alarms `["Meter offline","Meter data stale"]`. **This was not caused by the audit.**

The meter at `192.168.100.200:502` is fully reachable from the network — ICMP replies and TCP 502
accepts connections. The meter configuration has been edited since it was last working: it is now
named "Automatrix EM500" with `active_power_address: 2`, `scale: 0.001`, `poll_ms: 300`,
`timeout_ms: 800`. `/api/meters/config` returns `restart_required: true` for such a change, and
`uptime_ms` shows the controller has **not** rebooted since that edit.

The meter channel is therefore waiting for the restart the configuration change requires. No
restart was performed as part of this audit, because someone else is actively working on the
device and a reboot would apply their in-progress configuration.

This also means the earlier open question about the grid power magnitude is still open, and the
scale factor is evidently still being tuned — `0.001` now, against the `0.01` normalised to
`0.00001` seen in the boot log. That value must be confirmed against the meter's physical display
before any control decision depends on it.
