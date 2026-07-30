# Review — PR #16 "UI: industrial operator presentation pass 1"

- **Branch:** `ui/industrial-operator-pass-1` @ `30bd205` → `phase1-fix` @ `f40921b`
- **Reviewed:** 2026-07-30
- **Scope:** 8 files, +620 / −3. Two new web assets, their build/serve wiring, one source-contract
  test, one new CI workflow.
- **Verdict:** **Do not merge as the integration vehicle.** Cherry-pick at most two ideas.
  The bulk of it is a fifteenth cascade layer bolted onto the pile the base branch is
  currently dismantling, and it regresses UI defects that `phase1-fix` has already fixed
  properly.

---

## 0. Summary of findings

| # | Finding | Severity |
|---|---|---|
| F1 | ~~Base-branch regression: `/api/operator/*` routed through the default-deny gateway.~~ **WITHDRAWN — this finding is incorrect. See section 6.** | **Not a defect** |
| F2 | PR #16's client-side hiding is *not* the only protection. Server-side enforcement is real and default-deny. No client-only-authorization finding. | Pass |
| F3 | PR #16 CI fails for an inherited base defect (`offsetof` without `<stddef.h>`), already fixed on `phase1-fix`. Branch is stale, not broken. | Low (mechanical) |
| F4 | PR #16 reintroduces hardcoded hex colours and a sub-44 px tap target, directly regressing `phase1-fix` commit `04b89af` which fixed audit S5/S5a/S6. | **High (quality)** |
| F5 | PR #16 fixes none of S3, S4, S5, S5a, S6. S1/S2/S4/S5/S5a/S6 are already fixed on `phase1-fix` without it. | High |
| F6 | Trial merge is textually clean (zero conflicts), but semantically conflicts with the base's layer-consolidation direction. | Medium |

---

## 1. What PR #16 actually changes — commit by commit

Classification key: **(a)** genuine architectural correction · **(b)** safety boundary worth
preserving · **(c)** another patch layer.

| # | Commit | What it does | Class |
|---|---|---|---|
| 1 | `3b4aa54` feat(ui): add safe industrial operator presentation layer | Adds `web/industrial-operator-ui.js` (306 lines). An IIFE that runs after every other module, re-queries the finished DOM, and rewrites it. | **(c)** |
| 2 | `9b5f2e4` style(ui): add industrial operator and service presentation | Adds `web/industrial-operator-ui.css` (223 lines) — a new terminal cascade layer whose job is to out-specify the fourteen sheets before it. | **(c)** |
| 3 | `f31fcda` test(ui): add industrial operator presentation contract | `tests/industrial_operator_ui_source_contract.py`. **Confirmed: this file does not exist on `phase1-fix`; PR #16 adds it.** | **(b)**, partially — see §2.3 |
| 4 | `06dcb20` build(web): embed industrial operator UI assets | `CMakeLists.txt:16,49,120,153` — `configure_file` + `EMBED_FILES`. | mechanical |
| 5 | `3624305` build(web): declare industrial operator assets | `web_assets.h:19,52` — two getter declarations. | mechanical |
| 6 | `0675534` build(web): expose industrial operator assets | `web_assets.c:11,28,50,83,111,144` — `DECLARE_ASSET` / `ASSET_GETTER`. | mechanical |
| 7 | `1e800dc` feat(web): load industrial operator UI last | `web_server.c:87,126` — appends the two assets to the ends of `css_handler`/`js_handler`. This is the load-order dependency made explicit in C. | **(c)** — see below |
| 8 | `9d0ba96` fix(ui): reset advanced service view on engineering lock | `industrial-operator-ui.js:247` `if (!engineering) clearServiceView();` — drops the sessionStorage disclosure flag when the session locks. | **(b)** |
| 9 | `4a89dde` ci(ui): validate industrial presentation layer | `.github/workflows/industrial-ui-checks.yml` — `node --check` + run the contract. Passing (run 30554028282). | **(b)** |
| 10 | `30bd205` fix(ui): make presentation reconciliation idempotent | Adds the `reconcilePending` / `queueMicrotask` coalescer (`:11,275-279`) and per-element guards. | **(c)** — a fix for a problem the approach created |

**Commits 4–7 deserve a closer look than "mechanical".** They are the honest part of the PR: they
encode, in firmware C, that this module *must* be concatenated last. `web_server.c:126` places
`web_assets_industrial_operator_ui_js` after `commissioning_release_v3_js`, and `:87` does the same
for the CSS. That is a load-order contract enforced by the server binary. It works — but the reason
it is needed is that the module has no API of its own; it can only function by observing and
overwriting whatever the fourteen modules ahead of it happened to render. That is the definition of
a patch layer, and the PR makes the dependency permanent by baking it into the firmware.

**Commit 10 is the tell.** `reconcilePresentation()` mutates `#mainContent`, and
`industrial-operator-ui.js:297` installs `new MutationObserver(scheduleReconcile).observe(main,
{ childList: true, subtree: true })` on that same subtree. The module observes the DOM it is
itself editing. Commit 10 exists to stop that loop from running away, via microtask coalescing plus
a set of hand-written idempotency guards. It appears to converge — `applyAudienceVisibility()`
only touches attributes (`:252`, `:255`) and attributes are not observed — but the convergence is
incidental, not structural. Any future contributor who adds a `childList` mutation inside
`reconcilePresentation()` reopens the loop, and nothing in the contract test would catch it.

### The fragility that concerns me most

`organiseMeterConfiguration()` (`industrial-operator-ui.js:151-190`) does not style the meter form —
it **physically relocates DOM nodes**:

```js
:179   if (advancedNames.has(name)) advancedGrid.append(label);
```

Five `<label>` elements (`Port`, `Modbus PDU address`, `Scale to kW`, `Poll interval (ms)`,
`Timeout (ms)`) are moved out of the owning module's `.field-grid` into a `<details>` this module
built. It also renames two field labels in place (`:177-178`, `Host` → `Meter IP address`,
`Unit ID` → `Modbus device address`).

The re-entry guard is `if (!fieldGrid || panel.querySelector('.industrial-advanced-details')) return;`
(`:165`). If the meters module ever re-renders its `.field-grid` — replacing `innerHTML`, as these
modules routinely do — the five service fields return to the visible grid, the `<details>` wrapper
is still present, so the guard **returns early and never re-hides them**. The advanced service
fields are then exposed to the operator with no re-tidy. This is a presentational failure, not a
security one (§2 covers why), but it is exactly the class of bug that DOM-scraping layers produce
and that the source contract cannot see.

Equally, `markInternalDashboardCards()` (`:135-144`) identifies the "Requested PV" / "Applied PV"
cards to hide **by matching their English heading text**. Rename a card, or localise the UI, and
internal control detail silently reappears on the operator dashboard.

---

## 2. Safety boundaries — are they real?

### 2.1 What the PR hides, and from whom

`applyAudienceVisibility()` (`:244-260`) drives two tiers off
`document.documentElement.dataset.access`:

- `.industrial-engineering-panel` → `panel.hidden = !engineering` (`:252`) — meter config form
  (`:156`), `#writeConfirmationPanel` (`:206`)
- `.industrial-service-panel` → `panel.hidden = !service` (`:255`) — `#labTargetPanel` (`:208`),
  the system page advanced-JSON article (`:217`), `#em500Workspace` (`:219`)

Plus CSS-only hiding of the "Requested PV" / "Applied PV" dashboard cards
(`industrial-operator-ui.css:49-51`).

`data-access` is a client-side attribute, set by `web/product-mode.js:4` and mutated by the browser.
Anyone with devtools can set `document.documentElement.dataset.access = 'engineering'`, or simply
run `document.querySelectorAll('[hidden]').forEach(e => e.hidden = false)`, and every panel returns.
**The `hidden` property confers no authorization whatsoever.**

### 2.2 The server does refuse — and by default

I checked this specifically, because the brief is right that it is the thing that matters. The
answer is good, and it is good *architecturally* rather than by per-endpoint diligence.

`components/web_server/include/engineering_auth.h:33-35`:

```c
#ifndef ENGINEERING_GUARD_IMPLEMENTATION
#define httpd_register_uri_handler engineering_register_uri_handler
#endif
```

Every web-server translation unit that includes this header has its route registrations transparently
redirected through the gateway. `engineering_guard.c:282-301` then wraps each handler, and
`engineering_guard.c:274-280` is the **entire** public allowlist:

```c
static bool public_uri(const char *uri)
{
    return strcmp(uri, "/") == 0 || strcmp(uri, "/favicon.ico") == 0 ||
           strcmp(uri, "/app.css") == 0 || strcmp(uri, "/app.js") == 0 ||
           strcmp(uri, "/api/status") == 0 || strcmp(uri, "/api/telemetry") == 0 ||
           strncmp(uri, "/api/engineering/", 17) == 0;
}
```

Everything not on that list defaults to `GATEWAY_MODE_PROTECTED`, and
`engineering_auth_guarded_handler` (`engineering_guard.c:260-272`) falls through to
`engineering_auth_require()` → `401 Unauthorized` (`engineering_auth.c:484`).
`engineering_auth_is_authorized()` (`engineering_auth.c:461-467`) is
`configured && session_cookie_valid(request, true)` — it fails closed both when no password is set
and when no valid session cookie is presented.

Mapping this onto every capability PR #16 hides:

| Hidden UI | Backing endpoint(s) | Allowlisted? | Unauthenticated result |
|---|---|---|---|
| Meter configuration form | `POST /api/meters/config` | no | 401 |
| `#writeConfirmationPanel` | `/api/inverter-config`, `/api/inverter-profiles` | no | 401 |
| `#labTargetPanel` | inverter command paths | no | 401 |
| System advanced JSON | `/api/config` (POST) | no | 401 |
| `#em500Workspace` | `/api/meters/em500/*` | no | 401 |
| Requested/Applied PV cards | `/api/status`, `/api/telemetry` | **yes** | 200 |

The last row is fine: those are read-only telemetry values, already public by design, and hiding them
is a presentation decision about operator noise, not an access-control decision.

**Conclusion: there is no case in PR #16 where client-side hiding is the only protection.** The
boundary the PR draws is a *presentation* boundary layered on top of a working authorization
boundary it did not create and does not weaken. That is the correct relationship, and commit
`9d0ba96` (clearing the disclosure flag on lock) and the CI workflow `4a89dde` are worth preserving
in whatever replaces this PR.

### 2.3 The contract test is weaker than it reads

`tests/industrial_operator_ui_source_contract.py:14-16` is the genuinely valuable assertion:

```python
assert '/api/' not in js
assert 'fetch(' not in js
assert 'XMLHttpRequest' not in js
```

That is a real, cheap, durable invariant: this layer can never grow a command path or add request
traffic. Keep this idea.

The remainder (`:19-46`) asserts that six English UI strings and four CSS selectors are present, and
that six symbol names appear in the build files. Those are change-detectors, not contracts — they
will fail on any legitimate copy edit and will pass on any behavioural regression. They should not
be carried forward as written.

---

## 3. Why CI is failing — root cause

**Root cause: the PR branch is based on a stale `phase1-fix` and is inheriting a base-branch
compile failure. Nothing in PR #16's own diff is at fault.**

`gh run view 30554028201 --log-failed`, job `web`, step "Run commissioning gate unit tests":

```
tests/commissioning_gate_test.c:114:10: error: implicit declaration of function 'offsetof' [-Werror=implicit-function-declaration]
tests/commissioning_gate_test.c:114:19: error: expected expression before 'commissioning_inputs_t'
...
tests/commissioning_gate_test.c:120:19: error: expected expression before 'commissioning_inputs_t'
cc1: all warnings being treated as errors
##[error]Process completed with exit code 1.
```

`tests/commissioning_gate_test.c` is not in PR #16's diff (`git diff --stat origin/phase1-fix...
origin/ui/industrial-operator-pass-1` lists 8 files; that is not one of them). The base branch fixed
it independently — `git show origin/phase1-fix:tests/commissioning_gate_test.c` line 9:

```c
#include <stddef.h>  /* offsetof: pulled in transitively by some libcs, not by glibc */
```

Timeline from `gh run list --branch phase1-fix`:

```
30555928318  failure  fix(tests): include stddef.h for offsetof
30556362555  failure  fix(journal): distinguish a short read from a read error
30556674268  success  fix(tests): expose POSIX fileno/fsync to the journal harness
```

`phase1-fix` is now green. A rebase of PR #16 onto `f40921b` would clear this failure without a
single change to the PR's own content.

PR #16's *own* workflow (`Industrial UI checks`, run 30554028282) passes in 9 s.

---

## 4. What PR #16 does not fix

Checked against `docs/UI_VISUAL_AUDIT_2026-07-29.md`. PR #16 touches no chart code, no module
initialisation, no request scheduling, and no theme tokens, so the answer is short: **it fixes none
of the audit's open defects.** The more useful finding is that `phase1-fix` has already fixed most of
them — properly — while PR #16 was in flight.

### S1 — socket exhaustion: **fixed on `phase1-fix`, verified**

`git show origin/phase1-fix:components/web_server/web_server.c`:

```
:152   /* httpd enforces max_open_sockets + 3 <= CONFIG_LWIP_MAX_SOCKETS and ... */
:158   config.max_open_sockets = 10;
:162   config.lru_purge_enable = true;
```

Both the socket count and LRU purge are addressed, with the `CONFIG_LWIP_MAX_SOCKETS` arithmetic
documented in place. The audit's claim is satisfied.

### S2 — PSRAM disabled: **fixed on `phase1-fix`, verified**

`git show origin/phase1-fix:sdkconfig.defaults:19-24`:

```
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y
```

Octal mode and 80 MHz are set as the audit required. Note the audit's own caveat stands: this
**must be confirmed on hardware**, not accepted from the config file. That verification is not
something I can do from source.

### S3 — `/api/operator/history` 500s under browser load: **not fixed by PR #16; indirectly addressed**

PR #16 does not touch `operational_api.c`. The audit attributes the 500 to heap exhaustion during
assembly of the firmware's largest response (26,498 bytes) under socket pressure — i.e. downstream
of S1 and S2, both now fixed. No commit on `phase1-fix` restructures the history response itself
(`git log origin/phase1-fix -- components/web_server/operational_api.c` shows only alarm-lifecycle
work). **S3 should be re-tested on hardware and not assumed closed.**

### S4 — operator view fires engineering-only endpoints: **not fixed by PR #16; fixed on `phase1-fix`**

PR #16 changes no module initialisation. `phase1-fix` solved this with a shared request-scope table
plus a contract — `tests/operator_endpoint_scope_source_contract.py:66-73` pins exactly the four
endpoints the audit measured:

```python
for endpoint in [
    "'/api/inverter-profiles'",
    "'/api/wifi/scan'",
    "'/api/solar-grid/config'",
    "'/api/meters/em500/'",
]:
```

and `:96-107` requires each of the eight calling modules to route through
`AutomatrixEngineeringAccess` rather than its own auth check. This is the shape the fix should
take, and PR #16 contributes nothing to it.

### S5 / S5a — hardcoded colours bypass theme tokens: **not fixed; PR #16 actively regresses it**

`phase1-fix` commit `04b89af` — *"fix(ui): route surface colours through theme tokens and enforce
44x44 touch targets"* — closes S5/S5a and S6, and does so more accurately than the audit did. Its
message corrects the audit's own root-cause attribution:

> The Pre-Lab Readiness failure had a different root cause than the audit recorded.
> `product-shell-v2.css:66/120` are `[data-theme="light"]`-scoped and never apply in dark. The real
> culprit is `prelab-readiness.css`, which is written against `--panel-bg` / `--border-color` —
> token names that were never defined anywhere in the project.

The fix adds semantic tokens (`--surface-sunken`, `--surface-raised`, `--action-solid`, …) for both
themes and aliases the undefined legacy names onto them.

Against that, `web/industrial-operator-ui.css` introduces **new** hardcoded literals:

```
:190   background: #f28a2b;     /* .brand-mark::before */
:195   background: #2c6ea4;     /* .brand-mark::after  */
:7     --industrial-shadow: 0 8px 22px rgba(4, 15, 28, 0.16);
:150   ... var(--warning, #d99a2b) ...   /* literal fallback — the exact S5a pattern */
```

`:150` and `:157-161` use `var(--warning, #d99a2b)`. That literal-fallback form is precisely what
`04b89af` identified as the mechanism behind the 1.10:1 readiness page — a fallback that fires
silently in both themes when the token is undefined. Re-introducing the pattern days after it was
eliminated is a regression, not a neutral addition.

The sheet also leans on `!important` to win the cascade — `:22` (`border-radius`), `:50`, `:54`,
`:130` (`display: none`), `:150` (`border-color`), `:174-176` (`background`/`color`/`font-size` on
`.brand-mark`). Seven `!important` declarations in 223 lines is the signature of a sheet fighting
the ones above it.

### S6 — tap targets under 44×44: **not fixed; PR #16 adds a new violation**

`04b89af` raised the product-shell header controls to 44 px and gave `.op-range-button` a 44 px
min-height via padding. PR #16 touches none of `#menuButton`, `#refreshButton`, `#shellHealthButton`,
`#shellOverflowButton` or `.op-range-button`.

It then adds a **new** interactive control below the threshold:

```
industrial-operator-ui.css:109      min-height: 40px;    /* .industrial-evidence-toggle */
```

40 px, on a control the operator is expected to tap on every inverter page. (`:76`, the
`<details>` summary, is correctly 48 px — so the 40 px appears to be an oversight rather than a
policy.)

### Beyond the audit — the product owner's list

Checked directly, since these are the stated reasons not to merge blindly:

- **Page architecture** — unchanged. No route, no navigation, no page composition is touched.
- **Excessive text / repeated information** — *increased*. The PR adds an `OPERATOR VIEW` /
  `ENGINEERING VIEW` badge to the topbar (`:59-70`), two new dashboard cards (`:88-108`), an
  explanatory paragraph on the meters page (`:168-169`), a `Same Engineering session; no additional
  authority is granted.` caption (`:229`), and an `ADVANCED SERVICE TOOL` pseudo-element ribbon on
  every service panel (`css:155`).
- **Wasted screen area** — the two new dashboard cards (`#industrialControlSummary`,
  `#industrialAlarmSummary`) are populated by scraping `#dashboardMode`, `#statusControl` and
  `#statusAlarms` (`:114-117`) — values already rendered elsewhere on the same page. This is
  duplicated information consuming a four-column grid row (`css:57-59`), which is the opposite of
  the requested correction.
- **Competing chart implementations** — untouched. Both remain: `web/operator-view.js` and
  `web/operator-operations.js`.
- **Weak chart interaction** — untouched. No chart interaction code in the PR.
- **Visual hierarchy** — adjusted cosmetically (`border-radius`, `tabular-nums`, `min-height` on
  cards) but not restructured.

The product owner's characterisation is accurate on every point.

---

## 5. Conflicts with current `phase1-fix`

Trial merge performed in this worktree and **aborted**; nothing committed:

```
$ git checkout -B pr16-trial origin/ui/industrial-operator-pass-1
$ git merge --no-commit --no-ff origin/phase1-fix
Auto-merging components/web_server/CMakeLists.txt
Automatic merge went well; stopped before committing as requested
$ git merge --abort
```

**Zero textual conflicts.** The merge is clean because PR #16 is almost purely additive: two new
files plus append-only edits to four build/serve files. The one auto-merged file,
`components/web_server/CMakeLists.txt`, resolves correctly, and the load-order intent survives —
in the merged tree, `web_server.c:126` still has `web_assets_industrial_operator_ui_js` last, and
`:87` still has the CSS last (fifteenth of fifteen sheets).

**On the alarm-ack question specifically: PR #16 does not touch `web/operator-operations.js`.** The
base changed it (+37/−…), the PR does not, so there is no interaction. Confirmed against the PR's
own file list.

The conflicts are therefore semantic, not textual:

| File / area | PR #16 side | `phase1-fix` side | Which should win |
|---|---|---|---|
| `web/industrial-operator-ui.css` (new) | Adds a 15th cascade layer with 7 `!important`s and hardcoded hex | `1edfd5b` *"delete superseded polish sheet, drop unreachable rules, document the cascade"* deletes `web/release-polish-v1.css` (−200 lines) | **`phase1-fix`.** The base is removing layers; this adds one. Diametrically opposed direction. |
| Theme colours | `:190` `#f28a2b`, `:195` `#2c6ea4`, `:150` `var(--warning, #d99a2b)` | `04b89af` semantic tokens for both themes, literal fallbacks eliminated | **`phase1-fix`.** The PR reintroduces the exact defect class. |
| Tap targets | `:109` new 40 px control | `04b89af` enforces 44×44 | **`phase1-fix`.** |
| `web_server.c` asset lists | Appends 2 assets | Unchanged in this region | Merges cleanly; keep only if the assets survive review. |
| `.github/workflows/industrial-ui-checks.yml` | New workflow | n/a | **PR #16.** Genuinely additive and passing. |

This is the dangerous case: a merge that git reports as clean but that quietly re-opens two audit
findings the base branch closed three commits ago. A green merge button here would be misleading.

---

## 6. Finding F1 — WITHDRAWN. The reasoning was sound; the conclusion was wrong.

This review argued that `ca47f3e` added `#include "engineering_auth.h"` to `operational_api.c`,
that the header `#define`s `httpd_register_uri_handler` to the default-deny gateway, that no
`/api/operator/*` route appears in `public_uri()`, and that the operator-accessible alarm
acknowledgement of `2c3e8a7` is therefore inert.

Every step of that is individually true. The conclusion is still false, because of a build-system
fact none of the source reading could reveal.

**`components/web_server/CMakeLists.txt:215` compiles `operational_api.c` with
`ENGINEERING_GUARD_IMPLEMENTATION=1`** — the same exemption `engineering_guard.c` itself receives:

```cmake
set_source_files_properties("engineering_guard.c" "operational_api.c" PROPERTIES
    COMPILE_DEFINITIONS ENGINEERING_GUARD_IMPLEMENTATION=1)
```

The macro in `engineering_auth.h` is wrapped in `#ifndef ENGINEERING_GUARD_IMPLEMENTATION`, so in
this translation unit `httpd_register_uri_handler` is ESP-IDF's real function and the operator
routes are registered ungated — deliberately, matching the comment at `operational_api.c:1567`
that this unit "is deliberately outside the authorization gateway so operator history and events
stay readable without a session".

**Verified on hardware**, unauthenticated, against the live controller:

| Request | Result |
|---|---|
| `GET /api/operator/history?range=15m` | **200** |
| `GET /api/operator/alarms` | **200** |
| `GET /api/operator/events` | **200** |
| `POST /api/operator/alarms/ack` | **200**, `"acknowledged_by":"operator"` |
| `POST /api/operator/alarms/shelve` | **401** |

The 401 on shelve is the handler's OWN authorization check, not the gateway — which is exactly the
asymmetry `2c3e8a7` intended: acknowledgement open, suppression closed. Had the gateway been
wrapping this unit, ack and shelve would necessarily have behaved identically, since they are
registered from the same `handlers[]` array. They do not.

**The lesson worth keeping:** a source-level reading of macro-based interposition cannot be trusted
without checking per-file compile definitions. The gateway's opt-out is invisible from C source
alone. Anyone auditing this boundary in future must read `CMakeLists.txt:210-216` as part of it.


## 7. Recommendation

**Do not use PR #16 as the integration vehicle. Close it, cherry-pick three small things, and
rebuild the operator presentation as part of the redesign.**

The evidence supports the product owner's suspicion, and more strongly than expected. The
determining facts:

1. **It solves a problem that is already solved, and worse.** S5, S5a and S6 were closed on
   `phase1-fix` by `04b89af` using semantic theme tokens for both themes. PR #16 reintroduces
   hardcoded hex (`css:190`, `:195`), the literal-fallback pattern (`:150`), and a 40 px tap target
   (`:109`). Merging it is a net regression against the audit.

2. **It moves against the base branch's direction.** `1edfd5b` deleted a 200-line superseded polish
   sheet and documented the cascade. PR #16 adds the fifteenth sheet, wins with seven `!important`s,
   and pins its position in firmware C (`web_server.c:87,126`). The brief's instruction — *"do not
   complete this task by adding another CSS or JavaScript patch layer"* — describes this PR exactly.

3. **Its method cannot survive a redesign.** The JS layer works by scraping the rendered DOM:
   matching English headings (`:139`), matching field label text (`:174-179`), relocating nodes
   another module owns (`:179`), and re-running on a MutationObserver watching the subtree it edits
   (`:297`). Every one of those couplings breaks the moment the page architecture is corrected —
   which is the actual task. Rebasing it onto a redesign would mean rewriting it.

4. **It does not address the substance.** Page architecture, excessive text, repeated information,
   wasted screen area, competing chart implementations, chart interaction, visual hierarchy: none
   are touched, and duplicated information and screen consumption are measurably *increased*
   (§4, "Beyond the audit").

5. **Its CI failure is the least of its problems** — a stale base, cleared by a rebase (§3). That
   should not be mistaken for the PR being nearly ready.

### Cherry-pick these three

1. **The no-API-surface invariant.** `tests/industrial_operator_ui_source_contract.py:14-16` —
   `assert '/api/' not in js` / `'fetch(' not in js` / `'XMLHttpRequest' not in js`. Any future
   presentation module should carry this. Drop the string- and selector-matching assertions at
   `:19-46`; they are change-detectors.
2. **The lock-clears-disclosure rule.** `9d0ba96` / `industrial-operator-ui.js:247` —
   `if (!engineering) clearServiceView();`. A progressive-disclosure preference must not outlive the
   session that justified it. Small, correct, and worth keeping as a principle in the redesign.
3. **The CI workflow shape.** `.github/workflows/industrial-ui-checks.yml` — path-filtered,
   5-minute, `node --check` plus a contract run. Cheap and passing; reuse it with the redesign's own
   contract.

### What the redesign should keep from this PR's thinking

The two-tier model — operator sees plant state; engineering sees configuration; a third "advanced
service" tier behind explicit in-session disclosure — is a sound information architecture, and the
PR's insistence that the tier is presentation-only and grants no authority (`:229`) is exactly the
right posture. Build that into the page structure and the request scope from the start, the way
`tests/operator_endpoint_scope_source_contract.py` does for S4, rather than reconstructing it from
the outside with a MutationObserver.

### Sequence

1. Fix F1 on `phase1-fix` first (§6) and confirm on hardware — the operator view may currently be
   unusable without an engineering session, which would invalidate any UI testing done against it.
2. Verify S2 (PSRAM) and re-test S3 on hardware; neither can be closed from source.
3. Close PR #16 with the three cherry-picks above carried onto a fresh branch off `f40921b`.
4. Do the redesign against the corrected base, with the operator/engineering split expressed in the
   page architecture rather than applied to it afterwards.
