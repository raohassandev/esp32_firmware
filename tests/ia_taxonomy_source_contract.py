"""Information architecture, state vocabulary and diagnostic trends.

The four P1 items an external industrial-UI audit raised against this product
(docs/UI_VISUAL_AUDIT_2026-07-29.md, scored 63/100):

  1. Navigation had no hierarchy and "Engineering" behaved as a role, a mode, a
     page and a menu group at once, while Commissioning, Wi-Fi and Controller
     sat in the ordinary sidebar despite needing the same authority. A user
     could not tell what Engineering was. Routes are now grouped by what a page
     is FOR - Operate / Commission / Maintain / Access - and an unlocked
     engineering session is announced by a persistent banner that counts its
     remaining time down. The 30-minute window is real (AUTH_SESSION_TIMEOUT_MS)
     and losing it mid-write with no warning is a genuine failure.

  2. Sixteen words were in use for six concepts, with "Review" substituted
     almost everywhere. Six closed families now cover them. Where the firmware
     already publishes a state - control_authority.mode_label, its
     inhibit_reason, the alarm condition names, grid_measurement.quality - the
     interface shows that string verbatim. It must not paraphrase a safety
     decision it did not make.

  3. The trend visuals had no axes, units, range or scale, and drew a straight
     line across missing samples. Interpolating across a gap on a control system
     is drawing a measurement that was never taken.

  4. Type was sized in viewport units, so the application looked like a
     different design at different browser zoom levels, and one maximum width
     was applied to dashboards and to single-column forms alike.

Naming is checked as a whole rather than page by page: the audit's specific
complaints were Dashboard vs Plant overview, System vs Controller, Wi-Fi vs
Network setup and Alarm center vs Alarms and events.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APP = (ROOT / "web/app.js").read_text(encoding="utf-8")
APP_CSS = (ROOT / "web/app.css").read_text(encoding="utf-8")
THEME_CSS = (ROOT / "web/theme.css").read_text(encoding="utf-8")
INDEX = (ROOT / "web/index.html").read_text(encoding="utf-8")
MODE = (ROOT / "web/product-mode.js").read_text(encoding="utf-8")
OPERATOR = (ROOT / "web/operator-view.js").read_text(encoding="utf-8")
OPERATIONS = (ROOT / "web/operator-operations.js").read_text(encoding="utf-8")
OPERATIONS_CSS = (ROOT / "web/operator-operations.css").read_text(encoding="utf-8")
MODE_CSS = (ROOT / "web/product-mode.css").read_text(encoding="utf-8")
CHART = (ROOT / "web/pvdg-chart.js").read_text(encoding="utf-8")
DENSITY_CSS = (ROOT / "web/product-experience-v2.css").read_text(encoding="utf-8")
AUTH = (ROOT / "components/web_server/engineering_auth.c").read_text(encoding="utf-8")
WEB_API = (ROOT / "components/web_server/web_api.c").read_text(encoding="utf-8")
OPERATIONAL_API = (ROOT / "components/web_server/operational_api.c").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/esp-idf-build.yml").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


# ===================================================== 1. navigation hierarchy

require("const NAV_GROUPS" in APP, "there is no navigation grouping table")
for group in ("'operate'", "'commission'", "'maintain'", "'access'"):
    require(group in APP, f"navigation group missing: {group}")
for label in ("'Operate'", "'Commission'", "'Maintain'", "'Access'"):
    require(label in APP, f"navigation group is unlabelled: {label}")

# Every reachable route is placed in exactly one group. A route missing from the
# table previously fell back to 'dashboard' and stamped that into document.title
# while rendering something else.
for route in ("dashboard", "meters", "inverters", "alarms", "commissioning",
              "readiness", "wifi", "control", "system", "engineering"):
    require(f"{route}: {{ name:" in APP, f"route {route} has no entry in the route table")

require("function ensureNavigationHierarchy" in APP,
        "nothing owns where a navigation entry lands")
require("data-nav-group" in APP and "data-nav-group" in INDEX,
        "group headings are not marked up")
require(".nav-list .nav-group-title" in APP_CSS,
        "group headings have no styling")

# Other modules add, rename and reorder sidebar entries after this one runs, so
# the grouping has to be re-applied rather than assumed.
require("new MutationObserver" in APP and "function watchNavigation" in APP,
        "the grouping is not re-applied when another module changes the sidebar")
require("navPassRunning" in APP,
        "the navigation pass must be re-entrancy guarded, or it loops on its own writes")

# Grouping is presentation. Authorisation is unchanged and still belongs to the
# shared scope mechanism.
require("PROTECTED_ROUTES.has(currentRoute())" in MODE,
        "route protection must remain in the shared access layer")
require("state.authenticated))" not in MODE,
        "stale auth state must not redirect unrelated operator requests")
require("mayUseEngineering" in MODE and "onScopeChange" in MODE,
        "the shared scope predicate must survive the navigation change")


# ------------------------------------------------- one durable name per page

for route, name in [("dashboard", "Plant overview"), ("system", "Controller"),
                    ("wifi", "Network setup"), ("alarms", "Alarms and events"),
                    ("meters", "Grid power"), ("inverters", "Solar inverters"),
                    ("control", "PV-DG control"), ("readiness", "Pre-lab readiness"),
                    ("engineering", "Engineering access")]:
    require(f"{route}: {{ name: '{name}'" in APP,
            f"{route} does not have a single durable name ({name})")

# ------------------------------- narrow screens get a short FORM, not a second name
#
# The bottom bar on a phone kept its own five-entry label list, so the same
# pages were called Overview and Grid there and Plant overview and Grid power in
# the sidebar, the title and the breadcrumb -- the drift `name` exists to
# prevent, reintroduced on the surface an operator uses in the field.
#
# A shorter label is genuinely needed at 360px, so it is a field ON the route
# record. The property that makes it a rendering rather than a rival name is
# that it is built only from words already in `name`: "Grid power" may shorten
# to "Grid", never to "Meters". That is what is asserted, over the route table
# parsed out of the source with comments stripped -- an assertion that could be
# satisfied by prose in a comment would be no assertion at all.
APP_CODE = re.sub(r"(?m)^\s*//.*$", "", re.sub(r"/\*.*?\*/", "", APP, flags=re.S))
route_table = APP_CODE[APP_CODE.index("const ROUTES = {"):]
route_table = route_table[:route_table.index("\n    };")]
route_records = re.findall(r"(\w+): \{([^}]*)\}", route_table)
# A FLOOR, not a literal. The number of routes is supposed to grow -- adding a
# page is ordinary work -- and pinning the exact count makes this contract fail
# on every new route for no reason connected to what it checks. What the count
# is actually standing in for is "the regex still matches the table"; a parse
# that has silently stopped working returns zero or one record, not eleven. So
# the floor catches the failure this was defending against and stays quiet for
# the change it was never about.
require(len(route_records) >= 10,
        f"the route table parsed as only {len(route_records)} records, so the "
        f"extraction has probably stopped matching and the checks below are vacuous")

SUPPRESSED = 0
for route, body in route_records:
    name = re.search(r"name: '([^']*)'", body)
    require(name is not None, f"route {route} has no name")
    short = re.search(r"short: '([^']*)'", body)
    if short is None:
        continue
    SUPPRESSED += 1
    words = [word.lower() for word in re.findall(r"[\w-]+", name.group(1))]
    for word in re.findall(r"[\w-]+", short.group(1)):
        require(word.lower() in words,
                f"route {route} shortens {name.group(1)!r} to {short.group(1)!r}, which "
                f"introduces the word {word!r}. A short form must be built from the "
                "durable name it abbreviates, or it is a second name for the page")
    require(len(short.group(1)) <= len(name.group(1)),
            f"route {route} has a 'short' form longer than its name; it serves no purpose")
require(SUPPRESSED >= 4,
        "the narrow-screen labels are not coming from the route table")

require("function routeShortName" in APP,
        "nothing resolves the short form, so a navigation surface has to read the "
        "field itself or invent one")
require("routeShortName" in APP[APP.index("window.AutomatrixUi"):],
        "the short form is not published, so a narrow navigation surface cannot use it")

# The mobile bar must read both label and icon from the table rather than
# carrying its own list. Comments are stripped for the same reason as above.
SUITE = (ROOT / "web/operator-product-suite.js").read_text(encoding="utf-8")
SUITE_CODE = re.sub(r"(?m)^\s*//.*$", "", re.sub(r"/\*.*?\*/", "", SUITE, flags=re.S))
require("routeShortName" in SUITE_CODE and "ROUTES" in SUITE_CODE,
        "the mobile bar does not read the route table")
mobile = SUITE_CODE[SUITE_CODE.index("function ensureMobileNavigation"):]
mobile = mobile[:mobile.index("\n    function ")]
for durable in ("Plant overview", "Grid power", "Solar inverters", "Alarms and events",
                "PV-DG control", "Overview", "'Grid'", "'Solar'", "'Alarms'", "'Control'"):
    require(durable not in mobile,
            f"the mobile bar carries its own page label again: {durable}")
# The accessible name stays the durable one, so the label given over the phone
# is the label a screen reader announces.
require("routeName(route)" in mobile,
        "the mobile bar must still expose the full durable name; a short form is "
        "what is drawn, not what the page is called")


# The name is the navigation label, the title, the breadcrumb and document.title
# because there is only one string, applied in one place.
require("function applyRouteChrome" in APP,
        "title, breadcrumb and nav selection are not applied from one place")
chrome = APP[APP.index("function applyRouteChrome"):APP.index("function navigate()")]
for target in ("pageTitle", "breadcrumbCurrent", "document.title", "'.nav-link'"):
    require(target in chrome, f"route chrome does not set {target}")
require("const name = routeName(route);" in chrome,
        "the title and the breadcrumb must be the same string, not two strings")

# The screens that used to keep private name tables now defer to it.
for source, name in ((OPERATOR, "operator-view.js"), (OPERATIONS, "operator-operations.js")):
    require("applyRouteChrome" in source,
            f"{name} still writes its own title/breadcrumb instead of using the route table")
# product-mode.js used to call applyRouteChrome('engineering') from its own
# activateEngineeringRoute(). It no longer participates in route chrome at all:
# it injects the Engineering page and app.js selects it, either from the hash
# change or from the shared content notifier. Deferring to the route table is
# the weaker property; writing none of the chrome is the stronger one, so that
# is what is asserted here. Comments are stripped: the module documents the
# identifiers it must not touch by naming them.
MODE_CODE = re.sub(r"(?m)^\s*//.*$", "",
                   re.sub(r"/\*.*?\*/", "", MODE, flags=re.S))
for forbidden in ("pageTitle", "breadcrumbCurrent", "document.title", "applyRouteChrome"):
    require(forbidden not in MODE_CODE,
            f"product-mode.js must not touch route chrome ({forbidden}); app.js owns it")
require("'Grid Power' : 'Meters'" not in OPERATOR and "operator ? 'Grid Power'" not in OPERATOR,
        "sidebar labels must not change with the access level")


# ------------------------------------------------------ 2. the session banner

require('id="engineeringSessionBanner"' in INDEX, "there is no engineering session banner")
require("engineeringSessionRemaining" in INDEX and "engineeringSessionRemaining" in MODE,
        "the banner does not show remaining session time")
require("Session time remaining" in INDEX,
        "the remaining-time readout is unlabelled")
require("function sessionRemainingMs" in MODE and "function formatRemaining" in MODE,
        "remaining session time is not computed")
require("startSessionClock" in MODE and "setInterval" in MODE,
        "the remaining time is not counted down live")
require("renderSessionBanner" in MODE, "the banner is never rendered")

# The banner is persistent: it is part of the shell, not a page.
banner_index = INDEX.index('id="engineeringSessionBanner"')
require(INDEX.index('<main class="content"') > banner_index,
        "the session banner must sit in the shell, above the routed content")

# The length comes from the controller, and the countdown warns before zero.
require("session_timeout_minutes" in MODE,
        "the session length must come from the controller, not be hardcoded blind")
require("session_timeout_minutes" in AUTH,
        "the firmware no longer publishes the session length the banner reads")
require("AUTH_SESSION_TIMEOUT_MS (30ULL * 60ULL * 1000ULL)" in AUTH,
        "the 30-minute session window the banner describes has changed")
require("SESSION_WARN_MS" in MODE, "there is no warning before the session ends")
require("expiring" in MODE and ".engineering-session-banner.expiring" in APP_CSS,
        "the about-to-expire state is not shown")

# The clock may only be restarted by something that really extended the session
# server-side, so the figure is never longer than the truth.
require("function noteSessionActivity" in MODE and "function noteRequestActivity" in MODE,
        "the session clock has no defined anchor")
activity = MODE[MODE.index("function noteRequestActivity"):MODE.index("function renderSessionBanner")]
require("engineeringEndpointScope(url)" in activity,
        "only engineering-guarded requests extend the controller session; only they may move the clock")
require("status === 401" in activity,
        "a rejected request must not be counted as having extended the session")
require("session_cookie_valid(request, true)" in AUTH,
        "the firmware no longer extends the session per request; the clock model would be wrong")

# Both banner surfaces are declared in both themes. A token defined in one theme
# only is exactly how this codebase produced 1.10:1 and 1.17:1 text.
for token in ("--session-surface", "--session-line", "--session-text", "--session-muted",
              "--session-warn-surface", "--session-warn-line", "--session-warn-text"):
    require(token in APP_CSS, f"{token} is not declared for the dark theme")
    require(token in THEME_CSS, f"{token} is not declared for the light theme")


# ======================================================== 3. state vocabulary

require("const STATES = Object.freeze({" in APP, "there is no shared state vocabulary")
FAMILIES = {
    "dataQuality": ["'Good'", "'Stale'", "'Invalid'", "'Unavailable'"],
    "communication": ["'Online'", "'Degraded'", "'Offline'"],
    "commissioning": ["'Not configured'", "'Configured'", "'Qualified'", "'Failed'"],
    "control": ["'Active'", "'Standby'", "'Inhibited'", "'Faulted'"],
    "alarm": ["'Normal'", "'Warning'", "'Critical'"],
    "workflow": ["'Not started'", "'In progress'", "'Complete'", "'Blocked'"],
}
vocabulary = APP[APP.index("const STATES = Object.freeze({"):APP.index("function verbatim")]
for family, words in FAMILIES.items():
    require(f"{family}: Object.freeze({{" in vocabulary, f"state family missing: {family}")
    for word in words:
        require(word in vocabulary, f"{family} family is missing {word}")

require("STATES" in APP and "window.AutomatrixUi" in APP,
        "the vocabulary is not published to the other screens")
for source, name in ((OPERATOR, "operator-view.js"), (OPERATIONS, "operator-operations.js")):
    require("AutomatrixUi" in source, f"{name} does not consume the shared vocabulary")
require("function stateWord" in OPERATOR,
        "operator-view.js does not resolve its words through the vocabulary")

# The improvised words the audit listed are gone from the operator screens.
for banned in ["'Healthy'", "'Safely disabled'", "'Monitoring only'", "'Not commissioned'",
               "'Not monitored'", "'Review status'", "'Attention required'", "'Armed'",
               "'Production not monitored'", "'Alarm center'"]:
    require(banned not in OPERATOR,
            f"operator-view.js still uses an ad-hoc state word: {banned}")
    require(banned not in OPERATIONS,
            f"operator-operations.js still uses an ad-hoc state word: {banned}")


# -------------------------------- firmware states are shown, not paraphrased

require("function verbatim" in APP, "there is no verbatim rule")
require("function firmwareWord" in OPERATOR and "function controlAuthority" in OPERATOR,
        "the firmware's control-authority wording is not surfaced")
authority = OPERATOR[OPERATOR.index("function controlAuthority"):OPERATOR.index("function isOperator")]
require("authority?.mode_label" in authority,
        "the controller's own mode label must be used")
require("authority?.inhibit_reason" in authority,
        "the controller's own inhibit reason must be used")
require("firmwareWord(authority?.inhibit_reason" in authority,
        "the inhibit reason must pass through the verbatim rule, not be rewritten")
require("function measurementQuality" in OPERATOR,
        "the firmware's measurement quality token is not surfaced")

# It has to still be the firmware publishing these, or the copy above is wrong.
require('cJSON_AddStringToObject(authority, "mode_label"' in WEB_API,
        "the firmware no longer publishes a control mode label")
require('cJSON_AddStringToObject(authority, "inhibit_reason", control.inhibit_reason)' in WEB_API,
        "the firmware no longer publishes an inhibit reason")
require('cJSON_AddStringToObject(m, "quality", quality)' in WEB_API,
        "the firmware no longer publishes a grid measurement quality")

# Nothing may translate an inhibit reason into a friendlier word of its own.
for invention in ["Inhibited because", "Blocked because", "inhibitReasonText",
                  "REASON_LABELS", "translateInhibit"]:
    require(invention not in OPERATOR,
            f"the UI paraphrases a firmware safety statement: {invention}")

# The alarm condition names are the firmware's; the alarm screen already renders
# the controller's acknowledgement wording verbatim and must keep doing so.
require("result.note" in OPERATIONS,
        "the controller's own acknowledgement wording must be shown, not summarised")


# ============================================================ 4. trends
#
# These assertions used to name identifiers inside operator-operations.js
# (RANGE_WINDOW_MS, niceAxis, op-trend-tick, ...). Every one of them is now gone,
# because the two competing chart implementations were replaced by a single
# component in web/pvdg-chart.js. The PROPERTIES they were protecting are the
# real requirements, so they are re-expressed against whichever module now owns
# the chart. Asserting on identifier spelling was the brittleness: it fails on a
# rename that changes nothing and passes on a rewrite that changes everything.

require('/api/operator/history?range=' in OPERATIONS,
        "the trends do not read the controller history endpoint")

# EXACTLY ONE chart implementation. This is new, and it is the assertion that
# would have caught the original defect: two sparklines over the same two
# quantities, both on the dashboard at once, neither with a time axis.
for module, name in ((OPERATOR, "operator-view.js"), (OPERATIONS, "operator-operations.js")):
    require("function sparkline" not in module,
            f"{name} carries its own chart implementation again; there must be exactly one")
require("PvdgChart" in CHART, "the shared chart component must publish itself")

# An explicit time window per range, rather than plotting by array position.
require("const RANGES" in CHART, "the chart has no explicit range table")
for supported, window_ms in (("'15m'", "900000"), ("'1h'", "3600000"), ("'24h'", "86400000")):
    require(supported in CHART, f"supported history range missing: {supported}")
    require(window_ms in CHART,
            f"range {supported} has no explicit window in milliseconds; a trend "
            "plotted by array index cannot answer 'when'")

# The firmware substitutes 15m for anything it does not recognise, so offering a
# fourth range would silently mislabel 15 minutes of data.
for unsupported in ("'8h'", "'6h'", "'12h'", "'7d'"):
    require(unsupported not in CHART and unsupported not in OPERATIONS,
            f"an unsupported range is offered; the firmware would silently return 15m: {unsupported}")
require('strcmp(range, "1h")' in OPERATIONAL_API and 'strcmp(range, "24h")' in OPERATIONAL_API,
        "the firmware range set has changed; the range buttons would be wrong")

# Axes, units and a stated scale.
require("function niceScale" in CHART and "function niceStep" in CHART,
        "the chart has no computed value axis")
require("function timeStep" in CHART and "function formatClock" in CHART,
        "the chart has no time axis")
require("kW" in CHART, "the value axis does not name its unit")
require("function formatSpan" in CHART, "the visible range is not stated")
require("sample_interval_ms" in OPERATIONS,
        "the trend does not use the controller's declared sample interval")

# Current, minimum, maximum and average.
for label in ("'Current'", "'Minimum'", "'Average'", "'Peak'"):
    require(label in CHART, f"trend statistic missing: {label}")

# A gap must stay a gap. This is the one that matters on this product: an
# unmeasured interval drawn as a straight line, or worse plotted at zero, reads
# as "no power" on a controller whose whole purpose is preventing reverse power.
require("function gaps" in CHART and "function segments" in CHART,
        "the chart cannot represent missing data as a gap")
require(".filter(Number.isFinite)" not in CHART,
        "compacting the sample array deletes nulls and draws their neighbours "
        "adjacent -- an interpolated line across an unmeasured interval, with the "
        "surviving samples silently shifted in time")

# ============================================== 5. density without browser zoom

require("--page-max-operational" in APP_CSS and "--page-max-form" in APP_CSS
        and "--page-max-guided" in APP_CSS,
        "there are no page-type maximum widths")
require("const PAGE_TYPES" in APP and "data-page-type" not in APP.split("const PAGE_TYPES")[0],
        "page type is not derived from the route table")
require("dataset.pageType" in APP, "page type is never stamped on the document")
require('body[data-page-type="form"]' in DENSITY_CSS
        and 'body[data-page-type="guided"]' in DENSITY_CSS,
        "form and guided workflows do not get their own measure")
require(".app-shell .workspace .content { max-width: var(--page-max-operational); }" in DENSITY_CSS,
        "the operational measure must win on specificity, not on stylesheet order")

# Viewport-relative type is what made the app look like a different design at a
# different zoom level.
require("body { font-size: 15px; line-height: 1.5; }" in DENSITY_CSS,
        "body text is not set within the 14-16px band")
require(".metric-value, .op-kpi-value, .op-measure-reading strong { font-size: 28px;" in DENSITY_CSS,
        "primary numeric values are not sized in the 24-32px band")
# .op-gauge-copy strong used to be the third selector above. The semicircular
# gauge it belonged to is gone; the measurement bar that replaced it carries the
# page's largest number and is therefore governed by the same band. Asserted
# explicitly so the replacement cannot quietly escape the rule the gauge obeyed.
require(".op-measure-reading strong" in DENSITY_CSS,
        "the measurement bar's reading escapes the primary-value type band")
# Comment-stripped: this stylesheet's header explains WHY the gauge card was
# removed, and a prohibition that its own rationale trips is a prohibition that
# will be deleted rather than obeyed. The rule is about declarations.
for _sheet_name, _sheet in (("product-experience-v2.css", DENSITY_CSS),
                            ("product-mode.css", MODE_CSS),
                            ("operator-operations.css", OPERATIONS_CSS)):
    _rules = re.sub(r"/\*.*?\*/", "", _sheet, flags=re.S)
    require("op-gauge" not in _rules,
            f"the semicircular gauge is gone but {_sheet_name} still styles it")
# Font sizes must not be viewport-relative anywhere in the two stylesheets that
# set them. min(360px, calc(100vw - 40px)) on a fixed toast is a width, not type,
# and is left alone; a font-size in vw is the defect.
for name, sheet in (("app.css", APP_CSS), ("product-experience-v2.css", DENSITY_CSS)):
    for declaration in re.findall(r"font-size\s*:\s*[^;}]+", re.sub(r"/\*.*?\*/", "", sheet, flags=re.S)):
        require("vw" not in declaration and "vh" not in declaration,
                f"{name} sizes type from the viewport, so browser zoom changes the design: {declaration}")
require("min-height: 44px; min-width: 44px;" in DENSITY_CSS,
        "touch targets are not raised to 44x44")
require("@media (pointer: coarse), (max-width: 900px)" in DENSITY_CSS,
        "touch sizing must apply wherever touch is the pointer, not only on narrow screens")

# The .eyebrow AA failure recorded above is now resolved, by the second of the
# two options that change named: the accent was split into a fill token
# (--orange, unchanged, still the brand swatch that .button.primary and
# .brand-mark are contrast-matched to) and a text token (--accent-text). These
# assertions replace the earlier "leave it exactly as it is" pin with the
# stronger property that the pin was holding a place for.
require(".eyebrow { margin: 0; color: var(--accent-text);" in APP_CSS,
        "the .eyebrow accent must resolve through the theme-aware text token, "
        "not through the fill swatch")
require("--accent-text:" in APP_CSS and "--accent-text:" in THEME_CSS,
        "--accent-text must be defined in BOTH the dark base and the light theme; "
        "a token defined in only one theme is the defect class this file guards")
# The fill token must NOT be redefined per theme: the two foregrounds painted on
# it are chosen for that exact swatch, so a light-theme override breaks them.
require("--orange: #f28a2b;" in APP_CSS and "--orange:" not in THEME_CSS,
        "--orange is the fill swatch and must stay identical in both themes")
# No accent glyph, caption, rule or stroke may go back to the fill token.
require("color: var(--orange)" not in APP_CSS,
        "accent text/icon colour must use --accent-text, not the fill swatch")
require("5.21:1" in DENSITY_CSS,
        "the resolved .eyebrow contrast is not recorded where the open issue was")


# ------------------------------------------------------------ CI registration

require("tests/ia_taxonomy_source_contract.py" in WORKFLOW,
        "this contract is not registered in the build workflow")

print("information architecture, state taxonomy and trend source contract: PASS")
