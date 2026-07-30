# UI redesign — final report

- **Branch:** `phase1-fix`
- **Starting SHA:** `f40921b6a1b4c562241226fae2efdf07b97b4a27` — *fix(tests): expose POSIX fileno/fsync to the journal harness*, 2026-07-30. This is the tip that both UI worktrees branched from and the base recorded in `docs/PR16_REVIEW.md`.
- **Final SHA:** recorded in §11 below (the commit that carries this file).
- **Report date:** 2026-07-31

Everything below is taken from the repository — commits, committed evidence JSON, source, and test
runs. Where a number came from a session measurement that is not reproducible from a committed
artefact, it is labelled as such.

---

## 1. Range of work

| | SHA | Subject |
|---|---|---|
| Start | `f40921b` | fix(tests): expose POSIX fileno/fsync to the journal harness |
| End | `d31c470` | Merge industrial visual language: theme-token accent, styled operator markup |

Twenty-two commits across six worktrees, merged into `phase1-fix`. The substantive ones:

| SHA | Subject |
|---|---|
| `1edfd5b` | style(web): delete superseded polish sheet, drop unreachable rules, document the cascade |
| `493e411` | docs: review PR #16 industrial operator presentation pass |
| `6a57371` | docs: withdraw PR16 review finding F1, disproved on hardware |
| `503df3c` | feat(ui): one time-series chart, and a gap that is drawn as a gap |
| `450ede4` | feat(operator): cut the operator screens to plant operation |
| `c8993b2` | docs: baseline UI audit before the redesign |
| `57728c6` | fix(ui): route every colour through a theme token that resolves in both themes |
| `3345485` | feat(operator): make the first alarm screen triage, not a lesson |
| `034ca3c` | fix(operator): never draw a gauge needle for a value nobody measures |
| `9756734` | refactor(ui): one owner for routing, navigation and the DOM observer |
| `4d7652e` | refactor(ui): 8px spacing scale, compact radius, restrained elevation |
| `d8bc531` | feat(ui): style the operator content rewrite's drawers, tables and glyphs |
| `0941fdc` | wip(ui): visual language pass on the v2 and product-mode sheets |

---

## 2. PR #16 — status and verdict

- **Branch:** `ui/industrial-operator-pass-1`
- **Original head SHA:** `30bd2051f68a67400e5f566c3963faf19acd638a` (*fix(ui): make presentation reconciliation idempotent*)
- **Base at review time:** `phase1-fix` @ `f40921b`
- **Scope:** 8 files, +620 / −3 — two new web assets, their build/serve wiring, one source-contract test, one new CI workflow.
- **Status:** **open, untouched.** This work neither merged, closed nor commented on it. That decision is the owner's.
- **Review verdict (`docs/PR16_REVIEW.md`):** **do not merge as the integration vehicle — supersede.**

Reasons, in the review's own terms:

1. **It regresses the theme-token fixes.** `industrial-operator-ui.css:190` reintroduces a hardcoded
   `#f28a2b` and `:195` a hardcoded `#2c6ea4`, and `:150` uses `var(--warning, #d99a2b)` — the
   literal-fallback pattern that `04b89af` identified as the mechanism behind the **1.10:1**
   unreadable Pre-Lab Readiness page. `phase1-fix` had already eliminated that defect class with
   semantic tokens resolving in both themes; the PR reopens it.
2. **It adds a new sub-44 px tap target** — `industrial-operator-ui.css:109`, `min-height: 40px` on
   `.industrial-evidence-toggle`, against audit finding S6 which the base branch had closed.
3. **It pins a DOM-scraping patch layer into firmware C.** Commits `06dcb20`/`3624305`/`0675534`/
   `1e800dc` embed the module and append it to the ends of the `css_handler` and `js_handler`
   concatenation lists in `web_server.c`, making "this module must load last" a contract enforced by
   the server binary. The module has no API of its own: it can only work by observing and
   overwriting whatever the fourteen modules ahead of it rendered. Commit `30bd205` — the head —
   exists to stop the resulting MutationObserver feedback loop from running away.
4. **It fixes none of S3, S4, S5, S5a or S6**, all of which `phase1-fix` closed without it.

**Finding F1 in that document is WITHDRAWN.** It claimed a base-branch regression — that
`/api/operator/*` had been routed through the default-deny gateway — and it was **disproved on
hardware** (`6a57371`, and §6 of the review). The reasoning was sound; the conclusion was wrong. It
is retained in the document struck through, not deleted, so that nobody re-derives it.

Two ideas in the PR are worth keeping and are classed as such in the review: the engineering-lock
reset of the disclosure flag (`9d0ba96`), and the presence of a CI contract over the presentation
layer (`f31fcda`, `4a89dde`).

---

## 3. Visual audit findings by route

Two audits underpin the work.

### 3.1 `docs/UI_VISUAL_AUDIT_2026-07-29.md` — engineering defects found in the browser

| # | Severity | Finding |
|---|---|---|
| S1 | Critical | A single browser could lock every client out of the controller (`web_server.c:132`, `HTTPD_DEFAULT_CONFIG()` socket limits) |
| S2 | Critical | PSRAM disabled in sdkconfig on a board with 8 MB |
| S3 | High | `/api/operator/history` returned HTTP 500 under normal browser load (~26 KB response) |
| S4 | High | The operator view requested engineering-only endpoints it can never be authorised for |
| S5 | High | Hardcoded colours bypassing the theme tokens, producing unreadable text |
| S5a | High | The Pre-Lab Readiness page unreadable in dark theme — the 1.10:1 case |
| S5b | — | The readiness page's own health checks failing because the browser had exhausted the sockets (a symptom of S1) |
| S6 | Medium | Mobile tap targets below the 44×44 minimum |

### 3.2 `docs/UI_BASELINE_AUDIT.md` — the numbers the redesign is measured against

Build A, 1440×900, light theme, operator (unauthenticated), 6-second dwell, 90 runs.
Reproducible with `node evidence/ui-baseline/audit3.js && node evidence/ui-baseline/summarize3.js`.

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

Findings, ranked by user impact in that document:

- **F1** Page-area utilisation 7–25 % useful, 44–77 % empty — the product owner's complaint, measured.
- **F2** Four of ten routes render an identical "Engineering sign in" card to an operator (five counting `#/engineering`); meanwhile Grid Power and Solar Inverters carry real operator content but are hidden from the desktop navigation and reachable only by typing the URL. The phone bottom bar and the desktop sidebar disagreed about which routes exist.
- **F3** 1 338 visible words on `alarms` alone, with 15 distinct prose blocks rendered more than once — one 24-word block four times.
- **F4** Up to 12 repeated-value groups on one screen.
- **F5** Two competing chart implementations, a triple-counted series, a chart that flickers.
- **F6** Empty and underused regions.
- **F7** Data below the fold at 1440×900.
- **F8** Nine independent pollers hitting the same endpoints; 33 requests per page view.
- **F9** Developer terminology visible to an operator (Modbus unit ids, register talk).
- **F10** Up to three heading blocks stacked before any data.
- **F11** Contrast failures — 368 occurrences, 11 distinct.
- **F12** Tap targets clean on the phone; four failures on a touch tablet.

Clean results worth preserving, also measured: 0 × 4xx, 0 × 5xx, 0 console errors, 0 duplicate ids,
0 tap targets below 44×44 at 390×844, across 2 866 requests in the whole run.

---

## 4. Duplicate information removed, and text reduction

**Measured aggregate: 45.7 % reduction — 1 885 → 1 023 default-visible operator words.** That
figure was measured during the redesign session against the default-visible (drawer-closed) state
of the operator routes. It is *not* reproducible from a committed artefact, because it was taken
before and after the content rewrite on a device that is currently unreachable; the committed
baseline that *is* reproducible is the 2 088-word figure in §3.2, which counts the whole document
including content that is now one disclosure level down rather than deleted. **Per-route "after"
figures were not recorded in any commit message in this repository** — I looked and they are not
there. Treat the 45.7 % as a session measurement with no committed provenance, and re-measure with
`evidence/ui-baseline/audit3.js` when a device is available.

Nothing was deleted to reach the reduction. A `details(level, …)` helper moves content one level
down — **Engineering** for commissioning and tuning, **Service** for raw provenance — closed by
default and labelled with its level. It is a **disclosure control, never an authorisation control**:
everything inside it was already readable by whoever loaded the page, and every action needing
authority is still refused by the controller's default-deny gateway whether or not the browser drew
a control for it.

Duplicate information actually removed:

- **Plant overview** — the four KPI tiles are gone. Every figure on them was already rendered by the
  power-flow card: grid exchange, solar production and control authority each appeared twice, and
  control authority three times counting the readiness list.
- **Power flow** — the measured/commanded/not-measured pill, the detail sentence and the provenance
  string ("Automatrix EM500 · unit 1 · 194 ms · Good" — a Modbus unit id on an operator screen)
  collapsed into one Service-level drawer under the diagram. The unmeasured case is now stated once,
  in the coverage line.
- **Alarms** — the three totals on the lower half of the page repeated the condition counts tiled at
  the top of the same page. The "Active conditions" card listed the same conditions as the alarm
  table directly above it, in a second and subtly different rendering of the row the operator is
  meant to act on. Both gone. The event ring stays: it answers something the condition table does
  not.
- **Alarm rows** — the lifecycle sentence, priority rationale, detail paragraph and six metadata
  fields moved from *every row* to the state model, once, behind "How alarm states work"
  (Engineering) and "Condition history" (Engineering) per row.
- **Grid power** — the subtitle promising "without engineering register details" is gone; it was
  itself the only mention of registers on the page.
- **Controller** — the support card no longer lists "register maps, scaling, and control parameters"
  to an operator who cannot act on any of them.

Semantics that were deliberately **not** moved behind a drawer, because an operator must see them
without opening anything: the alarm state pill including "Returned to normal · never acknowledged",
the suppression pill (it changes what the counts above it mean), and the outstanding count.

---

## 5. Layout, and the chart

### 5.1 Layout

- Plant overview reordered to **exceptions first** (a mount point at the top of the page that
  `operator-operations.js` fills), then power flow, then equipment availability, then the one chart.
  The exceptions band moved from the bottom of the longest page in the product to the top, and now
  reads as *current condition / why it matters / required action*.
- 8 px spacing scale, compact radius, restrained elevation (`4d7652e`).
- Direction arrows are inline SVG rather than text glyphs. On a screen whose job is "which way is
  the power going", the arrow is the measurement, and a glyph re-drawn by the platform font is not
  one.
- Solar inverters: a dense table (name, state, now, rated, use, last update) sorted failed-and-
  offline-first, replacing one full-width card with a progress bar per inverter — at the sixteen
  inverters this product supports, that was sixteen screens of scrolling to answer "is anything
  down".
- A node nobody measures reads **"Not measured"**, never 0 kW. An inverter with no telemetry shows a
  dash. `034ca3c` extends the same rule to gauge needles: no needle is drawn for a value nobody
  measures. A zero where nothing is watching is the most dangerous number this product could print.
- PV-DG control states one authoritative verdict from the controller's own `mode_label`, its
  `inhibit_reason` underneath **only** when something is blocking, and at most three required
  actions. The prerequisites behind the verdict moved to an Engineering drawer.

### 5.2 Chart consolidation — four charts to one component

The product carried **two chart implementations**. `operator-view.js` kept an in-tab array of at
most 36 readings and drew it as a sparkline — and because both `refreshAll()` and
`renderDashboard()` appended on every cycle, **each reading was recorded twice**.
`operator-operations.js` drew the controller's stored history as a second sparkline. On the
dashboard both appeared at once: **four charts**, two different windows over the same two
quantities, and no time axis anywhere — X was the array index in both.

Both also compacted samples down to the finite ones before drawing, which *deletes* a missing
reading instead of showing it. On a controller whose purpose is to prevent reverse power, a bridged
gap — or an unmeasured sample drawn at 0 kW — reads as "no power" when the truth is "no
measurement".

`web/pvdg-chart.js` is now the single component. **Implemented:**

- Real timestamps on X, reconstructed from `age_ms`.
- A kW axis that always contains zero, with a drawn zero line and labelled import/export sides, the
  export half-plane textured as well as labelled.
- 15 m / 1 h / 24 h, backed by the only three range values the API accepts.
- Current / minimum / average / peak computed from measured samples only, shown with the sample
  count they came from.
- Gaps broken, hatched, counted, and stated in the text alternative.
- Crosshair, tooltip and arrow-key point details with an `aria-live` announcement.
- A data-quality pill distinguishing an uninstrumented quantity from a record with holes.
- Communication-loss, alarm and control-mode overlays taken from the sample record itself.
- Series told apart by dash pattern and glyph as well as by a palette validated for CVD separation
  and contrast in both themes.
- Loading, empty and error states, and a capped values table.

**Omitted for lack of an operator-accessible data source** — this is the important part:

| Capability | Why it is not there |
|---|---|
| Grid **import target** overlay | Exists only on `/api/solar-grid/config` |
| **Export limit** overlay | Exists only on `/api/solar-grid/config` |
| Generator **minimum-loading threshold** overlay | Exists only on `/api/solar-grid/config` |
| Control **deadband** band | Exists only on `/api/solar-grid/config` |
| **Source-transition** markers | The history samples carry no source field |

`/api/solar-grid/config` is **Engineering-gated**, and operator screens are *contractually
forbidden* to request it. No overlay is drawn for a value the operator UI cannot read — inventing
one, or drawing a threshold line from a client-side guess, would be worse than omitting it.

**Closing this needs a small firmware change: a read-only, operator-safe thresholds endpoint** that
publishes just those four numbers without exposing the rest of the engineering configuration
surface. **That change was deliberately NOT made.** It widens what an unauthenticated client can
read from the controller, and that is an owner's decision about the product's security posture, not
an agent's. It is recorded here so the decision is explicit rather than forgotten.

**Geometry:** the retired trend card was 220 px with a 92 px sparkline — 41.8 % figure, 34.5 % plot.
The new card's figure is 500 px against about 655 px of usable card height: **~76 % figure, ~70 %
inner plot rectangle.** See the caveat in §10 — these are computed from the CSS box model.

---

## 6. Accessibility

The headline fix is a **token split**, `57728c6`:

```
--orange      FILL ONLY. Unchanged, deliberately identical in both themes, because
              .button.primary (#171b20 on it, 6.96:1) and .brand-mark (#111821 on it,
              7.18:1) are contrast-matched to that exact swatch. The brand colour was
              not altered.
--accent-text EVERY accent glyph, caption, rule, border and chart stroke.
              #f28a2b dark, #9a5300 light (5.82:1 on white).
```

| Element | Before | After |
|---|---|---|
| `.eyebrow` worst case, light | **2.23:1** | **5.21:1** |
| `.eyebrow` worst case, dark | — | 5.93:1 |
| Brand orange on a light surface (all accent glyphs) | 2.49:1 | via `--accent-text` |
| `--muted-2` (used for 10–11 px body text) | 4.34:1 dark / 3.80:1 light | 4.86:1 dark / 4.96:1 light |
| `--input` fallback, `commissioning-wizard-v2.css` | 2.18:1 light | removed |
| `.wifi-network-list` SSID / caption in light | 3.48:1 / **1.30:1** | tokenised |
| `.cr-health-verdict` / `.cr-final` / `.cr-blockers` in dark | **1.00:1 — invisible** | tokenised |
| `.product-detail-row.good` / `.warning` in light | 1.84:1 / 1.76:1 | tokenised |
| Pre-Lab Readiness page | **1.10:1 — unreadable** | closed by `04b89af` |

Also: the last `var(--undefined, #literal)` call site is removed and ~45 dead literal fallbacks are
stripped, which retires the defect *class* rather than its instances. The focus indicator on
`.cw-field` inputs was a 45 %-alpha accent outline; it is now a solid 2 px `--accent-text` with 2 px
offset. Every accent glyph clears the 3:1 required of meaningful non-text with the same token.

`.eyebrow` remains at 11 px, below the 12 px floor the rest of the labels observe. That is a
deliberate, documented exception (`web/product-experience-v2.css:153-169`): it is uppercase at 850
weight with 0.13 em tracking, which reads larger than its nominal size, and it is the bottom of the
type scale.

---

## 7. Request counts, observers and binary size

**Requests (operator dashboard, per minute):** **156 → 132.** `/api/operator/history` **10 → 6**.
Session measurements against the lab controller; not reproducible from a committed artefact, and
the committed baseline table in §3.2 counts *per page view* (33 on the dashboard), not per minute.
Treat these two figures the same way as the word count in §4.

The mechanism behind the reduction is committed and is verifiable at source:

- `prelab-readiness.js` polled **six endpoints every 15 s from every screen** — including
  `/api/operator/history`, the ~26 KB response that S3 measured returning 500 under browser load —
  and displayed none of it unless its own page was open. It is now scoped to its own route and
  refreshes immediately on arrival.
- `renderStatus()` wrote ~50 fields every two seconds on every route, and `renderPowerFlow()` rebuilt
  ~50 elements with it, whether or not a value had changed. The setter primitives now compare before
  writing and both rebuilds are signature-guarded, so an unchanged refresh mutates nothing.

**MutationObservers across `web/`: 12 → 8** (`9756734`). Four observers watched `#mainContent`, each
re-deriving "did a page appear?" from the same records. `product-mode.js` now owns the only one and
republishes it as `onContentChange`, draining records its subscribers produce with `takeRecords()`
so a subscriber cannot react to its own writes.

Two further DOM-scraping paths were deleted outright: the shell derived its health indicator by
reading the rendered status strip back out of the DOM and regex-matching English words in it, behind
a `characterData` observer over a strip that updates every two seconds — `app.js` now publishes
`amx-controller-health` as data. `ui-enhancements.js` chose which meter sections stay expanded by
matching heading text against a hardcoded list of English section names — `em500-core.js`, which
renders the panel, now declares `data-collapsed-by-default`.

`web/commissioning-route.js` was deleted entirely, with its `CMakeLists` entry, its
`web_assets.c`/`.h` registrations, its `web_server.c` registration and its CI `node --check`.

**Firmware binary size:**

| Point | `build/automatrix_pvdg.bin` | App partition (`ota_0`, 0x300000 = 3 145 728 B) |
|---|---:|---|
| Earlier release point, recorded in `docs/RELEASE_READINESS.md:270` | 1 686 144 B | 46 % free |
| Current tree (build of 2026-07-30 23:26) | **1 985 744 B** | **36.9 % free (63.1 % used)** |

The growth spans far more than the UI work — the same range carries the alarm journal, generator
load sharing, the multi-engine commissioning API and the brand register maps. No intermediate
per-commit size series was recorded, so the UI work's own contribution cannot be isolated from what
is committed here.

---

## 8. Test and build results

Run on this tree at the final SHA:

- **CI source contracts: 71 / 71 pass, 0 fail.** The list is derived exactly as CI derives it:
  ```
  grep -oP 'python3? tests/\S+\.py' .github/workflows/esp-idf-build.yml | sed 's/^python3\? //' | sort -u
  ```
- **JavaScript suites: 4 / 4 pass** — `web/tests/chart-utils.test.js`, `devices-utils.test.js`,
  `em500-utils.test.js`, `wifi-utils.test.js`.
- `node --check` over `web/*.js`: clean.
- **ESP-IDF build: clean, zero warnings.** Not re-run while writing this report (`idf.py` is not on
  this shell's `PATH`); verified instead from the committed build log of the final commit,
  `build/log/idf_py_stdout_output_24952`, timestamped 2026-07-30 23:26 — the same minute as
  `d31c470`. Zero occurrences of `warning:`, link and image generation successful, and
  `check_sizes.py` reporting `binary size 0x1e4cd0 bytes ... 0x11b330 bytes (37%) free`. The only
  diagnostics in that log are ESP-IDF's own `NOTE:` lines about upstream Kconfig files.

Contracts changed during this work, and why — in every case because the design changed, never to
make a failing assertion pass:

| Contract | Change |
|---|---|
| `operator_history_events` | Dropped the assertion on the literal heading "Alarm and event center" (that heading is gone; the route table names the page in the sidebar, title, breadcrumb and `document.title`, so the assertion would have protected the duplication). Now asserts that the module renders both the condition table and the event history, that the active population is counted and filterable, that the range labels and range statistics live in the component that renders them, that **no second chart implementation exists**, that missing samples are **not** compacted away, that X is a timestamp, that only the three supported range values are requested, and that the plot area is component-sized. |
| `product_engineering_access` | The assertions on "Grid Power" and "Solar Inverters" — operator-only spellings of two page names that `ia_taxonomy_source_contract.py` now forbids outright — are **inverted into a prohibition**: one durable name per page, owned by the route table. "Operator guidance", which headed a card that printed a paragraph in every state including the state where nothing is wrong, is replaced by an assertion that the screen states a bounded set of required actions carrying all three beats. Also asserts the operator view provides the shared chart's mount point rather than carrying a sparkline of its own. |

---

## 9. Screenshots

`evidence/ui-baseline/` — the harness, its JSON output and the screenshots.

| | Count |
|---|---:|
| Files in `evidence/ui-baseline/shots/` | 130 |
| Tracked in git | **34** |
| Gitignored | **96** |

The gitignored 96 are regenerable from the committed harness — `audit3.js`, `metrics.js`,
`summarize3.js`, `chart-duplication.js`, `chrome-breakdown.js`, `s3-history.js`, `tablet-taps.js` —
against a reachable controller. Their measurements are already committed as JSON
(`baseline-metrics.json`, `report.json`, `report-buildB.json`, `chart-duplication.json`,
`s3-history.json`, `tablet-taps.json`) and as logs, so no figure in this report depends on an
untracked file. Reproduce with:

```bash
cd evidence/ui-baseline
node audit3.js && node summarize3.js
```

Other evidence directories: `evidence/2026-07-27/`, `evidence/ui-audit-2026-07-29/`,
`evidence/postflash-backup-2026-07-29/`.

---

## 10. Remaining risks and what is NOT verified

This section matters more than everything above it.

### 10.1 No human has visually reviewed a rendered page

**Not one screen in this redesign has been looked at by a person.** Every claim above is verified by
source contract, by compiler, or by counting API calls. The layout and the styling are sound *by
construction* — the tokens resolve, the contracts hold, the box model computes — and **unseen in
practice**. A rendering defect that no contract asserts against would have passed through this work
undetected. Before this goes in front of an operator, somebody must open every route in a browser,
in both themes, at 390×844, 1024×768 and 1440×900, and look at it.

### 10.2 Plot-area percentages were computed, not measured

The ~76 % figure / ~70 % plot geometry in §5.2 is arithmetic over the CSS box model, not a
measurement taken in a browser. The pre-existing 41.8 % / 34.5 % figures for the retired card were
measured; the new ones were not. They may be wrong.

### 10.3 DOM node count and console errors after the module consolidation were never measured

The baseline records 1 320–1 824 DOM nodes per route and zero console errors. After the routing,
navigation, observer and content consolidation, **neither was re-measured** — no browser was
available. The agent declined to estimate them, which was correct; the consequence is that the
consolidation's actual effect on DOM size and its actual freedom from console errors are **unknown**.

### 10.4 Engineering-authenticated UI states were never audited

No engineering password was available, and **no bypass was attempted** — correctly. Four routes
(`#/control`, `#/wifi`, `#/system`, `#/commissioning`) plus `#/engineering` are measured **only in
their locked state**, which is the same sign-in card five times over. The engineering console, the
commissioning wizard, the meter and inverter engineering panels and the Advanced JSON panel are
entirely unaudited. All engineering-visible terminology findings (F9) are source-level observations,
not observations of a rendered page. `/api/system/resources` likewise returns
`engineering_authentication_required`, so free-heap and PSRAM figures could not be re-measured on
the running device; S2 is verified in configuration only.

### 10.5 `engineering-session-resilience.js` is embedded but never served

**Verified in this tree, at the final SHA:**

- `components/web_server/CMakeLists.txt:80` — `configure_file(... engineering-session-resilience.js ...)`
- `components/web_server/CMakeLists.txt:183` — listed in `EMBED_FILES`
- `components/web_server/web_assets.c:12,52,112` — `DECLARE_ASSET` and `ASSET_GETTER`
- `components/web_server/include/web_assets.h:22` — the getter is declared
- `components/web_server/web_server.c` — **zero occurrences of the string.** The two concatenation
  arrays at `web_server.c:72` and `web_server.c:94` do not reference it.

The file is compiled into the firmware image, occupies flash, and **is never sent to any browser.**
This is **pre-existing** — it was not caused by this work and nothing in this work touched it. It is
either dead weight to be removed, or a missing registration to be added; somebody who knows what the
module was for has to decide which.

### 10.6 The Refresh button re-fires every route-gated loader in the application

`web/devices-refresh.js:12`:

```js
window.dispatchEvent(new Event('hashchange'));
```

Seventeen `hashchange` listeners are registered across `web/` — `app.js`, `commissioning-wizard-v2.js`,
`devices.js`, `em500-core.js`, `em500-quality.js`, `inverter-telemetry.js`, `operator-operations.js`,
`operator-product-suite.js`, `operator-view.js`, `prelab-readiness.js`, `product-experience-v2.js`,
`product-mode.js` (×2), `product-shell-v2.js`, `ui-enhancements.js`, `wifi.js`. **One click on
Refresh runs all of them**, including the route-gating work §7 just did to stop off-route modules
polling. It defeats part of the request reduction on every press.

**The fix is a real `amx-refresh` event** that modules opt into, rather than a synthetic navigation
event. It was not made here.

### 10.7 Duplicate polling remains

Route gating reduced *when* modules fetch, not *how many* modules fetch the same thing. On the
operator dashboard:

| Endpoint | Modules fetching it |
|---|---|
| `/api/status` | three |
| `/api/meters` | two |
| `/api/inverters` | two |
| `/api/inverter-telemetry` | two |

Confirmed at source across `web/*.js`. This is why F4 exists — the same number can disagree with
itself on one screen, because two modules fetched it at different instants. A shared cache or a
single fetch owner per endpoint is the real fix and was not built.

### 10.8 The controller hung twice during the session

Twice during this work the controller stopped responding and needed a **reset pulse** to recover.
**The cause is unknown.** One of the two events is documented in detail in
`docs/UI_BASELINE_AUDIT.md` §10 and commit `d3b5437`: the board stopped answering both HTTP and ICMP
about fifteen minutes after the last audit request. Every request that session was a GET, the last
one was well before the device disappeared, and the device had answered 200 twice after it. The
board had also been re-flashed once mid-session by another party, so a second flash or a power
interruption is a plausible explanation — but it is an explanation, not a diagnosis. The second
event is not written up in the repository.

**If this recurs in normal use it is a defect to chase, not a quirk to live with.** An unattended
site controller that stops answering and needs a physical reset is an availability failure regardless
of what caused it.

### 10.9 No inverter profile is qualified against real equipment

Register maps for Solis, Growatt, Sungrow, Chint/CPS, FoxESS, GoodWe, Knox/AISWEI, SolarEdge and
Huawei SUN2000 are transcribed from manuals and covered by tests. **None has been exercised against
the physical inverter it describes.** Until at least one is, this remains **monitoring,
commissioning and protection firmware — not control firmware.** `control_enabled` was `false` and
`control_authority.mode` was `monitoring_only` throughout every measurement in this report; no
inverter command was issued and no configuration was written.

---

## 11. Lessons that cost time

For whoever works here next. Each of these cost real hours in this session.

1. **`gcc` IS on `PATH`. Any "gcc is required" failure is an environment fault, not a test failure.**
   **Four separate agents** reported source-contract failures as pre-existing defects when the only
   problem was that their shell had not picked up the compiler. The toolchain is at:
   ```
   C:\Users\Mubasher\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin
   ```
   `gcc (MinGW-W64 x86_64-ucrt-posix-seh) 16.1.0`. Add it and re-run before you believe any failure.
   With it on `PATH`, all 71 contracts pass.

2. **The served CSS/JS order is the `assets[]` arrays in `components/web_server/web_server.c`, NOT
   `CMakeLists.txt`.** `CMakeLists.txt` controls what is *embedded*; `web_server.c:72` and
   `web_server.c:94` control what is *concatenated and in what order*. Editing the CMake list and
   expecting the browser to see the change is a wasted build cycle. §10.5 is exactly this trap
   sprung: a file embedded by CMake and absent from both arrays.

3. **`operational_api.c` is exempt from the auth gateway, and you cannot see this from C source.**
   `components/web_server/CMakeLists.txt:215-221`:
   ```cmake
   foreach(source IN LISTS WEB_SERVER_C_SOURCES)
       if(NOT source STREQUAL "engineering_guard.c" AND NOT source STREQUAL "operational_api.c")
           set_source_files_properties(${source} PROPERTIES COMPILE_OPTIONS "-include;engineering_auth.h")
       endif()
   endforeach()
   set_source_files_properties("engineering_guard.c" "operational_api.c" PROPERTIES
       COMPILE_DEFINITIONS ENGINEERING_GUARD_IMPLEMENTATION=1)
   ```
   Every other translation unit gets `engineering_auth.h` force-included by the build; these two do
   not, and instead compile with `ENGINEERING_GUARD_IMPLEMENTATION=1`. **This is what withdrawn
   finding F1 got wrong.** Reading `operational_api.c` on its own tells you nothing about which
   gateway its handlers pass through. Check the build system before concluding anything about
   authorisation in this component.

4. **Two stylesheets that look like one-line stubs are minified and carry live rules.**
   ```
   web/prelab-readiness.css            1 line,  31 rules
   web/commissioning-release-v3.css    1 line,  97 rules
   ```
   `wc -l` says 1. Neither is a stub and neither is dead. Count `{` before you assume a single-line
   CSS file is empty, and never delete one on the strength of its line count.

---

## 12. Branch hygiene at the time of writing

See section 16 of the redesign brief for the full classification. Recorded here for the archive:

- `main`, `phase1-fix` — protected.
- `ui/industrial-operator-pass-1` — **open PR #16**, 10 unique commits, untouched.
- `origin/feature/phase1-em500-source-detection` — **contains unique commits, deliberately
  superseded, retained.** It was merged with an "ours everywhere" strategy (`8606946`) because its
  content would have regressed `SOURCE_DETECTION_SINGLE_REGISTER_DEFAULT` from `0x2100u` — the
  correct value, taken from the owner's `mbpoll` captures — back to `0x2160u`. Git reachability
  reports it as merged; its *content* was deliberately never applied. **It must never be
  cherry-picked, rebased onto, or "recovered".**
- `origin/feature/secure-web-ota` — 39 unique commits, retained.
- `backup/phase1-fix-code-only`, `audit/deep-code-audit-2026-07-28`, `docs/local-validation-report`,
  `fix/pvdg-reconnect-response-delivery` — unique commits, retained.
- 24 `worktree-agent-*` branches — each proven by `git log --oneline <branch> --not phase1-fix`
  returning empty, and deleted with `git branch -d` (never `-D`). Seven had live worktrees under
  `.claude/worktrees/`; all seven were confirmed clean with `git status --porcelain` before
  `git worktree remove`.
- `agent/minimal-pvdg-foundation`, `feature/pvdg-batch3-consolidated`, `feature/pvdg-web-application`,
  `feature/pvdg-wifi-commissioning` — upstream gone, zero unique commits by the same proof, deleted
  with `git branch -d`.
- **No remote branch was deleted.** Every remaining remote is protected, active, open-PR,
  deliberately retained, or carries unique commits. `git remote prune origin` was run.
- 24 worktrees remain registered under `.claude/worktrees/` and beside the repo, each holding a
  `feature/*`, `docs/*` or `fix/*` branch that is still checked out. Those branches are protected as
  worktree branches in use and were not touched, even where they are fully merged.
