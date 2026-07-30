# UI baseline audit — 2026-07-30

Baseline structural and visual audit of the **live controller** at `192.168.100.14`, taken
immediately before the UI redesign. Driven with Playwright + Chromium 151 by the harness in
`evidence/ui-baseline/`, which extends the 2026-07-29 harness rather than replacing it
(`checks.js` is reused verbatim; `metrics.js`, network capture, a 200 % text-zoom pass and two
extra viewports are new).

**Coverage:** 90 page runs, **90 completed, 0 run errors**.
10 routes × 4 viewports (1440×900, 1920×1080, 1024×768, 390×844) × 2 themes, plus a
200 % text-zoom pass at 1440×900. Everything was read-only: GETs and normal page loads only.
No configuration was posted, nothing was flashed or restarted, automatic control was never
enabled, and no inverter command was issued.

Machine-readable results: `evidence/ui-baseline/report.json` (full),
`baseline-metrics.json` (per-route rows for numeric before/after comparison),
`s3-history.json`, `chart-duplication.json`, `tablet-taps.json`, `chrome.log`.
Screenshots: `evidence/ui-baseline/shots/{route}-{viewport}-{theme}-{access}.png`
(`-full.png` = full-page at 1440×900, `-zoom200` = 200 % text zoom, `-buildB` = post-reflash).

---

## 0. Two things to read before any number below

### 0a. The engineering-authenticated state could not be captured

I do not have the engineering password, and no attempt was made to obtain or bypass it.
`web/product-mode.js:13` gates `wifi`, `control`, `system` and `commissioning` behind
`PROTECTED_ROUTES`; without a session those four routes render the sign-in card instead of their
content. **Everything this report says about those four routes describes the locked state.**
The engineering-authenticated layouts — the commissioning wizard, meter/inverter engineering
panels, the Advanced JSON panel — are **not measured and are not estimated**. That is a gap in
this baseline and the "after" run should close it by running with `ENG_PASSWORD` set
(`audit3.js` already accepts it).

### 0b. The device was re-flashed by someone else in the middle of the session

The 90-run baseline ran against one build ("build A"). Roughly four minutes after it finished,
a navigation returned `ERR_CONNECTION_TIMED_OUT`, and `/api/telemetry` then reported
`generated_ms: 212146` — 3.5 minutes of uptime, against ~51 minutes measured earlier in the
session. **The controller was restarted with a new build by another party; not by this audit.**

The new build ("build B") serves UI code that does not exist in this worktree at all: an entire
`pvc-*` chart component (`pvc-svg`, `pvc-grid-line`, `pvc-crosshair`, `pvc-gap-band`,
`pvc-alarm-mark`, `pvc-legend-item`, …) is present in the device's `/app.js` and absent from
`web/`. The two sparkline implementations described in F5 are **gone** from build B
(`grep -c op-sparkline` on the device's `app.js` = 0).

I therefore report **build A as the baseline** (one consistent build, 90 runs) and add a
10-run spot check of build B in §6 so the difference is quantified rather than assumed.
Nothing in this report was re-labelled after the fact: measurements state which build produced
them.

---

## 1. Method — how the percentages were produced

Defined in the header of `evidence/ui-baseline/metrics.js` and repeatable by the "after" run.

Area accounting runs on an **8 × 8 CSS-pixel grid over the first viewport only** (0,0 →
`innerWidth`,`innerHeight`), because "page area utilisation" is a claim about what an operator
sees without scrolling. Every visible **leaf** element (no element children, non-empty text, or a
form control) is classified and its bounding box painted onto the grid. Higher-priority classes
overwrite lower ones, so overlapping boxes are counted once:

| Class | Definition | Counted as |
|---|---|---|
| `value` | leaf whose text contains a digit | useful |
| `label` | leaf with 1–4 words, no digit — the caption a value needs | useful |
| `control` | button / link / input / select outside chrome | useful |
| `prose` | leaf with ≥ 5 words and no digit — explanatory copy | not useful |
| `chrome` | anything inside `header`/`nav`/`footer`/`aside`/`.sidebar`/`[role=banner]` | not useful |
| *(unpainted)* | padding, gaps, blank card interior | empty |

`usefulPct = (value + label + control) / total cells`.

**Three limitations, stated so the numbers are not over-read:**

1. **Graphics are not counted as useful area.** An SVG chart or gauge arc paints no text leaf, so
   a chart card scores as mostly empty. Chart pixels are reported separately in F5 instead of
   being folded into `usefulPct`. This is why `meters` shows two cards at 14.8 % and 15.6 % fill
   in F6 — a gauge and a sparkline live in them.
2. **`chromePct` is chrome *content*, not the chrome bounding box.** Empty sidebar space counts
   as empty, not as chrome. This is why chrome measures 8.0–8.2 % on the routes rendered by
   `operator-view.js` and 15.8–16.2 % elsewhere: the operator renderer hides the legacy
   `.page-intro` heading block, which lives inside a `header` and is otherwise visible
   (`chrome.log`: on `alarms`, chrome leaves include `h2` "Alarms and events" 989×28 at
   (262,112), `p.experience-question` 780×22, `p.experience-guidance` 780×19, `p.eyebrow` 989×15.
   That probe covered `dashboard`, `alarms` and `control` and was cut short on its fourth route by
   the reflash of §0b, so it left no JSON — its console output is `chrome.log`.)
3. Contrast is computed against the nearest opaque ancestor background — an approximation where
   translucency is involved. The figures below are all confirmable against CSS source.

Word counts: `visibleWords` = every word in visible non-chrome leaves across the whole document
(the redesign's 35–50 % reduction target should be measured against this); `proseWords` = words in
leaves of ≥ 5 words; `viewportWords` = words visible in the first viewport.

---

## 2. What the 2026-07-29 findings look like now

| Finding | 2026-07-29 | 2026-07-30 measured | Verdict |
|---|---|---|---|
| **S1** socket exhaustion | one browser could lock every client out | during 3 concurrent tabs for 60 s, an independent client got `/api/status` **HTTP 200 in 1.18 s** (`s3-history.json` → `C.independentClientDuringLoad`) | **resolved** |
| **S2** PSRAM disabled | `CONFIG_SPIRAM` unset | `sdkconfig.defaults:19-24` `CONFIG_SPIRAM=y`, octal, 80 MHz | **fixed in config** (heap headroom not independently re-measured — `/api/system/resources` needs engineering auth, see §0a) |
| **S3** `/api/operator/history` 500s | **120 × HTTP 500** | **262 requests, 262 × HTTP 200, zero failures** | **resolved** |
| **S4** 401s from unauthorised routes | 171 401s (80 + 44 + 40 + 7) | **0 × 401, 0 × 403, 0 × 4xx/5xx in 2 866 requests across 90 runs** | **resolved** — `web/product-mode.js:31-40` now scopes each engineering endpoint to the routes that need it |
| **S5** hardcoded colours | 728 low-contrast, worst **1.17** and **1.10** | 368 low-contrast, **11 distinct**, worst **2.23**; the 1.17 and 1.10 cases are gone | **partly fixed** — see F11 |
| **S5a** readiness unreadable in dark | entire page white-on-white | readable; `web/app.css:51-66` aliases `--panel-bg` etc. onto real tokens. See `shots/readiness-1440x900-dark-operator.png` | **resolved** |
| **S5b** readiness reported the controller offline | "Controller API unavailable", "3 blockers" | "Controller API is responding", "Connected to Automatrix-4G at 192.168.100.14", 1 blocker (a real one: 3 active alarms) | **resolved** |
| **S6** tap targets < 44×44 | 211 on mobile | **0 at 390×844**, across all 10 routes and both themes | **resolved at phone size**; 4 remain at tablet size in build B, F12 |
| Console errors / warnings | 296 / 60 | **0 / 0** across 90 runs | **resolved** |

### S3 measured in detail (`evidence/ui-baseline/s3-history.js`)

| Phase | Requests to `/api/operator/history` | Result |
|---|---:|---|
| A — 30 standalone sequential GETs | 30 | 30 × 200 |
| B — one browser tab on `/#/dashboard`, held 90 s | 15 | 15 × 200 |
| C — three concurrent tabs (dashboard + meters + inverters), 60 s | 37 | 37 × 200 |
| Main audit — 90 page runs | 180 | 180 × 200 |
| **Total** | **262** | **262 × 200, 0 × 500** |

The failure mode that produced 120 HTTP 500s a day earlier did not occur once. The socket and
PSRAM changes did resolve S3.

**One transient event, reported for completeness:** a single `ERR_CONNECTION_TIMED_OUT` on one
navigation out of ~94 — the moment the device was re-flashed by someone else (§0b). Two GETs
immediately afterwards returned 200 in 1.02 s and 0.05 s. The device was never left unresponsive
and was never power-cycled.

---

## 3. Findings ranked by user impact

### F1 — Page area utilisation: 7–25 % useful, 44–77 % empty (the product owner's complaint, measured)

At 1440×900, light, operator (`report.json`; screenshots `shots/{route}-1440x900-light-*.png`):

| Route | useful % | value % | prose % | chrome % | **empty %** | largest empty region |
|---|---:|---:|---:|---:|---:|---|
| dashboard | 13.6 | 1.8 | 8.4 | 8.2 | **69.8** | 928×152 @(512,752) = 10.8 % |
| meters | 12.9 | 1.7 | 3.0 | 8.2 | **75.9** | 1224×232 @(216,672) = 21.8 % |
| inverters | 12.5 | 2.4 | 2.3 | 8.0 | **77.2** | 1224×312 @(216,592) = 29.3 % |
| control *(locked)* | 7.1 | 0 | 2.1 | 15.9 | **74.9** | 1224×400 @(216,504) = 37.6 % |
| alarms | 24.5 | 14.3 | 15.0 | 16.2 | **44.4** | 280×368 @(0,448) = 7.9 % |
| wifi *(locked)* | 7.1 | 0 | 2.1 | 15.9 | **74.9** | 1224×400 @(216,504) = 37.6 % |
| system *(locked)* | 7.1 | 0 | 2.1 | 15.9 | **74.9** | 1224×400 @(216,504) = 37.6 % |
| commissioning *(locked)* | 7.1 | 0 | 2.1 | 15.9 | **74.9** | 1224×400 @(216,504) = 37.6 % |
| readiness | 18.5 | 3.4 | 5.8 | 15.8 | **59.8** | 320×384 @(0,432) = 9.4 % |
| engineering *(locked)* | 7.1 | 0 | 1.8 | 15.9 | **75.3** | 1224×440 @(216,464) = 41.4 % |

The single most striking figure is **`valuePct`**: on the plant overview, the numbers an operator
came to read occupy **1.8 %** of the screen, while explanatory sentences occupy **8.4 %** — prose
outweighs data 4.7 : 1. On `meters` and `inverters` it is 1.7 % and 2.4 %.

Counting by hand from `shots/dashboard-1440x900-light-operator.png` agrees: the first viewport
contains exactly **one** live measured number (`24.83 kW`), one `4` in the alarm badge, and four
"Not measured" states.

**Wider screens make it worse, not better.** The layout is a fixed-width column, so extra pixels
become empty:

| Route | 1440×900 | 1920×1080 | 1024×768 | 390×844 |
|---|---:|---:|---:|---:|
| dashboard | 13.6 | **9.4** | 11.9 | 20.1 |
| meters | 12.9 | **9.0** | 13.2 | 24.1 |
| inverters | 12.5 | **8.7** | 16.1 | 23.6 |
| alarms | 24.5 | 27.1 | 15.2 | 20.0 |
| readiness | 18.5 | **13.2** | 18.9 | 31.1 |
| the five locked routes | 7.1 | **4.5** | 11.4 | 20.7 |

A 1920×1080 control-room display shows **4.5 %** useful content on any engineering-locked route
and **9.4 %** on the plant overview. The phone is the densest presentation the product has.

### F2 — Four of the ten routes are a sign-in wall for an operator, and two routes with real operator content cannot be reached from the operator navigation

`web/product-mode.js:13`:

```js
const PROTECTED_ROUTES = new Set(['wifi', 'control', 'system', 'commissioning']);
```

For an operator, `#/control`, `#/wifi`, `#/system` and `#/commissioning` all render the identical
"Engineering sign in" card — measured as identical at 7.1 % useful / 74.9 % empty / 28 visible
words on every one of them. `#/engineering` renders the same thing. **Five of ten routes are the
same screen.** Screenshot: `shots/control-1440x900-light-operator.png` (the heading says
"Engineering access", not "PV-DG Control" — the operator gets no indication of what they asked
for).

Meanwhile `web/product-mode.js:166` hides the nav links for `meters` and `inverters` unless
authenticated, yet `web/operator-view.js:145,282,328` renders full operator content for those
routes. **Grid Power (12.9 % useful, a live gauge, meter health) and Solar Inverters (12.5 %) are
reachable only by typing the URL.** The desktop sidebar shows four items: Plant overview, Alarms
and events, Pre-lab readiness, Engineering access.

The phone disagrees with the desktop: `shots/dashboard-390x844-dark-operator.png` shows a bottom
bar of five items — **Overview, Grid, Solar, Alarms, Control** — which both exposes Grid and Solar
(hidden on desktop) and offers Control, which leads an operator to the sign-in wall.

### F3 — Prose volume: 1 338 visible words on the alarms screen, and boilerplate repeated up to 4×

Visible words per route (1440×900, whole document, non-chrome):

| Route | visible words | prose words | words in first viewport | prose blocks |
|---|---:|---:|---:|---:|
| **alarms** | **1 338** | 589 | 272 | 57 |
| dashboard | 321 | 153 | 210 | — |
| readiness | 158 | 72 | 128 | — |
| meters | 75 | 31 | 75 | — |
| inverters | 63 | 16 | 63 | — |
| control / wifi / system / commissioning | 28 | 20 | 28 | — |
| engineering | 21 | 13 | 21 | — |

On `alarms`, **15 distinct blocks of prose are rendered more than once**, because the state
explanation is attached to every row rather than to the state model
(`web/operator-operations.js:39-59`):

- "The condition cleared itself before anyone accepted it. It is not resolved work: it stays on
  this list until someone acknowledges that it happened." — **24 words × 4 occurrences = 96 words**
- "Returned to normal · never acknowledged" — 4×
- "Shelving and out-of-service are engineering actions and need a session." — 4×
- eleven further blocks at 2× each (recommended actions, meter/network event explanations)

The largest single blocks on the plant overview (`web/operator-view.js` flow card):
36 words ("Facility load is not metered on this site. It is not derived from the grid meter: with
one measurement point the arithmetic has more unknowns than equations…"), 23 words (generator),
21 words (solar fleet). Three cards of the flow diagram carry **80 words** whose entire content is
"this is not measured"; the facility-load card is fully inside the first viewport, the other two
straddle or sit below the fold (`shots/dashboard-1440x900-light-operator-full.png`).

### F4 — The same value rendered more than once on one screen

Repeated-value groups per route at 1440×900 (a group = one numeric string with the same unit
appearing in ≥ 2 places). Full list with both coordinates in
`report.json → metrics.repeatedValues`.

| Route | repeated groups | worst case |
|---|---:|---|
| alarms | **12** | `"45 min ago"` × **21**, at (347,1715), (619,1715), (347,2113), (619,2113), … |
| dashboard | 2 | `25.22 kW` at `strong` (352,468) **and** `strong.op-kpi-value` (283,1473) |
| meters | 1 | `1` — "1 online" (1240,475) and "1 enabled meter" (1240,497), 22 px apart |
| inverters | 1 | `100.0 kW` — "100.0 kW installed capacity" (340,412) and "100.0 kW rated" (352,571) |
| readiness | 1 | `1` — "1 blocker(s) · 2 warning(s)" and "1 meter channel is online" |

On the alarms screen the four alarm rows each repeat the same four fields ("45 min ago" 4×,
"15 sec" 4×, "1" 5×), and the summary tiles restate the counts already shown by the rate tiles:
`4` appears as an `op-kpi-value` at (283,519), again at (863,519), and again as "4 alarm(s)" at
(860,720) and (1142,725).

On the dashboard, grid power is shown twice — in the flow card and in the KPI strip 1 005 px
lower — with no other information added.

### F5 — Two competing chart implementations, a triple-counted series, and a chart that flickers

**Both implementations exist and both render.** Verified in source and in the browser
(`chart-duplication.json`).

| | Browser-session sparkline | Controller-history chart |
|---|---|---|
| Source | `web/operator-view.js:71` | `web/operator-operations.js:520` |
| viewBox | 320×74 | 420×92 |
| Data | `state.gridTrend` / `state.solarTrend`, capped at 36 points (`operator-view.js:62`), lost on reload | `/api/operator/history`, **180 controller-resident samples at 5 s** |
| Rendered on | dashboard ×2 (`:275,:277`), meters (`:301`), inverters (`:349`) | dashboard ×2 (`operator-operations.js:589`), meters (`:600`), inverters (`:602`) |
| Note under it | "Recent live samples stored in this browser session" | "Stored by the controller · 15m range" |
| Range selector | none | 15 min / 1 hour / 24 hours |

**They appear on the same screen, and the better one is unstable.** Sampling the DOM twice a
second for 30 s:

| Route | controller-history panel present | both implementations on screen simultaneously |
|---|---:|---:|
| dashboard | 27/60 samples (**45 %**) | 27/60 |
| meters | 27/60 (**45 %**) | 27/60 |
| inverters | 26/60 (**43 %**) | 26/60 |

Mechanism: `operator-operations.js:669` re-adds the history panel every 10 s, and
`operator-view.js:528` → `renderDashboard` calls `view.replaceChildren()` every 5 s, which deletes
it. The panel — the one showing the controller's real 180-sample history, with the range
selector — is on screen **less than half the time and disappears while being read**. The
`MutationObserver` meant to re-add it (`operator-operations.js:665-667`) only fires for mutations
whose `record.target` is `#mainContent` itself, and the operator renderer mutates a grandchild,
so it never fires.

**The session series is sampled three times per refresh cycle on the dashboard.** Counting
`<polyline>` points 30.7 s apart (6.15 cycles at the 5 s poll of `operator-view.js:528`):

| Route | points before | after | delta | **samples per 5 s cycle** |
|---|---:|---:|---:|---:|
| dashboard "Grid power trend" | 6 | 24 | +18 | **2.93 ≈ 3** |
| meters "Recent demand trend" | 2 | 8 | +6 | **0.98 ≈ 1** |

`refreshAll()` pushes at `operator-view.js:463-464`, then calls `renderCurrent()` →
`renderDashboard()`, which pushes the same value again at `:242-243`; the `amx-site-telemetry`
listener (`:521-528`) triggers a third `renderDashboard()`. On `meters`, `renderMeter()` does not
push, so the identical series advances at 1×. **The same quantity is therefore drawn at two
different time scales on two routes**, and the dashboard's 36-point window covers 60 s of plant
history rather than 180 s.

**Chart sizing.** The brief's allegation is accurate:

```css
web/product-mode.css   .op-trend-card { min-height: 220px }
web/product-mode.css   .op-sparkline svg { height: 92px }
```

Measured at 1440×900 (build A):

| Route | plot | card | plot ÷ card area | plot ÷ card height |
|---|---|---|---:|---:|
| dashboard grid trend | 525×92 | 567×196 | **43.5 %** | 47.0 % |
| meters demand trend | 417×92 | 459×320 | **26.2 %** | 28.8 % |
| meters gauge | 220×140 | 299×320 | 32.2 % | 43.8 % |
| inverters gauge ×2 | 220×140 | 301×222 | 46.1 % | 63.1 % |

A 92-pixel plot is asked to show power trend on a device that is holding 180 samples
(`GET /api/operator/history?range=15m` → `"controller_resident":true, "sample_interval_ms":5000`,
28 773 bytes). Screenshot `shots/meters-1440x900-light-operator-full.png` shows the result: a
455×315 card containing a single flat orange line and a caption.

### F6 — Empty and underused regions

Largest empty rectangles at 1440×900 (§1 grid, coordinates and sizes in CSS px):

- `control` / `wifi` / `system` / `commissioning`: **1224×400 at (216,504) = 37.6 %** of the
  viewport, plus 352×360 at (1088,144) — the whole lower half of the page is blank below a
  760×290 sign-in card.
- `engineering`: **1224×440 at (216,464) = 41.4 %**.
- `inverters`: **1224×312 at (216,592) = 29.3 %**.
- `meters`: **1224×232 at (216,672) = 21.8 %**.
- Every route: **216×384 at (0,432)** — the sidebar below the four nav items, blank on all ten
  routes.
- `dashboard`: 928×152 at (512,752) inside the flow card, and 576×192 at (752,56) to the right of
  the page heading.

Cards below 40 % filled, in view, at 1440×900:

| Route | card | size | fill |
|---|---|---|---:|
| meters | `.op-gauge-card` | 299×320 | **14.8 %** |
| meters | `.op-trend-card` | 459×320 | **15.6 %** |
| inverters | `.op-gauge-card` ×2 | 301×222 | 16.8 %, 18.8 % |
| dashboard | `.op-health-card` | 363×**1159** | 24.9 % |
| meters | `.op-card` (meter availability) | 1150×141 | 30.4 % |
| inverters | `.op-trend-card` | 516×222 | 35.6 % |
| dashboard | `.op-flow-card` | 771×**1159** | 32.2 % |

(The gauge and sparkline cards are understated by the method — §1 limitation 1 — but 14.8 % for a
299×320 card holding one gauge and three short lines is still the shape of the problem. The
1 159 px-tall dashboard cards are not: `System readiness` is a 363×1159 card whose content stops
at y≈640, leaving ~360×520 px blank inside a card, visible in
`shots/dashboard-1440x900-light-operator-full.png`.)

`op-trend-card` carries `min-height: 220px` (`web/product-mode.css`) — a fixed floor applied to a
card whose content is a 92 px plot and one caption.

### F7 — Data below the fold at 1440×900

| Route | value-bearing elements below y=900 | page height |
|---|---:|---:|
| alarms | **54** | 5 105 px (5.7 folds) |
| dashboard | 2 | 1 811 px (2.0 folds) |
| all others | 0 | 900–1 106 px |

On `alarms` the alarm rows themselves — every timestamp, duration and recurrence count — start at
y≈1 715. The first viewport is spent on three heading blocks, a 35-word notice, four summary tiles
and four EEMUA-rate tiles. At 390×844 the alarms page is **12.1 screens** tall with 66 values below
the first fold.

### F8 — Nine independent pollers hitting the same endpoints

Per route at 1440×900, over a 6-second dwell, **8–9 endpoints are requested more than once**.
Across all 90 runs (`baseline-metrics.json → network`):

| Endpoint | requests | statuses |
|---|---:|---|
| `/api/status` | **675** | 675 × 200 |
| `/api/inverters` | 377 | 200 |
| `/api/meters` | 376 | 200 |
| `/api/inverter-telemetry` | 304 | 200 |
| `/api/engineering/session` | 207 | 200 |
| `/api/config` | 180 | 200 |
| `/api/operator/history` | 180 | 200 |
| `/api/operator/events` | 180 | 200 |
| `/api/operator/alarms` | 90 | 200 |
| `/api/telemetry` | 27 | 200 |
| **API subtotal** | **2 596** | 0 × 4xx, 0 × 5xx |
| `/` + `/app.css` + `/app.js` | 270 | 200 |
| **Total** | **2 866** | **0 × 4xx, 0 × 5xx** |

`/api/status` was requested **8 times in one 6-second dwell** on the dashboard — 1.3 requests per
second for one value. Nine independent `setInterval` timers run concurrently:

| File:line | Interval | |
|---|---:|---|
| `web/app.js:796` | 2 s | `refreshStatus` |
| `web/inverter-telemetry.js:146` | 2 s | |
| `web/devices.js:419` | 5 s | |
| `web/operator-view.js:528` | 5 s | `/api/status` + 3 more |
| `web/operator-operations.js:669` | 10 s | history + events + alarms |
| `web/operator-product-suite.js:401` | 10 s | |
| `web/prelab-readiness.js:189` | 15 s | |
| `web/em500-core.js:470` | — | |
| `web/engineering-session-resilience.js:75` | 5 min | |

This is no longer causing failures (S1/S4 fixed), but it is 33 requests per page view on a
device with a 10-socket pool, and it is why the same value can be a refresh cycle out of date in
two places on one screen.

### F9 — Developer terminology visible to an operator

Measured against a 24-term list (Modbus, PDU, register, coil, unit id, scale factor, poll
interval, timeout, baud, endian, CRC, RTU, port 502, hex addresses, raw JSON, schema, milestone,
…). Operator-visible hits at 1440×900, build A:

| Term | Where | Routes |
|---|---|---|
| **schema** | `div#sidebarVersion.sidebar-version` — "Configuration schema 5 · automatrix-pvdg" (`web/app.js:520`) | **all 10** |
| **register** | "Live demand, direction, freshness, and meter availability—without engineering register details." (`web/operator-view.js:296`) | meters |

Two observations rather than one:

1. The persistent sidebar footer shows an internal schema number and the device's slug on every
   screen an operator ever sees. It is the only piece of developer vocabulary that is
   unconditionally present.
2. The `meters` subtitle introduces the word "register" **in order to say the page does not show
   registers**. The sentence costs 12 words and teaches the operator a term they should not need.

Standards vocabulary is also on the customer-facing alarms screen — `ISA-18.2` as a headline
value, `EEMUA 191 · measured` and `EEMUA 191 · rationalised` as section eyebrows
(`shots/alarms-1440x900-light-operator.png`). That is deliberate in the source
(`web/operator-operations.js:27-38`) and defensible, but it is jargon on the operator's screen and
the redesign should decide consciously whether it stays.

**In the engineering-only DOM (not operator-visible, and not reachable for measurement — §0a):**
`web/index.html:88` carries `title="Advanced JSON write support is scheduled for a later
milestone"` and the visible string "Advanced JSON is read-only in this milestone.", plus a
`Configuration JSON` textarea. Those will be on screen for an authenticated engineer. Flagged from
source; **not measured**.

### F10 — Up to three heading blocks stacked before any data

Every route renders a topbar `h1` + subtitle (`web/product-shell-v2.js:4-15,35`), then a static
`.page-intro` eyebrow + `h2` + question + guidance, then an operator `sectionHeader` eyebrow + `h3`
+ description (`web/operator-view.js:117`). On `alarms` at 1440×900 that is:

- topbar: "Alarms and events" / "Conditions that require attention"
- page-intro: "ATTENTION" / "Alarms and events" / "What changed, what is affected and what should
  be done next?" / "Work from highest severity to lowest and confirm each condition clears."
- section: "PLANT ATTENTION" / "Alarm conditions" / 25 words of description

The words "Alarms and events" appear twice and the page's purpose is restated three times, using
the top **459 px** of the viewport — 51 % of its height — before the first data tile
(`chrome.log`, `alarms`: `h2` at y=112, `p.experience-question` at y=148,
`p.experience-guidance` at y=175; the four summary tiles are 280×142 at y=**459**).
`readiness` does the same with three different capitalisations of its own name: "Pre-lab
readiness" (topbar), "Pre-lab readiness" (h2), "Pre-Lab Readiness" (h3).

### F11 — Contrast failures

368 occurrences across 90 runs, **11 distinct**. Worst cases (`summary.txt`, full list in
`report.json`):

| Ratio | Element | Detail | Occurrences |
|---:|---|---|---:|
| **2.23** | `p.eyebrow` | `rgb(242,138,43)` on `rgb(238,243,248)`, 10 px — section eyebrows | 65 |
| **2.23** | `span` nav glyph "⌂" | same orange, 19 px | 40 |
| **2.32** | mobile bottom-bar glyph and label ("Overview") | `rgb(255,140,42)` on white, 10–19 px | 8 |
| **2.38** | `p.eyebrow` "EEMUA 191 · measured" | on `rgb(248,250,252)` | 10 |
| **2.49** | `p.eyebrow` "Live power flow" / "Authentication" | on white, 10–14 px | 30 |
| **2.49** | `span` "→" flow arrow, 24 px | non-text ≥ 3:1 required | 10 |
| **3.41** | `span` "△" alarm glyph | `rgb(113,133,154)` on `rgb(238,243,248)` | **160** |
| 3.80 / 4.34 | `small` "Into site" | 9 px muted, light / dark | 45 |

The pattern is one colour, used everywhere, that fails at small sizes: the orange accent
`#f28a2b` at 10 px for every eyebrow, and the muted grey `#71859a` for every icon glyph and
caption. Both are theme tokens, so this is one decision repeated 368 times, not 368 defects.
The severe (1.1–1.2) cases from 2026-07-29 are gone.

### F12 — Tap targets: clean on the phone, four failures on a touch tablet (build B)

`checks.js` only applies the 44×44 rule when `window.__isMobile` is set, and `audit3.js` sets it
for the 390×844 phone only. `web/mobile-prelab-fixes.css:52` keys its S6 fix on
`@media (pointer: coarse)`, which a touch tablet also matches, so `tablet-taps.js` forces the
check at 1024×768 with `hasTouch: true` (`pointer: coarse` confirmed `true` on all 10 routes).

Result — **4 failures, all in the new build's chart legend**, none in the shared shell:

| Route | Control | Size |
|---|---|---|
| dashboard | `button.pvc-legend-item` "Grid power" | 107×**25** |
| dashboard | `button.pvc-legend-item` "Solar production" | 138×**25** |
| meters | `button.pvc-legend-item` "Grid power" | 107×**25** |
| inverters | `button.pvc-legend-item` "Solar production" | 138×**25** |

This measurement was taken **after** the reflash, so it describes build B (`tablet-taps.json`).
Build A had no such control. The 25 px height repeats the mistake `.op-range-button`
(`web/operator-operations.css:22`, `min-height: 30px`) made before it: an interactive legend built
as text, then given a `<button>` role. The rest of the shell passes at tablet size.

---

## 4. Clean results — measured, and worth keeping through the redesign

Across 90 runs at four viewports and both themes:

| Check | Result |
|---|---|
| Page horizontal overflow | **0** |
| Elements overflowing the viewport | **0** |
| Wide content not scrollable | **0** |
| Text clipped by its container | **0** |
| Text clipped at **200 % text zoom** (10 routes, 1440×900) | **0** — the zoom pass produced the identical 68 contrast findings and nothing else |
| Inputs without a label | **0** |
| Controls without an accessible name | **0** |
| Images without alt | **0** |
| **Duplicate element ids** | **0** |
| Tap targets < 44×44 at 390×844 | **0** |
| Console errors / warnings | **0 / 0** |
| Failed HTTP requests | **0** in 2 866 |

DOM node counts are stable and modest: 1 320–1 824 per route (`commissioning` highest at 1 824,
`engineering` lowest at 1 320).

---

## 5. Numbers the redesign will be measured against

Build A, 1440×900, light, operator, 6-second dwell. Reproduce with
`node evidence/ui-baseline/audit3.js && node evidence/ui-baseline/summarize3.js`.

| Route | visible words | prose words | useful % | value % | empty % | requests | dup-value groups | DOM nodes | plot % of card |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| dashboard | 321 | 153 | 13.6 | 1.8 | 69.8 | 33 | 2 | 1 511 | 43.5 |
| meters | 75 | 31 | 12.9 | 1.7 | 75.9 | 33 | 1 | 1 429 | 26.2 / 32.2 |
| inverters | 63 | 16 | 12.5 | 2.4 | 77.2 | 37 | 1 | 1 461 | 46.1 |
| control *(locked)* | 28 | 20 | 7.1 | 0 | 74.9 | 32 | 0 | 1 370 | — |
| alarms | 1 338 | 589 | 24.5 | 14.3 | 44.4 | 31 | 12 | 1 761 | — |
| wifi *(locked)* | 28 | 20 | 7.1 | 0 | 74.9 | 31 | 0 | 1 327 | — |
| system *(locked)* | 28 | 20 | 7.1 | 0 | 74.9 | 32 | 0 | 1 407 | — |
| commissioning *(locked)* | 28 | 20 | 7.1 | 0 | 74.9 | 32 | 0 | 1 824 | — |
| readiness | 158 | 72 | 18.5 | 3.4 | 59.8 | 31 | 1 | 1 395 | — |
| engineering *(locked)* | 21 | 13 | 7.1 | 0 | 75.3 | 30 | 0 | 1 320 | — |
| **Total / worst** | **2 088** | **954** | — | — | — | **322** | **17** | — | — |

A 35–50 % prose reduction against this baseline means **2 088 → 1 044–1 357 visible words**, of
which the alarms screen alone must supply most of the saving.

Whole-run aggregates: 2 866 requests, 0 × 4xx, 0 × 5xx, 0 console errors, 368 contrast
occurrences / 11 distinct, 0 duplicate ids, 0 tap targets below 44×44 at 390×844.

Cross-viewport useful % is tabulated in §3 F1 and in `baseline-metrics.json` (`rows[]`,
keyed by `route`/`viewport`/`theme`).

---

## 6. Post-reflash spot check (build B) — 10 runs, 1440×900, both themes

Run after the device was re-flashed by another party (§0b), for comparison only.
`report-buildB.json`, screenshots `*-buildB.png`.

| Route | useful % A → B | empty % A → B | visible words A → B | dup groups A → B | DOM A → B |
|---|---|---|---|---|---|
| dashboard | 13.6 → **13.6** | 69.8 → 69.8 | 321 → **582** | 2 → 6 | 1 511 → 1 647 |
| meters | 12.9 → **9.8** | 75.9 → 78.3 | 75 → **232** | 1 → 3 | 1 429 → 1 512 |
| inverters | 12.5 → **7.0** | 77.2 → 80.6 | 63 → **200** | 1 → 5 | 1 461 → 1 549 |
| alarms | 24.5 → 24.5 | 44.4 → 44.4 | 1 338 → 1 338 | 12 → 12 | 1 761 → 1 761 |
| readiness | 18.5 → 18.5 | 59.8 → 59.8 | 158 → 158 | 1 → 1 | 1 395 → 1 395 |

Build B **replaces both sparkline implementations with a single large chart**: a 1120×500
`svg.pvc-svg` in a 1150×749 `.pvc-card`, i.e. **65–67 % of card area** and **67–69 % of card
height**, against 43.5 % / 26.2 % in build A. `op-sparkline` and `op-history-chart` no longer
exist in the device's `app.js`. F5's competing-implementation and triple-sampling defects are
therefore **addressed in build B** — but that code is not in this worktree, so it could not be
reviewed at source.

The chart is placed **below the fold** (dashboard page height 1 811 → 2 814 px), and the added
copy roughly doubles or triples the word count on the three routes that gained it, pushing
`inverters` from 12.5 % to 7.0 % useful. The redesign should take the chart and not the prose.

---

## 7. What was NOT measured

Stated plainly rather than estimated:

- **Engineering-authenticated layouts** — no password (§0a). Four protected routes plus the
  engineering console, commissioning wizard, meter/inverter engineering panels and the Advanced
  JSON panel are unmeasured. All engineering-visible jargon in F9 is a source-level observation.
- **`/api/system/resources`** returns `engineering_authentication_required`, so free-heap and
  PSRAM figures could not be re-measured on the running device; S2 is verified in configuration
  only.
- **Acknowledged alarm state** — the brief permits `POST /api/operator/alarms/ack`. I chose not to
  use it: it would permanently change the lab device's alarm state for other people working on it,
  and the acknowledged row layout can be measured any time it occurs naturally. The alarms
  baseline is therefore of four **unacknowledged** conditions.
- **Build B beyond the 10-run spot check** — the full 90-run matrix was not repeated after the
  reflash.
- **Real-user timing** (first contentful paint, interaction latency) — not part of this brief.

---

## 8. Suggested order of work for the redesign

1. **F2 navigation** — five of ten routes being the same sign-in card, and two content routes
   being unreachable, is a bigger loss of screen value than any density change.
2. **F1/F6 density and empty space** — 1.8 % of the plant overview being live values is the
   headline number the product owner is reacting to. Wider viewports must add columns, not margin.
3. **F3 prose** — 2 088 visible words, 954 of them prose, with the state explanation attached per
   row on `alarms`. Attach explanations to the state model once, not per instance.
4. **F5 charts** — build B already fixes the sizing and the duplication; carry that forward and
   keep the controller's 180 stored samples and the range selector.
5. **F4 repeated values** and **F10 heading stacks** — mechanical, and they buy the vertical space
   the charts need.
6. **F11 contrast** — one token decision (10 px orange eyebrows, muted grey glyphs) repeated 368
   times.
7. **F8 polling** — nine timers, 33 requests per page view; not failing now, but it is why the
   same number can disagree with itself on one screen.

---

## 9. Reproducing this audit

```bash
cd evidence/ui-baseline
export NODE_PATH=/path/to/node_modules      # playwright 1.62
node audit3.js                              # 90 runs -> report.json
node summarize3.js                          # tables + baseline-metrics.json
node s3-history.js                          # /api/operator/history under browser load
node chart-duplication.js                   # competing chart implementations
node tablet-taps.js                         # 44x44 at 1024x768 (pointer: coarse)
node chrome-breakdown.js                    # area-accounting explanation
```

Optional env: `TARGET`, `ENG_PASSWORD` (would unlock §0a), `ONLY_ROUTE`, `ONLY_VP`,
`REPORT_NAME`, `SHOT_SUFFIX`.

---

## 10. Device state when this audit ended

The controller went **off the network** after all measurement had finished. `/api/status`
returned `000` after 15 s and 20 s timeouts, and ICMP failed 7/7 — the device is not merely
refusing HTTP, it is not answering ping.

Timeline, for whoever owns the board:

| Time | Event |
|---|---|
| — | 90-run baseline completes against build A; device healthy |
| +4 min | one navigation times out; `/api/telemetry` shows 212 s uptime → **re-flashed and restarted by another party** (§0b) |
| +5 min | build B spot check (10 runs) and `tablet-taps.js` complete normally; `/api/status` 200 in 1.02 s, then 0.05 s |
| +20 min | `/api/status` returns `000`; ICMP 100 % loss |

**This was not caused by the audit.** Every request this session was a GET; the last one was
~15 minutes before the device disappeared, and the device answered normally after it. The board
had already been re-flashed once by someone else during the session, and a second flash or a
power interruption is the obvious explanation. Per the audit's ground rules I stopped, waited,
re-checked twice, and did **not** power-cycle it or attempt recovery. It should be checked
physically.

Nothing in the report depends on the device being reachable now: every figure comes from the
committed JSON and screenshots.

---

**None of the findings in this report are firmware safety defects.** `control_enabled` was
`false` and `control_authority.mode` was `monitoring_only` throughout; no inverter command was
issued and no configuration was written.
