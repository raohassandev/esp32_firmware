# UI gap analysis — what is missing, orphaned or broken

- **Branch:** `phase1-fix`
- **Analysed at:** `aa4151d` — *evidence: partial after-audit, interrupted when the access point dropped*
- **Report date:** 2026-07-31
- **Trigger:** the owner's report after the seven-workstream redesign — *"UI work is not complete,
  pages are missing. thing are not as well managed as i expected. I think working is remaining"*

---

## 0. How this was established, and what it does not prove

The lab controller was tried once at `http://192.168.100.14` and did not answer (its access point
changed, as `docs/UI_REDESIGN_FINAL_REPORT.md` already records). **Nothing in this document was
measured on hardware.** Everything below is source analysis plus a local execution of the real
asset bundle.

The bundle was reproduced exactly as the firmware serves it: the JS and CSS getter lists were
parsed out of the `assets[]` arrays in `components/web_server/web_server.c:72` (CSS) and
`components/web_server/web_server.c:94` (JS) — **not** from `components/web_server/CMakeLists.txt`,
whose order is deliberately different — concatenated in that order, and served as `/app.js` and
`/app.css` to `web/index.html` under jsdom, with `/api/*` answered from the recorded payloads in
`evidence/postflash-backup-2026-07-29/`. Every route was loaded twice, once with
`/api/engineering/session` answering `authenticated: false` and once `true`.

**One correction that changed a finding.** jsdom reports `document.readyState === "loading"` while
deferred scripts execute. The HTML specification's *the end* algorithm sets it to `"interactive"`
**before** deferred scripts run, and browsers do so. `web/index.html:9` loads the whole bundle as a
single `defer` script, and nine modules branch on `document.readyState` to decide whether to start
now or at `DOMContentLoaded` — so under unpatched jsdom the module start order is wrong, and it
produced a *false* finding (the Commissioning sidebar entry disappearing). The harness now reports
`"interactive"` in place of `"loading"`, which reproduces browser ordering exactly, and the table
below is from that corrected run. The uncorrected ordering is still used deliberately in §4 to
demonstrate a latent defect.

**A load is not a session.** These results cover first paint and roughly 1.5 s of polling per route.
They do not cover navigating between routes, signing in *while* a page is open, session expiry, or
sustained polling. Findings that depend on those are marked as reasoned from source, not observed.

---

## 1. Route table

`ROUTES` in `web/app.js:52-75` is the router's source of truth; `NAV_GROUPS` (`web/app.js:45-50`)
drives the sidebar grouping. All ten routes were exercised.

Access column: **Open** = renders for anyone; **Protected** = in `PROTECTED_ROUTES`
(`web/product-mode.js:26`), so `enforceRoute()` (`web/product-mode.js:454`) sends a signed-out
visitor to the sign-in page.

| Route | Nav entry | Page element | Renderer | Access | Verdict |
|---|---|---|---|---|---|
| `dashboard` | `web/index.html:35` (static) | `web/index.html:111` (static) | `web/operator-view.js:398` `renderDashboard`, power flow in `web/app.js` | Open | **works** |
| `meters` | `web/index.html:36` (static), hidden from operators by `web/product-mode.js:306-315` | `web/index.html:144` (static) | `web/operator-view.js:440`, `web/em500-core.js:364`, `web/devices.js:90` | Open — **not** protected | **orphaned for operators** — page renders, sidebar entry hidden. See F1 |
| `inverters` | `web/index.html:37` (static), hidden by `web/product-mode.js:306-315` | `web/index.html:149` (static) | `web/operator-view.js:524`, `web/inverter-config.js:182`, `web/inverter-profiles.js:49`, `web/inverter-telemetry.js:32` | Open — **not** protected | **orphaned for operators** — as above. See F1 |
| `alarms` | injected `web/operator-operations.js:233-246` | injected `web/operator-operations.js:247-258` | `renderAlarmConsole` (all access) + `renderAlarmPage` `web/operator-operations.js:965` (operator only) | Open | **works**, but see F4 (disclosure) and F6 |
| `commissioning` | injected `web/operator-product-suite.js:293-317` | injected by the same function; also built by `web/commissioning-release-v3.js:20` and `web/commissioning-wizard-v2.js:37` | `render()` in `web/commissioning-release-v3.js` | Protected | **works** — was latently broken, **fixed**, see F2 |
| `readiness` | injected `web/prelab-readiness.js:21-31` | injected `web/prelab-readiness.js:32-37` | `web/prelab-readiness.js` `render` | Open | **works** |
| `wifi` | `web/index.html:39` (static) | `web/index.html:130` (static) | `web/wifi.js:80` | Protected | **works** |
| `control` | `web/index.html:41` (static) | `web/index.html:227` (static) | `web/solar-grid.js:731` plus control panels in `web/app.js` | Protected | **works** |
| `system` | `web/index.html:42` (static) | `web/index.html:268` (static) | `web/app.js:850` | Protected | **works** |
| `engineering` | injected `web/product-mode.js:306-342` | injected `web/product-mode.js:329-358` | `web/product-mode.js` | Open (it *is* the sign-in) | **works** |

### What this rules out

The headline result is that **no route is missing, dead or unrendered.** For all ten routes, in both
access levels:

- an entry in `ROUTES` exists;
- a `.page` element exists (six static in `web/index.html`, four injected at runtime);
- the router activates it — `applyRoute()` (`web/app.js:300`) selected the correct page in 20 of 20
  loads, including all four late-injected pages;
- a renderer put content into it;
- **zero uncaught JavaScript errors and zero unhandled rejections** in all 20 loads.

There is no nav entry pointing at a route with no page, no route in `ROUTES` without a `.page`, and
no page whose mount point was renamed out from under its renderer.

### The late-injection path specifically

This was the most likely suspect and it is **sound**. `product-mode.js` owns the single
`#mainContent` observer (`web/product-mode.js:459-467`) and republishes it as `onContentChange`;
`app.js` subscribes `applyRoute` to it (`web/app.js:2356`). Two details make it work where it could
easily not have:

1. `app.js` registers its subscriber during script execution, before `DOMContentLoaded`, so it is
   already in `contentSubscribers` when the observer is installed.
2. The engineering page is injected *before* the observer exists, and `web/product-mode.js:464-466`
   deliberately fires the subscriber list once by hand to cover exactly that.

`notifyContentChange` (`web/product-mode.js:199`) drains records produced *while* subscribers run
via `takeRecords()`. That is correct for loop suppression, and no current subscriber injects a page,
so no injection is currently swallowed. It is worth knowing that a future subscriber that did inject
a page would have that injection discarded.

### Deleted and unserved modules

- **`web/commissioning-route.js` (deleted).** Nothing depends on it. The only live references are
  the class name `commissioning-route-active` in `web/commissioning-wizard-v2.js:406,410`, which is
  a CSS hook of its own and unrelated, a historical comment at `web/app.js:61`, and
  `tests/operator_product_suite_source_contract.py:75,109-115`, which *asserts the file stays
  deleted and unserved*. Clean.
- **`web/engineering-session-resilience.js` is embedded in firmware but never served.** It has a
  getter (`components/web_server/web_assets.c:112`), a declaration
  (`components/web_server/include/web_assets.h:22`) and a `configure_file` entry, but appears in
  **neither** `assets[]` array in `web_server.c`. Pre-existing. **Nothing depends on it:** its only
  outward signal is the `amx-engineering-session-ready` event
  (`web/engineering-session-resilience.js:38`), and a repository-wide search finds no listener. It
  should **not** be added to `assets[]` to "fix" this — it wraps `window.fetch`
  (`web/engineering-session-resilience.js:4,51`) and auto-establishes an engineering session, which
  would collide with the fetch wrapper and request-scope predicate that `product-mode.js` owns. See
  F7.

---

## 2. What the report is most likely about

The complaint is accurate, but the cause is not a broken router. Ranked by how much of the product
an operator cannot see:

### F1 — An operator's sidebar shows four of ten pages, and hides two that are theirs

**This is the finding that best matches "pages are missing", and it needs a decision — not fixed
here.**

A signed-out operator's sidebar contains exactly: Plant overview, Alarms and events, Pre-lab
readiness, Engineering access. The whole **Maintain** group is empty and its heading is hidden
(correctly, by `web/app.js:373-383`).

Four of the six hidden entries are hidden for a good reason: `wifi`, `control`, `system` and
`commissioning` are in `PROTECTED_ROUTES` (`web/product-mode.js:26`), so an operator genuinely
cannot open them.

**`meters` and `inverters` are different.** `web/product-mode.js:306-315` marks six routes
`data-engineering-nav`, and `setEngineering()` (`web/product-mode.js:292`) hides them while signed
out — but that list includes `meters` and `inverters`, which are **not** protected. The consequence,
measured:

- `#/meters` as an operator renders 2273 characters of operator content, activates correctly, and
  carries the Grid power trend chart.
- `#/inverters` as an operator renders 12065 characters and the Solar production trend chart.
- `web/app.js:66-67` places both in the **`operate`** group — *"watching a running plant"*.
- The product mobile bar already offers both to operators (`web/operator-product-suite.js:104`).
- `web/product-mode.css:22-28` already hides the engineering-only *contents* of those pages
  (`#meterConfigurationEditor`, `#inverterList`, the meter dashboard grid and so on) from operators.

So two pages that were built for operators, that render operator content, that the redesign's own
route table files under "watching a running plant", and whose engineering internals are already
hidden by CSS, are reachable by URL and by the mobile bar but have **no sidebar entry**. That is the
definition of orphaned.

**Why this is not fixed here:** removing `'meters'` and `'inverters'` from the list at
`web/product-mode.js:308` would change what an unauthenticated visitor is offered. That is an
access-surface decision belonging to the owner, not a presentation defect. The evidence says the
list and the route table disagree; which one is wrong is the owner's call.

### F2 — Commissioning's sidebar entry was conditional on module start order *(fixed)*

`ensureCommissioningPage()` guarded the page **and** its navigation entry behind one early return on
the page already existing. Commissioning is not the only module that creates
`[data-page="commissioning"]`: `web/commissioning-release-v3.js:20` builds the same section whenever
it starts on that route, and it starts from an access-scope change
(`web/commissioning-release-v3.js`, last line) as well as from load. Whichever ran first decided
whether Commissioning appeared in the sidebar **at all**.

Under specification ordering `operator-product-suite.js` wins the race, so this does not currently
bite a browser. It is still a real defect: the entry's existence depends on an ordering nothing
enforces. Demonstrated by running the same bundle under jsdom's non-conformant `readyState`, which
reverses the order — the Commissioning entry vanished from the sidebar entirely, on both access
levels, while the page itself still rendered. A page reachable only by typing its URL is exactly
what gets reported as missing.

**Fixed** at `web/operator-product-suite.js:293-317`: the page check and the navigation check are
now independent, matching what `web/operator-operations.js:233` and `web/prelab-readiness.js:21`
already do. The entry is now created under both orderings. Placement stays app.js's decision.

### F3 — The mobile bar offered a route it could not open *(fixed)*

`web/operator-product-suite.js:100-108` built a five-entry bottom bar including **PV-DG control**,
unconditionally. `control` is protected, so a signed-out operator who tapped it was answered with
the Engineering sign-in page. It was the only navigation entry in the product that could not reach
the page it named.

**Fixed.** `updateMobileNavigation()` (`web/operator-product-suite.js:126-135`) now withholds an
entry exactly while it is unreachable, reading the decision from `isProtectedRoute` — a new
**read-only** view of the set `enforceRoute()` already enforces (`web/product-mode.js:483`). No
authorisation logic moved or changed; presentation asks rather than duplicating the set.

Three supporting changes were needed for the hide to actually take effect and look right:

- `web/operator-product-suite.css` gives every entry `display: grid`, which outranks the user-agent
  rule behind `hidden`. A `.product-mobile-link[hidden] { display: none }` rule was added in that
  same sheet.
- The bar was `grid-template-columns: repeat(5, 1fr)`, so hiding one entry would have held its cell
  open. Changed to `grid-auto-flow: column; grid-auto-columns: minmax(0, 1fr)` — identical to
  `repeat(N, 1fr)` for N visible children, and correct as N changes.
- `web/product-shell-v2.css:143` re-imposed the five-track list later in the cascade and had to make
  the same change.
- `normalizeNavigation()` now calls `updateMobileNavigation()`, so signing in restores the entry.
  `ensureMobileNavigation()` only builds the bar once, so without this the bar kept whatever it was
  given at load.

`meters` and `inverters` were deliberately **left visible** in the mobile bar: they are openable, and
withdrawing them would be the F1 decision made silently in the restrictive direction.

### F4 — Content that moved behind a disclosure, and reads as absent

Not a defect, but it is the most likely source of *"thing are not as well managed as i expected"*.
Measured as visible text with closed `<details>` collapsed:

| Route | Visible | Behind a closed drawer | Drawer |
|---|---|---|---|
| `alarms` (operator and engineering) | 1026 chars | **971 chars** | "How alarm states work" (762), "Alarm system performance" (209) |
| `dashboard` (operator) | 3339 chars | 834 chars | "Measurement sources for each node" |
| `control` (engineering) | 6086 chars | 102 chars | "Control prerequisites" |

The alarms screen is the one to look at: **just under half of its content is behind two collapsed
drawers**, and the page has exactly one visible heading. That is the intended outcome of *"make the
first alarm screen triage, not a lesson"* (`3345485`), and it is defensible — but if the owner's
"pages are missing" means the alarms screen, this is what changed. Left as-is: reversing it would
undo a deliberate redesign decision.

### F5 — The mobile bar uses a second set of page names

`web/operator-product-suite.js:104` labels entries Overview / Grid / Solar / Alarms / Control;
`web/operator-product-suite.js:62-66` renames five *sidebar* entries to Overview / Grid Power /
Solar / Control / Controller. `web/app.js` then restores the durable names via
`ensureNavigationHierarchy()` (`web/app.js:351-359`) — the sidebar is corrected on every mutation,
so the durable names win there. The **mobile bar is not corrected**, so on a phone the pages are
called Overview and Grid while the same pages are called Plant overview and Grid power everywhere
else. That directly contradicts the one-durable-name premise documented at `web/app.js:17-31`
("An operator who is told over the phone to open *Network setup* must be able to find it in the
sidebar").

Not fixed: five full names in a five-column bottom bar is a layout problem with no obviously correct
answer, and `web/operator-product-suite.js:62-66` renaming entries that `app.js` immediately renames
back is dead code whose removal is a separate cleanup. Both are owner decisions.

### F6 — An engineering user sees the alarm table but not the recent-events list

`renderAlarmPage()` (`web/operator-operations.js:965`) returns early when `!isOperator()`, and its
target `#operatorAlarmView` sits inside `.operator-product-view`, which
`web/product-mode.css:33` hides outright for engineering. So signing in removes the "Recent events"
card from the alarms screen.

This is consistent with the operator/engineering split used everywhere else, and the condition table
with the acknowledge action is deliberately kept *outside* that container
(`web/operator-operations.js:250-253`) so it survives — the design intent is explicit and honoured.
Recorded as an observation. Whether an engineer should also see the event ring is a product
decision, and any change here touches alarm presentation semantics, which this pass excludes.

### F7 — `engineering-session-resilience.js` is dead weight in the firmware image

Embedded, given a getter, and served by nothing. Nothing depends on it (§1). It costs flash and
nothing else. The correct resolution is to **remove** the embed, not to add it to `assets[]` —
serving it would install a second `window.fetch` wrapper alongside the one `product-mode.js` owns
and auto-establish engineering sessions outside the scope predicate. Not done here: deleting an
embedded asset touches `web_assets.c`, `web_assets.h`, `CMakeLists.txt` and the linker symbol list,
which is a build change, not a presentation change.

### F8 — A nav entry created after `DOMContentLoaded` never gets the engineering marker

`addNavigation()` (`web/product-mode.js:306`) runs once and stamps `data-engineering-nav` on the six
entries that exist at that moment. Any protected route whose entry is created later is not stamped
and is never hidden by `setEngineering()`. Commissioning is the only such entry, and under
specification ordering it exists before `addNavigation()` runs, so it is stamped correctly (verified:
`hidden=true` for an operator). Under the reversed ordering used to demonstrate F2 it was not.
Recorded as a latent coupling; not fixed, because the fix is either another observer or moving the
ownership of protected-entry visibility, and neither is warranted for a case that is currently
unreachable.

---

## 3. Ranked list of what to fix

Most user-visible first.

| # | What | Verdict | Status |
|---|---|---|---|
| 1 | **F1** — `meters` and `inverters` have no operator sidebar entry although they are unprotected, render operator content and are grouped under "Operate" | orphaned | **Needs a decision.** One-line change at `web/product-mode.js:308`; it alters the signed-out access surface, so it is the owner's call |
| 2 | **F2** — Commissioning's sidebar entry depended on module start order | broken (latent) | **Fixed** |
| 3 | **F3** — mobile bar offered PV-DG control to operators; the tap reached the sign-in page | broken | **Fixed** |
| 4 | **F4** — roughly half the alarms screen sits behind two closed drawers | moved-behind-disclosure | **Left as-is** — deliberate; flagged so the owner can confirm it is what they wanted |
| 5 | **F5** — mobile bar uses a second set of page names, contradicting the durable-name rule | broken (consistency) | **Needs a decision** — five full names do not fit five columns |
| 6 | **F6** — engineering loses the "Recent events" card on the alarms screen | works as designed | **Needs a decision**; touches alarm presentation, excluded from this pass |
| 7 | **F7** — `engineering-session-resilience.js` embedded but never served | dead weight | **Needs a decision** — remove the embed; do **not** serve it |
| 8 | **F8** — nav entries created after `DOMContentLoaded` are never marked engineering-only | latent | **Left as-is** — currently unreachable |

---

## 4. Changes made

Presentation only. No control, safety, authorisation, alarm-semantics or acquisition behaviour was
altered. No DOM post-processing layer was added — both fixes are in the module that already owns the
thing being fixed.

| File | Change |
|---|---|
| `web/operator-product-suite.js` | `ensureCommissioningPage()`: page and navigation checks separated (F2). `updateMobileNavigation()`: withholds an entry while its route is unreachable (F3). `normalizeNavigation()` re-runs it so sign-in restores the entry |
| `web/product-mode.js` | Exports `isProtectedRoute`, a read-only view of the existing `PROTECTED_ROUTES` set. Decides nothing; lets a navigation surface avoid offering an unreachable route instead of duplicating the set |
| `web/operator-product-suite.css` | `.product-mobile-link[hidden] { display: none }`; bar sized from the entries actually offered rather than a hardcoded five |
| `web/product-shell-v2.css` | Stops re-imposing the five-track list later in the cascade |

### Verification

Run before the changes to establish a baseline, and again after. Identical both times:

- `node --check` on all 32 `web/*.js` — clean.
- All 4 `web/tests/*.test.js` — pass.
- All **71** CI contracts from `.github/workflows/esp-idf-build.yml` — pass. No assertion was
  weakened or skipped.
- Bundle harness, all 10 routes × 2 access levels, before and after: every route still activates its
  own page, every route still has a navigation entry, zero JavaScript errors.
- F2 specifically re-checked under **both** module orderings; the Commissioning entry is now present
  in both, where previously it was absent in one.
- F3 specifically: signed out, the bar offers `dashboard, meters, inverters, alarms` and withholds
  `control`; signed in, all five are offered.

Not verified: anything on real hardware, and any behaviour that requires navigating between routes,
signing in mid-session, or sustained polling.
