/* app.js - router and controller-status publisher.
 *
 * OWNS: routing. The only module that may set `.page.active`,
 *   `.nav-link.active`, #pageTitle, #breadcrumbCurrent or document.title;
 *   route metadata lives in ROUTES below and nowhere else. Modules inject
 *   their page inactive and the router activates it. Also the /api/status
 *   poll, republished as amx-controller-status / amx-controller-health so no
 *   other module needs a second one; /api/config; the power-flow view.
 * DOES NOT OWN: authorisation, engineering-only visibility, the request scope
 *   predicate and the single #mainContent observer (product-mode.js); top bar
 *   and navigation list (product-shell-v2.js); page mastheads
 *   (product-experience-v2.js); operator content (operator-view.js).
 */
(() => {
    'use strict';

    /* ------------------------------------------------------ information architecture
     *
     * One durable name per page. The audit found the same screen called
     * "Dashboard" in the sidebar and "Plant overview" in its own heading,
     * "System" and "Controller", "Wi-Fi" and "Network setup", "Alarm center"
     * and "Alarms and events". An operator who is told over the phone to open
     * "Network setup" must be able to find it in the sidebar.
     *
     * `name` is that single name: it is the navigation label, the page title,
     * the breadcrumb and document.title. They cannot drift apart because there
     * is only one string. Route, title and selected navigation item have
     * disagreed here before - a route missing from this table fell back to
     * 'dashboard' and stamped that into document.title while rendering a
     * different section - so every route the product can reach has an entry,
     * including the ones later modules inject.
     *
     * `group` answers the question the audit said the interface could not:
     * whether Engineering is a role, a mode, a page or a menu group. It is a
     * session (Access); the pages that need it live in the group that matches
     * what they are FOR, not in a separate locked wing.
     *
     *   Operate    - watching a running plant
     *   Commission - bringing the site into service
     *   Maintain   - servicing and tuning an installed controller
     *   Access     - who you are signed in as
     *
     * Grouping is presentation only. Authorisation is still decided solely by
     * PROTECTED_ROUTES and the scope predicate in web/product-mode.js. */
    const NAV_GROUPS = [
        { id: 'operate', label: 'Operate', hint: 'Watching a running plant' },
        { id: 'commission', label: 'Commission', hint: 'Bringing the site into service' },
        { id: 'maintain', label: 'Maintain', hint: 'Servicing an installed controller' },
        { id: 'access', label: 'Access', hint: 'Engineering session' }
    ];

    const ROUTES = {
        /* Routes owned by later modules still need an entry here. Without one,
         * routeFromHash() fell back to 'dashboard' and stamped that into
         * document.title, so #/alarms reported itself as "Dashboard" while
         * correctly rendering the alarms section - the route, the title and the
         * selected navigation item disagreed. Every reachable route, including
         * commissioning and engineering, is therefore listed.
         *
         * The commissioning and engineering entries were also rewritten from
         * outside: commissioning-route.js (now deleted) and product-mode.js
         * each re-stamped their own title and breadcrumb after this router had
         * written its own, so the text depended on event ordering. Both other
         * writers are gone. The wording lives here, once, as `name`. */
        dashboard: { name: 'Plant overview', short: 'Overview', group: 'operate', icon: '⌂' },
        meters: { name: 'Grid power', short: 'Grid', group: 'operate', icon: '▤' },
        /* The generator's own page.
         *
         * Grid and generator readings used to share one page, so on a
         * single-meter tariff plant the generator's output was drawn under a
         * "Grid power" heading -- the screen stating the plant was importing
         * from the utility while it burned diesel. One supply per page; see
         * web/meter-source-routing.js. */
        generator: { name: 'Generator power', short: 'Generator', group: 'operate', icon: '◉' },
        inverters: { name: 'Solar inverters', short: 'Solar', group: 'operate', icon: '◇' },
        alarms: { name: 'Alarms and events', short: 'Alarms', group: 'operate', icon: '△' },
        /* The site owner's own Wi-Fi page, in Operate rather than Commission.
         * Changing the router is something the person who runs the plant does,
         * not a commissioning step -- and putting it in Commission would file it
         * under a heading they have been told not to touch. The engineer's
         * network page is the separate 'wifi' route below and keeps everything
         * that can strand a controller. */
        network: { name: 'Wi-Fi network', short: 'Wi-Fi', group: 'operate', icon: '≈' },
        commissioning: { name: 'Commissioning', group: 'commission', icon: '✓' },
        readiness: { name: 'Pre-lab readiness', short: 'Readiness', group: 'commission', icon: '⌾' },
        wifi: { name: 'Network setup', short: 'Network', group: 'commission', icon: '⌁' },
        control: { name: 'PV-DG control', short: 'Control', group: 'maintain', icon: '⇄' },
        system: { name: 'Controller', group: 'maintain', icon: '⚙' },
        engineering: { name: 'Engineering access', short: 'Engineering', group: 'access', icon: '▣' }
    };

    /* Page type drives the maximum measure. Operational screens are read across
     * and want the width; a form or a guided workflow is read down one column
     * and gets worse, not better, when the label ends up on one side of a
     * monitor and its field on the other. See the density block in
     * web/product-experience-v2.css. */
    const PAGE_TYPES = {
        dashboard: 'operational', meters: 'operational', generator: 'operational',
        inverters: 'operational',
        alarms: 'operational',
        network: 'form', wifi: 'form', control: 'form', system: 'form',
        commissioning: 'guided', readiness: 'guided', engineering: 'guided'
    };

    const ROUTE_ORDER = Object.keys(ROUTES);

    function routeName(route) {
        const meta = ROUTES[route];
        return meta ? meta.name : 'Controller';
    }

    /* `short` is a narrow-screen rendering OF `name`, not a second name.
     *
     * The bottom bar on a phone was carrying its own five-entry label list -
     * Overview / Grid / Solar / Alarms / Control - so the same pages were
     * called one thing on a phone and another thing everywhere else, which is
     * exactly the drift the `name` field above exists to make impossible. The
     * sidebar was being renamed from a second list too, and only looked right
     * because ensureNavigationHierarchy() overwrote it on the next mutation.
     *
     * A shorter label IS genuinely needed - five full names do not fit five
     * columns at 360px - so it lives on the route record, next to the name it
     * abbreviates, and it may only be built from words that are already in that
     * name. "Grid power" may shorten to "Grid"; it may not become "Meters".
     * An operator told over the phone to open Grid power still recognises it,
     * and tests/ia_taxonomy_source_contract.py holds the two together.
     *
     * A route with no `short` is one whose name is already short enough. */
    function routeShortName(route) {
        const meta = ROUTES[route];
        if (!meta) return routeName(route);
        return meta.short || meta.name;
    }

    /* ------------------------------------------------------------ state taxonomy
     *
     * Sixteen words were in use for six concepts - Online, Healthy, Ready,
     * Review, Monitoring, Monitoring only, Safely disabled, Disabled, Clear,
     * Normal, Not commissioned, Not monitored, Active, Cleared, Pass, Warning -
     * with "Review" standing in almost everywhere. On a controller that writes
     * inverter power limits, two words for one state is two states as far as
     * the reader is concerned.
     *
     * Six families, each closed. A screen picks the family that matches the
     * question it is answering and uses only that family's words.
     *
     * VERBATIM is the rule that keeps this honest. Where the firmware already
     * publishes a state - control_authority.mode_label, its inhibit_reason, the
     * alarm condition names, the grid measurement quality - that string is
     * displayed exactly as received. The interface must not paraphrase a safety
     * decision it did not make, and must not translate an inhibit reason into a
     * friendlier word than the controller chose. */
    const STATES = Object.freeze({
        dataQuality: Object.freeze({
            good: 'Good', stale: 'Stale', invalid: 'Invalid', unavailable: 'Unavailable'
        }),
        communication: Object.freeze({
            online: 'Online', degraded: 'Degraded', offline: 'Offline'
        }),
        commissioning: Object.freeze({
            notConfigured: 'Not configured', configured: 'Configured',
            qualified: 'Qualified', failed: 'Failed'
        }),
        control: Object.freeze({
            active: 'Active', standby: 'Standby', inhibited: 'Inhibited', faulted: 'Faulted'
        }),
        alarm: Object.freeze({ normal: 'Normal', warning: 'Warning', critical: 'Critical' }),
        workflow: Object.freeze({
            notStarted: 'Not started', inProgress: 'In progress',
            complete: 'Complete', blocked: 'Blocked'
        })
    });

    /* Returns the firmware's own wording untouched, and says plainly when the
     * firmware supplied nothing rather than inventing a state on its behalf. */
    function verbatim(value, absent = STATES.dataQuality.unavailable) {
        const text = typeof value === 'string' ? value.trim() : '';
        return text || absent;
    }

    const WIFI_STATES = ['Idle', 'Scanning', 'Connecting primary', 'Connecting fallback', 'Connected', 'Setup AP', 'Disconnected'];
    const CONTROL_MODES = ['Disabled', 'Grid', 'Generator', 'Manual', 'Failsafe', 'Emergency'];

    const state = {
        route: 'dashboard',
        config: null,
        status: null,
        /* Site capability report from the public /api/telemetry endpoint: which
         * quantities this installation actually measures. It is polled by
         * devices.js on the dashboard route and republished as an event, so the
         * flow model costs no additional request on a controller that can serve
         * only four client sockets (audit S1/S4). */
        telemetry: null,
        inverterTelemetry: null,
        sourceDetection: null,
        /* Why the source is not known, when it is not: an engineering session is
         * required for /api/source-detection, and "we may not ask" is a
         * different statement from "the controller says unknown". */
        sourceDetectionAbsence: '',
        refreshing: false,
        saving: false,
        lastUpdatedAt: null,
        /* Lab-simulator and commissioning-gate reads. Each is null until the
         * corresponding endpoint has actually answered, so "unknown" is never
         * rendered as "fine". */
        commissioningGate: null,
        solarGridStatus: null,
        /* Rebuild guard. web/product-mode.js keeps an unguarded MutationObserver
         * on #mainContent, so replacing this list on every poll would re-run its
         * DOM enforcement pass several times a minute for no change at all. */
        gateSignature: ''
    };

    const byId = (id) => document.getElementById(id);
    const all = (selector) => Array.from(document.querySelectorAll(selector));

    /* Idempotent primitives. renderStatus() writes ~50 fields every two
     * seconds on every route. Assigning textContent or className replaces the
     * text node or attribute even when the value is identical, so an unchanged
     * refresh used to emit a full set of DOM mutations and wake every observer
     * watching the shell. Compare first; unchanged means no-op. */
    function writeText(node, value) {
        if (node && node.textContent !== value) node.textContent = value;
    }

    function writeClass(node, value) {
        if (node && node.className !== value) node.className = value;
    }

    function setText(id, value) {
        writeText(byId(id), value == null || value === '' ? '--' : String(value));
    }

    function setTone(id, tone) {
        const node = byId(id);
        if (!node) return;
        ['good-text', 'warning-text', 'bad-text', 'muted-text'].forEach((name) => {
            node.classList.toggle(name, Boolean(tone) && name === `${tone}-text`);
        });
    }

    function setDot(id, tone) {
        writeClass(byId(id), `dot${tone ? ` ${tone}` : ''}`);
    }

    function setPill(id, label, tone) {
        const node = byId(id);
        if (!node) return;
        writeText(node, label);
        writeClass(node, `live-pill ${tone || 'neutral'}`);
    }

    function setBadge(id, label, tone) {
        const node = byId(id);
        if (!node) return;
        writeText(node, label);
        writeClass(node, `subtle-badge${tone ? ` ${tone}` : ''}`);
    }

    function setMessage(id, message, tone) {
        const node = byId(id);
        if (!node) return;
        writeText(node, message || '');
        writeClass(node, `action-message${tone ? ` ${tone}` : ''}`);
    }

    function formatPower(value) {
        const number = Number(value);
        return Number.isFinite(number) ? `${number.toFixed(2)} kW` : 'Unavailable';
    }

    const deviceUtils = () => window.PvdgDeviceUtils || null;

    /* Renders "EM500 slave 3 · unit 3 · 45 ms · Good" from the provenance the
     * status API publishes alongside a live value. Returns a plain statement of
     * ignorance rather than an empty string when provenance is absent, so a
     * value never appears more authoritative than it is. The implementation now
     * lives in devices-utils.js so the dashboard, the flow model and the
     * operator view describe the same measurement the same way. */
    function describeProvenance(provenance) {
        const utils = deviceUtils();
        return utils ? utils.describeProvenance(provenance) : 'Source unknown';
    }

    function formatAge(milliseconds) {
        const value = Number(milliseconds);
        if (!Number.isFinite(value) || value < 0) return 'Unavailable';
        if (value < 1000) return `${Math.round(value)} ms`;
        if (value < 60000) return `${(value / 1000).toFixed(1)} s`;
        return `${(value / 60000).toFixed(1)} min`;
    }

    function signalQuality(rssi) {
        const value = Number(rssi);
        if (!Number.isFinite(value) || value === 0) return 'Unavailable';
        if (value >= -55) return 'Excellent';
        if (value >= -67) return 'Good';
        if (value >= -75) return 'Fair';
        return 'Weak';
    }

    function gridDescriptor(power) {
        const value = Number(power);
        if (!Number.isFinite(value)) return { label: 'Unavailable', detail: 'Meter data required', arrow: '↔' };
        if (Math.abs(value) < 0.01) return { label: 'Balanced', detail: 'Near-zero grid exchange', arrow: '↔' };
        if (value > 0) return { label: 'Importing', detail: `${value.toFixed(2)} kW from grid`, arrow: '←' };
        return { label: 'Exporting', detail: `${Math.abs(value).toFixed(2)} kW to grid`, arrow: '→' };
    }

    async function api(path, options = {}) {
        const response = await fetch(path, { cache: 'no-store', ...options });
        const text = await response.text();
        if (!response.ok) {
            const error = new Error(text || `${response.status} ${response.statusText}`);
            /* The status is carried on the error because 401 is not a failure the
             * user needs to see as one - it means "sign in", and a screen that
             * cannot tell 401 from 500 either shows a broken page or hides a real
             * fault. */
            error.status = response.status;
            throw error;
        }
        if (!text) return null;
        try {
            return JSON.parse(text);
        } catch (error) {
            throw new Error(`Invalid controller response from ${path}`);
        }
    }

    function routeFromHash() {
        const route = window.location.hash.replace(/^#\/?/, '').trim();
        return ROUTES[route] ? route : 'dashboard';
    }

    /* Selection only, and idempotent: classList.toggle to a value already
     * present records no mutation. Re-run when another module injects its page
     * into #mainContent - that is how alarms, readiness, commissioning and
     * engineering become active without each shipping its own router. */
    function applyRoute() {
        state.route = routeFromHash();
        all('.page').forEach((page) => page.classList.toggle('active', page.dataset.page === state.route));
        all('.nav-link').forEach((link) => link.classList.toggle('active', link.dataset.route === state.route));
    }

    /* ------------------------------------------------- navigation hierarchy
     *
     * Several modules add, relabel or reorder sidebar entries after this one
     * runs: the alarm centre inserts its own link, the readiness and
     * commissioning modules insert theirs, the operator suite renames five of
     * them on every route change and moves the alarm link next to Controller.
     * Those modules are owned elsewhere. Rather than fight them one by one,
     * this function is the single place that decides where a route sits and
     * what it is called, and it is re-run whenever the sidebar changes.
     *
     * It is idempotent by construction: it only writes when the DOM already
     * disagrees with the table, so re-running it on its own mutations settles
     * immediately instead of looping. */
    let navPassRunning = false;

    function navGroupHeading(group) {
        const nav = document.querySelector('.nav-list');
        if (!nav) return null;
        let heading = nav.querySelector(`[data-nav-group="${group.id}"]`);
        if (!heading) {
            heading = document.createElement('p');
            heading.className = 'nav-group-title';
            heading.dataset.navGroup = group.id;
            heading.textContent = group.label;
            heading.title = group.hint;
            nav.append(heading);
        }
        return heading;
    }

    function ensureNavigationHierarchy() {
        const nav = document.querySelector('.nav-list');
        if (!nav || navPassRunning) return;
        navPassRunning = true;
        try {
            const wanted = [];
            NAV_GROUPS.forEach((group) => {
                const heading = navGroupHeading(group);
                if (!heading) return;
                wanted.push(heading);
                ROUTE_ORDER.filter((route) => ROUTES[route].group === group.id).forEach((route) => {
                    const link = nav.querySelector(`.nav-link[data-route="${route}"]`);
                    if (!link) return;
                    /* One durable name, applied after any module that renamed it. */
                    const spans = link.querySelectorAll('span');
                    if (spans[0] && spans[0].textContent !== ROUTES[route].icon) {
                        spans[0].textContent = ROUTES[route].icon;
                    }
                    if (spans[1] && spans[1].textContent !== ROUTES[route].name) {
                        spans[1].textContent = ROUTES[route].name;
                    }
                    if (link.getAttribute('aria-label') !== ROUTES[route].name) {
                        link.setAttribute('aria-label', ROUTES[route].name);
                    }
                    wanted.push(link);
                });
            });
            /* Anything this table does not know about keeps its current place at
             * the end rather than being deleted: an unknown link is a module
             * this file has not been told about, not rubbish. */
            const known = new Set(wanted);
            Array.from(nav.children).forEach((child) => {
                if (!known.has(child) && child.classList.contains('nav-link')) wanted.push(child);
            });
            let cursor = null;
            wanted.forEach((element) => {
                const expected = cursor ? cursor.nextElementSibling : nav.firstElementChild;
                if (expected !== element) nav.insertBefore(element, expected);
                cursor = element;
            });
            /* A group whose every route is hidden must not leave a heading over
             * nothing. Engineering hides its own links when locked. */
            NAV_GROUPS.forEach((group) => {
                const heading = nav.querySelector(`[data-nav-group="${group.id}"]`);
                if (!heading) return;
                const visible = ROUTE_ORDER
                    .filter((route) => ROUTES[route].group === group.id)
                    .some((route) => {
                        const link = nav.querySelector(`.nav-link[data-route="${route}"]`);
                        return link && !link.hidden;
                    });
                if (heading.hidden !== !visible) heading.hidden = !visible;
            });
        } finally {
            navPassRunning = false;
        }
    }

    /* Title, breadcrumb, document title and the selected navigation item all
     * come from the one name, in one place, so they cannot disagree. Other
     * modules call this rather than writing their own three strings. */
    function applyRouteChrome(route = routeFromHash()) {
        const name = routeName(route);
        setText('pageTitle', name);
        setText('breadcrumbCurrent', name);
        /* Idempotent like the rest of the chrome: this runs on every sidebar
         * mutation, and assigning an identical document.title still notifies
         * anything watching the title. */
        if (document.title !== `${name} · Automatrix PV-DG`) {
            document.title = `${name} · Automatrix PV-DG`;
        }
        all('.nav-link').forEach((link) => link.classList.toggle('active', link.dataset.route === route));
        const pageType = PAGE_TYPES[route] || 'operational';
        if (document.body && document.body.dataset.pageType !== pageType) {
            document.body.dataset.pageType = pageType;
        }
    }

    function navigate() {
        applyRoute();
        ensureNavigationHierarchy();
        applyRouteChrome(state.route);
        closeMenu();
    }

    function watchNavigation() {
        const nav = document.querySelector('.nav-list');
        if (!nav) return;
        /* The sidebar is ten links; watching its text as well as its children is
         * what lets one durable name survive a module that renames five of them
         * on every route change. */
        new MutationObserver(() => {
            ensureNavigationHierarchy();
            applyRouteChrome();
        }).observe(nav, { childList: true, subtree: true, characterData: true });
        window.addEventListener('amx-access-change', () => {
            ensureNavigationHierarchy();
            applyRouteChrome();
        });
    }

    function openMenu() {
        document.body.classList.add('menu-open');
        byId('menuButton').setAttribute('aria-expanded', 'true');
    }

    function closeMenu() {
        document.body.classList.remove('menu-open');
        byId('menuButton').setAttribute('aria-expanded', 'false');
    }

    /* ------------------------------------------------------------ power flow
     *
     * P0-7. The flow view carried solar, controller and utility grid. On a PV-DG
     * controller the generator is the asset the entire control strategy exists
     * to protect and facility load is what both sources are there to serve, so
     * both are now first-class nodes. Where a quantity is not measured on this
     * site the node says exactly that: showing 0 kW for an unmetered generator
     * would tell an operator it is off-load when in fact nothing is watching it.
     * The model itself lives in devices-utils.js and is unit-tested. */
    function flowNodeElement(entry) {
        const node = document.createElement('article');
        node.className = `flow-node flow-${entry.role}${entry.measured ? '' : ' flow-unmeasured'}${entry.tone ? ` tone-${entry.tone}` : ''}`;
        node.dataset.flowNode = entry.id;

        const head = document.createElement('span');
        head.className = 'flow-node-label';
        head.textContent = entry.label;

        const value = document.createElement('strong');
        value.className = 'flow-node-value';
        value.textContent = entry.value;

        const kind = document.createElement('span');
        kind.className = `flow-kind flow-kind-${entry.valueKind}`;
        kind.textContent = entry.valueKind === 'measured' ? 'Measured'
            : entry.valueKind === 'command' ? 'Commanded'
            : 'Not measured';

        const detail = document.createElement('small');
        detail.className = 'flow-node-detail';
        detail.textContent = entry.detail;

        const provenance = document.createElement('small');
        provenance.className = 'flow-node-provenance';
        provenance.textContent = entry.provenance;

        node.append(head, value, kind, detail, provenance);
        (entry.notes || []).forEach((note) => {
            const line = document.createElement('small');
            line.className = 'flow-node-note';
            line.textContent = note;
            node.append(line);
        });
        return node;
    }

    function flowConnector(entry) {
        const line = document.createElement('div');
        line.className = `flow-line${entry.direction === 'unknown' ? ' flow-line-unknown' : ''}`;
        const glyph = document.createElement('span');
        glyph.setAttribute('aria-hidden', 'true');
        glyph.textContent = entry.arrow;
        const label = document.createElement('small');
        label.textContent = entry.directionLabel;
        line.append(glyph, label);
        return line;
    }

    function renderPowerFlow() {
        const container = byId('powerFlow');
        const utils = deviceUtils();
        if (!container || !utils || !state.status) return;

        const model = utils.buildPowerFlowModel({
            status: state.status,
            telemetry: state.telemetry,
            inverterTelemetry: state.inverterTelemetry,
            sourceDetection: state.sourceDetection
        });

        /* A full rebuild of ~50 elements, driven by a two-second poll. It used
         * to run whether or not a number had moved. */
        const signature = JSON.stringify(model);
        if (container.dataset.flowSignature === signature) return;
        container.dataset.flowSignature = signature;

        container.replaceChildren();
        const supply = document.createElement('div');
        supply.className = 'flow-column flow-supply';
        const demand = document.createElement('div');
        demand.className = 'flow-column flow-demand';
        const hub = document.createElement('div');
        hub.className = 'flow-column flow-hub';

        model.nodes.forEach((entry) => {
            const wrap = document.createElement('div');
            wrap.className = 'flow-slot';
            wrap.append(flowNodeElement(entry), flowConnector(entry));
            if (entry.role === 'supply') supply.append(wrap);
            else if (entry.role === 'demand') demand.append(wrap);
            else hub.append(wrap);
        });
        container.append(supply, hub, demand);

        setBadge('flowState', model.summary.label, model.summary.tone === 'neutral' ? '' : model.summary.tone);
        setText('flowCoverage', model.summary.coverage);
        setText('flowMeterCount', model.summary.meters_configured == null
            ? 'Unavailable'
            : `${model.summary.meters_configured} configured`);
    }

    /* devices.js already polls the public /api/telemetry on the dashboard route.
     * Reusing its answer instead of issuing a second one keeps the flow model
     * free of additional socket pressure on a server with four client sockets. */
    function bindSiteTelemetry() {
        window.addEventListener('amx-site-telemetry', (event) => {
            state.telemetry = event.detail || null;
            renderPowerFlow();
        });
    }

    /* Source detection is engineering-gated. It is requested only when the
     * shared scope predicate says the request can succeed, so the operator
     * dashboard never fires a guaranteed 401 (audit S4). */
    async function refreshSourceDetection() {
        const access = window.AutomatrixEngineeringAccess;
        if (!access || !access.mayUseEngineering('dashboard')) {
            state.sourceDetection = null;
            state.sourceDetectionAbsence = 'An engineering session is required before the controller will report the active source.';
            renderSourceBanner();
            return;
        }
        try {
            state.sourceDetection = await api('/api/source-detection');
            state.sourceDetectionAbsence = '';
        } catch (error) {
            state.sourceDetection = null;
            state.sourceDetectionAbsence = 'The source-detection read failed, so the active source is not known to this screen.';
        }
        /*
         * Published for the product view, which shows the same evidence on the
         * plant overview and must not fetch it again: this board has few client
         * sockets and the endpoint is one Modbus-backed read.
         *
         * Published HERE, where it is fetched, rather than where it is drawn --
         * the banner that used to draw it is hidden on the plant overview now,
         * and a cache that only fills while something is on screen is a cache
         * that is empty exactly when the other view asks.
         *
         * A CACHE, not an API: whoever reads it gets what this poll last saw,
         * and null when the read failed.
         */
        window.AutomatrixSourceDetectionCache = state.sourceDetection;
        renderSourceBanner();
        renderPowerFlow();
    }

    /* ------------------------------------------------------- active power source
     *
     * GRID or GENERATOR changes what the controller is allowed to do, so the
     * dashboard states it in one word rather than leaving it to be inferred from
     * a generator node in the flow diagram. Every string below comes from
     * PvdgSourceDetectionUtils (web/source-detection.js), which is pure and
     * property-tested: it renders a confident identity only when the firmware
     * has actually resolved one, and UNKNOWN whenever it is fail-closed,
     * debouncing, unconfigured, stale, in conflict, or silent. This function
     * does not decide any of that; it only writes what it is handed. */
    function renderSourceBanner() {
        if (!byId('sourceBanner')) return;
        const utils = window.PvdgSourceDetectionUtils;
        if (!utils) return;
        const view = utils.describeSourceDetection(state.sourceDetection);

        setText('sourceBannerLabel', view.sourceLabel);
        setTone('sourceBannerLabel', view.sourceTone);
        setBadge('sourceBannerBadge', view.qualityLabel,
            view.confident ? (view.sourceTone === 'good' ? 'good' : 'warning') : 'bad');
        setText('sourceBannerDetail',
            view.present ? view.detail : (state.sourceDetectionAbsence || view.detail));
        setText('sourceBannerQuality', view.qualityLabel);
        setText('sourceBannerConsequence', view.controlConsequence);
        setTone('sourceBannerConsequence', view.confident && view.state === 'grid' ? 'good' : view.confident ? 'warning' : 'bad');
        setText('sourceBannerTariff', view.tariffLabel);

        const consequenceDetail = byId('sourceBannerConsequenceDetail');
        if (consequenceDetail) {
            writeText(consequenceDetail, view.controlConsequenceDetail);
            writeClass(consequenceDetail, view.confident && view.state === 'grid' ? 'notice safe' : 'notice warning');
        }
        /* The firmware's own sentences, or nothing at all. They are never
         * paraphrased and never replaced with a friendlier summary. */
        const reason = byId('sourceBannerReason');
        if (reason) {
            writeText(reason, view.reasonText);
            if (reason.hidden !== !view.reasonText) reason.hidden = !view.reasonText;
        }
        const limitation = byId('sourceBannerLimitation');
        if (limitation) {
            writeText(limitation, view.limitationText);
            if (limitation.hidden !== !view.limitationText) limitation.hidden = !view.limitationText;
        }
    }

    function renderControllerUnavailable() {
        setPill('controllerPill', 'Controller unavailable', 'bad');
        setText('statusController', 'Offline');
        setText('statusNetwork', 'Unavailable');
        setText('statusMeter', 'Unavailable');
        setText('statusUpdated', 'Refresh failed');
        setText('sidebarState', 'Controller unavailable');
        setTone('statusController', 'bad');
        publishHealth({
            controller: 'Offline', network: 'Unavailable', meter: 'Unavailable',
            control: 'Unavailable', alarms: 'Unavailable',
            online: false, meterHealthy: false, controlEnabled: false, alarmCount: 0
        });
    }

    function renderStatus() {
        const status = state.status;
        if (!status) return;

        const networkOnline = Boolean(status.network_online);
        const meterHasData = Boolean(status.meter_has_data);
        const meterFresh = Boolean(status.meter_online) && !Boolean(status.meter_stale);
        const meterStale = meterHasData && Boolean(status.meter_stale);
        const controlEnabled = Boolean(status.control_enabled);
        const alarmNames = Array.isArray(status.alarm_names) ? status.alarm_names : [];
        const wifiState = WIFI_STATES[Number(status.wifi_state)] || `State ${status.wifi_state}`;
        const connectionRole = status.using_fallback_sta ? 'Fallback STA' : status.fallback_ap_active ? 'Recovery AP' : 'Primary STA';
        const mode = CONTROL_MODES[Number(status.mode)] || `Mode ${status.mode}`;
        const updated = state.lastUpdatedAt ? state.lastUpdatedAt.toLocaleTimeString() : 'Now';

        setPill('controllerPill', networkOnline ? `Online · ${status.ip || '--'}` : status.fallback_ap_active ? 'Recovery AP active' : 'Network offline', networkOnline ? 'good' : status.fallback_ap_active ? 'warning' : 'bad');
        setText('statusController', networkOnline ? 'Online' : 'Offline');
        setText('statusNetwork', networkOnline ? `${status.ssid || '--'} · ${status.ip || '--'}` : wifiState);
        setText('statusMeter', meterFresh ? 'Online' : meterStale ? 'Stale' : 'Offline');
        setText('statusControl', controlEnabled ? `Enabled · ${mode}` : 'Disabled');
        setText('statusAlarms', alarmNames.length ? `${alarmNames.length} active` : 'Clear');
        setText('statusUpdated', updated);
        setTone('statusController', networkOnline ? 'good' : 'bad');
        setTone('statusMeter', meterFresh ? 'good' : meterStale ? 'warning' : 'bad');
        setTone('statusControl', controlEnabled ? 'warning' : 'good');
        setTone('statusAlarms', alarmNames.length ? 'bad' : 'good');
        setText('sidebarState', networkOnline ? `Online on ${status.ssid || 'Wi-Fi'}` : wifiState);

        setText('dashboardMode', controlEnabled ? mode : 'Control disabled');
        setTone('dashboardMode', controlEnabled ? 'warning' : 'good');

        /* Every live value states where it came from, how old it is and whether
         * it can be trusted. Without this the same signal sampled at two
         * instants reads as two disagreeing measurements, and an operator
         * cannot tell which one the controller is acting on. */
        setText('gridPowerProvenance', describeProvenance(status.grid_measurement));

        /*
         * WHOSE POWER THIS CARD IS SHOWING.
         *
         * The card is headed "Grid power" in the markup, and the plant overview
         * showed 371.82 kW under it while the controller had resolved GENERATOR
         * and the site energy balance directly below correctly said GENERATOR.
         * Two panels on one screen, disagreeing about the same measurement.
         *
         * On a single-meter tariff plant one meter measures whichever source is
         * live. See web/source-attribution.js -- the rule is the firmware's, not
         * re-derived here.
         */
        const supply = window.AutomatrixSource?.attribution(status)
            || { node: 'grid', label: 'Grid', known: true, reason: '' };
        const cardTitle = document.querySelector('#gridDot')?.closest('.metric-head')?.querySelector('span');
        if (cardTitle) {
            cardTitle.textContent = supply.node === 'generator' ? 'Generator power'
                : supply.known ? 'Grid power' : 'Live source power';
        }

        if (meterFresh) {
            setText('gridPowerValue', formatPower(status.grid_power_kw));
            const descriptor = gridDescriptor(status.grid_power_kw);
            /* The direction wording belongs to whichever source it is: negative
             * is ordinary export on a grid and reverse power -- a fault -- on a
             * generator. */
            setText('gridPowerDetail', supply.known
                ? (supply.node === 'generator'
                    ? `${supply.direction(Number(status.grid_power_kw))} · the plant is on the generator`
                    : descriptor.detail)
                : supply.reason);
            setDot('gridDot', Number(status.grid_power_kw) > 0 ? 'warning' : 'good');
            setText('meterHealthValue', 'Online');
            setText('meterHealthDetail', `Fresh sample · ${formatAge(status.meter_age_ms)}`);
            setDot('meterDot', 'good');
        } else if (meterStale) {
            setText('gridPowerValue', `${formatPower(status.grid_power_kw)} stale`);
            setText('gridPowerDetail', supply.known
                ? `Last valid ${supply.label.toLowerCase()} sample retained; not current`
                : 'Last valid sample retained; not current');
            setDot('gridDot', 'warning');
            setText('meterHealthValue', 'Stale');
            setText('meterHealthDetail', `Last sample ${formatAge(status.meter_age_ms)} ago`);
            setDot('meterDot', 'warning');
        } else {
            setText('gridPowerValue', 'Unavailable');
            setText('gridPowerDetail', 'No valid meter sample available');
            setDot('gridDot', 'bad');
            setText('meterHealthValue', 'Offline');
            setText('meterHealthDetail', `${Number(status.meter_errors) || 0} communication errors`);
            setDot('meterDot', 'bad');
        }

        setText('requestedPvValue', Number.isFinite(Number(status.requested_pv_kw)) ? formatPower(status.requested_pv_kw) : 'Unavailable');
        setText('appliedPvValue', Number.isFinite(Number(status.applied_pv_kw)) ? formatPower(status.applied_pv_kw) : 'Unavailable');
        setText('flowMeterAge', meterHasData ? formatAge(status.meter_age_ms) : 'Unavailable');
        renderPowerFlow();

        setText('healthWifi', networkOnline ? `${connectionRole} · ${status.ssid || '--'}` : wifiState);
        setTone('healthWifi', networkOnline ? 'good' : 'bad');
        setText('healthIp', status.ip || 'Unavailable');
        setText('healthMeter', meterFresh ? 'Online and fresh' : meterStale ? 'Stale; last value retained' : 'Offline');
        setTone('healthMeter', meterFresh ? 'good' : meterStale ? 'warning' : 'bad');
        setText('healthControl', controlEnabled ? `Enabled · ${mode}` : 'Disabled · no commands issued');
        setTone('healthControl', controlEnabled ? 'warning' : 'good');
        setText('healthAlarms', alarmNames.length ? alarmNames.join(', ') : 'Alarms clear');
        setTone('healthAlarms', alarmNames.length ? 'bad' : 'good');

        setText('wifiCurrentSsid', status.ssid || 'Unavailable');
        setText('wifiConnectionRole', `${connectionRole} · ${wifiState}`);
        setText('wifiCurrentIp', status.ip || 'Unavailable');
        setText('wifiNetworkDetail', `GW ${status.gateway || '--'} · Mask ${status.netmask || '--'}`);
        setText('wifiCurrentRssi', Number(status.rssi) ? `${status.rssi} dBm` : 'Unavailable');
        setText('wifiSignalQuality', signalQuality(status.rssi));
        setText('wifiRecoveryState', status.fallback_ap_active ? 'Active' : 'Inactive');
        setText('wifiRecoveryDetail', status.fallback_ap_active ? 'Setup network available' : 'Not currently serving');

        setText('meterPagePower', meterFresh ? formatPower(status.grid_power_kw) : meterStale ? `${formatPower(status.grid_power_kw)} stale` : 'Unavailable');
        setText('meterPageAge', meterHasData ? formatAge(status.meter_age_ms) : 'Unavailable');
        setText('meterPageErrors', Number(status.meter_errors) || 0);
        setBadge('meterPageBadge', meterFresh ? 'Online' : meterStale ? 'Stale' : 'Offline', meterFresh ? 'good' : meterStale ? 'warning' : 'bad');

        setText('controlPageMode', controlEnabled ? mode : 'Disabled');
        setText('controlPageRequested', Number.isFinite(Number(status.requested_pv_kw)) ? formatPower(status.requested_pv_kw) : 'Unavailable');
        setText('controlPageApplied', Number.isFinite(Number(status.applied_pv_kw)) ? formatPower(status.applied_pv_kw) : 'Unavailable');
        setText('controlPageMeter', meterFresh ? 'Ready' : meterStale ? 'Interlocked: stale' : 'Interlocked: offline');
        setTone('controlPageMeter', meterFresh ? 'good' : 'bad');
        setText('controlSafetyTitle', controlEnabled ? 'Automatic control enabled' : 'Automatic control disabled');
        setText('controlSafetyDetail', controlEnabled ? 'Commissioning safeguards must be verified before operation.' : 'No inverter commands are issued while control is disabled.');

        setText('systemIp', status.ip || 'Unavailable');
        setText('systemSsid', status.ssid || 'Unavailable');

        /* control_authority.mode_label and inhibit_reason ride the public status
         * poll, so the gate panel's two verbatim rows stay current even while the
         * engineering-guarded gate read is unavailable. */
        renderCommissioningGate();

        publishHealth({
            controller: networkOnline ? 'Online' : 'Offline',
            network: networkOnline ? `${status.ssid || '--'} · ${status.ip || '--'}` : wifiState,
            meter: meterFresh ? 'Online' : meterStale ? 'Stale' : 'Offline',
            control: controlEnabled ? `Enabled · ${mode}` : 'Disabled',
            alarms: alarmNames.length ? `${alarmNames.length} active` : 'Clear',
            online: networkOnline,
            meterHealthy: meterFresh,
            controlEnabled,
            alarmCount: alarmNames.length
        });
    }

    /* The shell header used to derive its indicator by reading the rendered
     * status strip back out of the DOM and regex-matching the English words in
     * it, behind a characterData observer that fired every two seconds. The
     * renderer knows the answer as data, so it states it. amx-controller-status
     * carries the raw payload so nothing needs a second /api/status. */
    let lastHealthSignature = '';
    function publishHealth(health) {
        const tone = !health.online || health.alarmCount > 0 || !health.meterHealthy ? 'bad'
            : !health.controlEnabled ? 'warning'
            : 'good';
        const label = tone === 'bad' ? 'Attention' : tone === 'warning' ? 'Review' : 'Normal';
        const detail = { ...health, tone, label };
        const signature = JSON.stringify(detail);
        if (signature === lastHealthSignature) return;
        lastHealthSignature = signature;
        window.dispatchEvent(new CustomEvent('amx-controller-health', { detail }));
    }

    async function refreshStatus() {
        if (state.refreshing) return;
        state.refreshing = true;
        try {
            state.status = await api('/api/status');
            /* Published for the product view, which renders from the same
             * payload on its own timer. It was fetching /api/status again --
             * a second request for a fact already in the browser, on a
             * controller with very few client sockets. A CACHE with the instant
             * it was taken, so a reader can refuse a stale one. */
            window.AutomatrixStatusCache = { at: Date.now(), payload: state.status };
            state.lastUpdatedAt = new Date();
            renderStatus();
            window.dispatchEvent(new CustomEvent('amx-controller-status', { detail: state.status }));
        } catch (error) {
            renderControllerUnavailable();
        } finally {
            state.refreshing = false;
        }
    }

    function setProfileForm(prefix, profile = {}) {
        byId(`${prefix}Enabled`).checked = Boolean(profile.enabled);
        byId(`${prefix}Ssid`).value = profile.ssid || '';
        byId(`${prefix}Password`).value = '';
        byId(`${prefix}Mode`).value = String(profile.ip_mode ?? 0);
        byId(`${prefix}Ip`).value = profile.static_ip || '';
        byId(`${prefix}Gateway`).value = profile.gateway || '';
        byId(`${prefix}Netmask`).value = profile.netmask || '';
        byId(`${prefix}Dns1`).value = profile.dns1 || '';
        byId(`${prefix}Dns2`).value = profile.dns2 || '';
        updateStaticFieldState(prefix);
    }

    function collectProfile(prefix, profile) {
        profile.enabled = byId(`${prefix}Enabled`).checked;
        profile.ssid = byId(`${prefix}Ssid`).value.trim();
        const password = byId(`${prefix}Password`).value;
        if (password) profile.password = password;
        profile.ip_mode = Number(byId(`${prefix}Mode`).value);
        profile.static_ip = byId(`${prefix}Ip`).value.trim();
        profile.gateway = byId(`${prefix}Gateway`).value.trim();
        profile.netmask = byId(`${prefix}Netmask`).value.trim();
        profile.dns1 = byId(`${prefix}Dns1`).value.trim();
        profile.dns2 = byId(`${prefix}Dns2`).value.trim();
    }

    function updateStaticFieldState(prefix) {
        const isStatic = Number(byId(`${prefix}Mode`).value) === 1;
        all(`.static-${prefix}`).forEach((label) => {
            const input = label.querySelector('input');
            if (input) input.disabled = !isStatic;
        });
    }

    function renderConfig() {
        const config = state.config;
        if (!config) return;
        const wifi = config.wifi || {};
        const meter = Array.isArray(config.meters) && config.meters[0] ? config.meters[0] : {};
        const control = config.control || {};

        setProfileForm('primary', wifi.primary || {});
        setProfileForm('fallback', wifi.fallback || {});
        byId('scanBeforeConnect').checked = Boolean(wifi.scan_before_connect);
        byId('recoveryEnabled').checked = true;
        byId('recoverySsid').value = wifi.fallback_ap_ssid || '';
        byId('recoveryPassword').value = '';
        byId('wifiRetries').value = wifi.max_retries_per_profile ?? 5;
        byId('wifiBackoff').value = wifi.reconnect_backoff_ms ?? 2000;

        setText('meterNameHeading', meter.name || 'Grid meter');
        byId('meterHost').value = meter.host || '';
        byId('meterPort').value = meter.port ?? 502;
        byId('meterUnit').value = meter.unit_id ?? 1;
        byId('meterAddress').value = meter.active_power_address ?? 0;
        byId('meterScale').value = meter.scale ?? 1;
        byId('meterPoll').value = meter.poll_ms ?? 1000;
        byId('meterTimeout').value = meter.timeout_ms ?? 1500;

        byId('controlTarget').value = control.grid_import_target_kw ?? 0;
        byId('controlDeadband').value = control.deadband_kw ?? 0;
        byId('controlInterval').value = control.interval_ms ?? 250;
        byId('controlStaleTimeout').value = control.meter_stale_timeout_ms ?? 3000;

        setText('systemDeviceName', config.device_name || '--');
        setText('systemSchema', config.schema ?? '--');
        setText('sidebarVersion', `Configuration schema ${config.schema ?? '--'} · ${config.device_name || 'Controller'}`);
        byId('advancedJson').value = JSON.stringify(config, null, 2);
    }

    async function loadConfig() {
        state.config = await api('/api/config');
        renderConfig();
        return state.config;
    }

    function clearValidation() {
        all('.field.invalid').forEach((field) => field.classList.remove('invalid'));
        all('.field-error').forEach((error) => error.remove());
    }

    function markInvalid(inputId, message) {
        const input = byId(inputId);
        const field = input ? input.closest('.field') : null;
        if (!field) return;
        field.classList.add('invalid');
        const error = document.createElement('span');
        error.className = 'field-error';
        error.textContent = message;
        field.appendChild(error);
    }

    function isIpv4(value) {
        const parts = String(value).trim().split('.');
        return parts.length === 4 && parts.every((part) => /^\d{1,3}$/.test(part) && Number(part) >= 0 && Number(part) <= 255);
    }

    function validateProfile(prefix, label) {
        let valid = true;
        const enabled = byId(`${prefix}Enabled`).checked;
        const ssid = byId(`${prefix}Ssid`).value.trim();
        const mode = Number(byId(`${prefix}Mode`).value);
        if (enabled && !ssid) {
            markInvalid(`${prefix}Ssid`, `${label} SSID is required when enabled.`);
            valid = false;
        }
        if (mode === 1) {
            const required = [
                [`${prefix}Ip`, 'A valid static IP is required.'],
                [`${prefix}Gateway`, 'A valid gateway is required.'],
                [`${prefix}Netmask`, 'A valid netmask is required.']
            ];
            required.forEach(([id, message]) => {
                if (!isIpv4(byId(id).value)) {
                    markInvalid(id, message);
                    valid = false;
                }
            });
            [`${prefix}Dns1`, `${prefix}Dns2`].forEach((id) => {
                const value = byId(id).value.trim();
                if (value && !isIpv4(value)) {
                    markInvalid(id, 'Enter a valid IPv4 address or leave blank.');
                    valid = false;
                }
            });
        }
        return valid;
    }

    function collectWifiConfig() {
        if (!state.config || !state.config.wifi) throw new Error('Configuration is not loaded.');
        clearValidation();
        const primaryValid = validateProfile('primary', 'Primary');
        const fallbackValid = validateProfile('fallback', 'Fallback');
        const retries = Number(byId('wifiRetries').value);
        const backoff = Number(byId('wifiBackoff').value);
        if (!Number.isInteger(retries) || retries < 1 || retries > 20) markInvalid('wifiRetries', 'Retries must be between 1 and 20.');
        if (!Number.isFinite(backoff) || backoff < 500 || backoff > 60000) markInvalid('wifiBackoff', 'Reconnect delay must be 500–60000 ms.');
        if (!primaryValid || !fallbackValid || !Number.isInteger(retries) || retries < 1 || retries > 20 || !Number.isFinite(backoff) || backoff < 500 || backoff > 60000) {
            throw new Error('Correct the highlighted Wi-Fi settings.');
        }

        const wifi = state.config.wifi;
        collectProfile('primary', wifi.primary);
        collectProfile('fallback', wifi.fallback);
        wifi.scan_before_connect = byId('scanBeforeConnect').checked;
        // Always on; the firmware rejects a request that would disable it.
        wifi.fallback_ap_enabled = true;
        wifi.fallback_ap_ssid = byId('recoverySsid').value.trim();
        const recoveryPassword = byId('recoveryPassword').value;
        if (recoveryPassword) wifi.fallback_ap_password = recoveryPassword;
        wifi.max_retries_per_profile = retries;
        wifi.reconnect_backoff_ms = backoff;
    }

    function collectMeterConfig() {
        if (!state.config || !Array.isArray(state.config.meters) || !state.config.meters[0]) throw new Error('Meter configuration is unavailable.');
        clearValidation();
        const host = byId('meterHost').value.trim();
        const port = Number(byId('meterPort').value);
        const unitId = Number(byId('meterUnit').value);
        const address = Number(byId('meterAddress').value);
        const scale = Number(byId('meterScale').value);
        const poll = Number(byId('meterPoll').value);
        const timeout = Number(byId('meterTimeout').value);
        let valid = true;
        if (!host) { markInvalid('meterHost', 'Host is required.'); valid = false; }
        if (!Number.isInteger(port) || port < 1 || port > 65535) { markInvalid('meterPort', 'Port must be 1–65535.'); valid = false; }
        if (!Number.isInteger(unitId) || unitId < 1 || unitId > 247) { markInvalid('meterUnit', 'Unit ID must be 1–247.'); valid = false; }
        if (!Number.isInteger(address) || address < 0 || address > 65535) { markInvalid('meterAddress', 'PDU address must be 0–65535.'); valid = false; }
        if (!Number.isFinite(scale) || scale === 0) { markInvalid('meterScale', 'Scale must be a non-zero number.'); valid = false; }
        if (!Number.isFinite(poll) || poll < 100) { markInvalid('meterPoll', 'Polling interval must be at least 100 ms.'); valid = false; }
        if (!Number.isFinite(timeout) || timeout < 100) { markInvalid('meterTimeout', 'Timeout must be at least 100 ms.'); valid = false; }
        if (!valid) throw new Error('Correct the highlighted meter settings.');

        const meter = state.config.meters[0];
        meter.host = host;
        meter.port = port;
        meter.unit_id = unitId;
        meter.active_power_address = address;
        meter.scale = scale;
        meter.poll_ms = poll;
        meter.timeout_ms = timeout;
    }

    function collectControlConfig() {
        if (!state.config || !state.config.control) throw new Error('Control configuration is unavailable.');
        clearValidation();
        const target = Number(byId('controlTarget').value);
        const deadband = Number(byId('controlDeadband').value);
        const interval = Number(byId('controlInterval').value);
        const staleTimeout = Number(byId('controlStaleTimeout').value);
        let valid = true;
        if (!Number.isFinite(target)) { markInvalid('controlTarget', 'Enter a valid target.'); valid = false; }
        if (!Number.isFinite(deadband) || deadband < 0) { markInvalid('controlDeadband', 'Deadband must be zero or greater.'); valid = false; }
        if (!Number.isFinite(interval) || interval < 100) { markInvalid('controlInterval', 'Interval must be at least 100 ms.'); valid = false; }
        if (!Number.isFinite(staleTimeout) || staleTimeout < 500) { markInvalid('controlStaleTimeout', 'Stale timeout must be at least 500 ms.'); valid = false; }
        if (!valid) throw new Error('Correct the highlighted control settings.');

        state.config.control.grid_import_target_kw = target;
        state.config.control.deadband_kw = deadband;
        state.config.control.interval_ms = interval;
        state.config.control.meter_stale_timeout_ms = staleTimeout;
    }

    async function saveConfiguration(messageId) {
        if (state.saving) return null;
        state.saving = true;
        setMessage(messageId, 'Saving configuration…');
        try {
            const result = await api('/api/config', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(state.config)
            });
            setMessage(messageId, 'Saved and verified in flash.', 'good');
            byId('advancedJson').value = JSON.stringify(state.config, null, 2);
            toast('Configuration saved successfully.', 'good');
            return result;
        } finally {
            state.saving = false;
        }
    }

    async function restartController(messageId = 'systemMessage') {
        setMessage(messageId, 'Restarting controller…');
        await api('/api/system/restart', { method: 'POST' });
        setMessage(messageId, 'Restart accepted. Reconnecting…', 'good');
        toast('Controller restart accepted.', 'good');
    }

    async function handleWifiSave(event) {
        event.preventDefault();
        setMessage('wifiMessage', '');
        try {
            collectWifiConfig();
            await saveConfiguration('wifiMessage');
            await restartController('wifiMessage');
        } catch (error) {
            setMessage('wifiMessage', error.message, 'bad');
        }
    }

    async function handleMeterSave() {
        setMessage('meterMessage', '');
        try {
            collectMeterConfig();
            await saveConfiguration('meterMessage');
            setMessage('meterMessage', 'Saved. Restart required to apply endpoint changes.', 'good');
        } catch (error) {
            setMessage('meterMessage', error.message, 'bad');
        }
    }

    async function handleControlSave() {
        setMessage('controlMessage', '');
        try {
            collectControlConfig();
            await saveConfiguration('controlMessage');
            setMessage('controlMessage', 'Parameters saved. Control enable state was not changed.', 'good');
        } catch (error) {
            setMessage('controlMessage', error.message, 'bad');
        }
    }

    async function handleJsonSave() {
        setMessage('systemMessage', '');
        try {
            state.config = JSON.parse(byId('advancedJson').value);
            await saveConfiguration('systemMessage');
            await loadConfig();
        } catch (error) {
            setMessage('systemMessage', `Save failed: ${error.message}`, 'bad');
        }
    }

    async function handleRescan() {
        setMessage('wifiMessage', 'Starting Wi-Fi rescan…');
        try {
            await api('/api/wifi/rescan', { method: 'POST' });
            setMessage('wifiMessage', 'Wi-Fi rescan and reconnect accepted.', 'good');
            toast('Wi-Fi rescan started.', 'good');
        } catch (error) {
            setMessage('wifiMessage', `Rescan failed: ${error.message}`, 'bad');
        }
    }

    function exportConfiguration() {
        if (!state.config) return;
        const blob = new Blob([JSON.stringify(state.config, null, 2)], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const link = document.createElement('a');
        link.href = url;
        link.download = 'automatrix-pvdg-config.json';
        document.body.appendChild(link);
        link.click();
        link.remove();
        URL.revokeObjectURL(url);
    }

    function toast(message, tone = '') {
        const region = byId('toastRegion');
        const node = document.createElement('div');
        node.className = `toast${tone ? ` ${tone}` : ''}`;
        node.textContent = message;
        region.appendChild(node);
        window.setTimeout(() => node.remove(), 4200);
    }

    /* ================================================== lab simulator authority
     *
     * The firmware can have a specific inverter endpoint DECLARED a Modbus
     * simulator. Doing so grants LAB-only command authority through a profile
     * that has not been qualified on physical equipment, and the commissioning
     * gate then reports scope "lab_simulator_only" instead of "production".
     *
     * Three rules hold everywhere in this section.
     *
     *  1. VERBATIM. Where the controller publishes wording - lab_simulator_notice,
     *     scope_notice, control_authority.mode_label, inhibit_reason, a
     *     prerequisite title, a reason code and its sentence - that string is
     *     rendered exactly as received. This interface must not paraphrase a
     *     safety decision it did not make, and must not invent a register
     *     number, an inverter command or a timing value.
     *
     *  2. UNKNOWN IS NOT FINE. Every one of these endpoints requires an
     *     authenticated engineering session. Until one has answered, the state is
     *     null and the interface says it does not know. It never renders
     *     "production", and it never renders a hidden banner as evidence that
     *     the plant is not a simulator.
     *
     *  3. NO GUARANTEED 401s. The controller serves a small client socket pool,
     *     so a request that can only answer 401 is not free. Every read below is
     *     asked for only when the shared scope predicate says it can succeed.
     *
     * KNOWN LIMITATION, deliberately not worked around: no PUBLIC endpoint
     * publishes lab_simulator_mode. /api/status does not carry the commissioning
     * scope. An operator with no engineering session therefore cannot be shown
     * the lab banner at all. Surfacing the scope on /api/status is a firmware
     * change and is not made here. */

    /* The four write-confirmation states, worst last, matching the firmware's own
     * severity order in inverter_write_confirmation.c. Every sentence below
     * describes what the firmware actually does; none of it is inferred. */

    const WRITE_CONFIRMATION_STATES = Object.freeze({
        confirmed: Object.freeze({
            label: 'Confirmed',
            mark: '✓',
            /* Corrected copy. This used to state a setpoint readback as the sole
             * meaning of confirmed. Since plant-level logger control landed that
             * is only one of two kinds of evidence, and it is the weaker one: on a
             * stored-command interface the readback is an echo and proves
             * acceptance, not a limit. Confirmed is never shown on this panel
             * without the limit evidence beside it. */
            meaning: 'The controller records a confirmed value here, but confirmed is not one thing. It rests either on measured output that was above the new limit before the command and at or below it after, which demonstrates the limit, or on a setpoint readback that matched, which on a plant-level logger is an echo of a stored command and proves only that the command was accepted. The limit evidence shown beside this state says which of the two it was.'
        }),
        pending: Object.freeze({
            label: 'Pending',
            mark: '⋯',
            meaning: 'The write was accepted by the transport and no readback has confirmed it yet. This is not success. A real inverter can take longer than a second to apply a setpoint, so the controller waits rather than claiming the value took effect.'
        }),
        unverified: Object.freeze({
            label: 'Unverified',
            mark: '?',
            meaning: 'Confirmation is impossible or has failed to arrive: the assigned profile carries no manual-verified readback register, the write did not reach the device, or the confirmation deadline passed with no usable post-write sample. A fourth cause reaches this state too: output at or below the limit that was already at or below it before the command, which proves nothing either way. The limit evidence beside this state says which. This is neither success nor failure.'
        }),
        mismatched: Object.freeze({
            label: 'Mismatched',
            mark: '!',
            meaning: 'A readback taken after the write disagreed with the requested setpoint. The controller treats this as a fault: it drives the inverter to its safe fallback and keeps the fault latched until a readback confirms that safe value.'
        })
    });


    /* LIMIT EVIDENCE, as a THIRD and separate vocabulary.
     *
     * The four states above say WHETHER a write was confirmed. They do not say
     * what confirmed it, and since plant-level logger control landed there are
     * two kinds of evidence behind the same word:
     *
     *   measured_power      measured output was ABOVE the new limit before the
     *                       command and at or below it after. The limit is
     *                       demonstrated. No change in irradiance can lift a
     *                       plant above a limit that is in force, so the
     *                       opposite direction is unambiguous too.
     *   setpoint_readback   the setpoint register read back matching. On some
     *                       devices that is an applied value; on the Huawei
     *                       SmartLogger plant interface the register STORES the
     *                       command and forwards it, so the readback is an echo
     *                       and proves acceptance only. The logger may also
     *                       apply an adjustment coefficient that has no register
     *                       at all, so a commanded 80 % need not deliver 80 %.
     *   ambiguous_headroom  output is at or below the limit but was ALREADY at
     *                       or below it, or there is no usable pre-command
     *                       baseline. Equally consistent with the limit being
     *                       honoured and with the sun going in. Neither success
     *                       nor failure: nothing was proven.
     *
     * Its own labels and its own glyphs, shared with neither of the other two
     * vocabularies, because it is a third independent answer. Every sentence
     * states what the firmware does; none of it is inferred, and none of it
     * upgrades the echo case into something stronger than it is. */

    const WRITE_PROOF_STATES = Object.freeze({
        measured_power: Object.freeze({
            label: 'Limit demonstrated by measurement',
            mark: '⤓',
            demonstrated: true,
            meaning: 'Measured output was above the new limit before the command and at or below it after. That is the only evidence this controller accepts as demonstrating a limit, because falling irradiance cannot explain it: the plant was generating above the limit and then was not. This is the strongest statement available about a power limit.'
        }),
        setpoint_readback: Object.freeze({
            label: 'Setpoint echo only',
            mark: '↩',
            demonstrated: false,
            meaning: 'The setpoint register read back matching the value written. On some devices that is the value actually applied. On a plant-level logger the register stores the command and forwards it, so reading it back returns the stored command rather than the plant state, and the logger may scale it by an adjustment coefficient that has no register at all. This proves the command was accepted. It does not show that the limit is in force, and nothing here should be read as if it did.'
        }),
        ambiguous_headroom: Object.freeze({
            label: 'Below limit, nothing proven',
            mark: '≈',
            demonstrated: false,
            meaning: 'Output is at or below the commanded limit, but it was already at or below it before the command was sent, or no usable pre-command baseline exists. That is exactly as consistent with the limit being honoured as it is with the sun going in, so nothing has been proven either way. This is neither success nor failure and it is not a fault: the controller deliberately does not drive the plant to zero over it, because that would happen every time irradiance fell below the commanded limit. It keeps measuring instead. If output later rises above the limit the verdict becomes Mismatched and the safe fallback is demanded then. Until a sample is taken while the plant is generating above a newly commanded limit, treat the limit as not shown to be in force.'
        }),
        none: Object.freeze({
            label: 'No evidence recorded',
            mark: '·',
            demonstrated: false,
            meaning: 'The controller has recorded no evidence for or against this command. That is the state before a write has been issued, and it is also the honest answer when neither a qualified setpoint readback nor a measured quantity could be obtained. It claims nothing.'
        })
    });

    function writeProofMeta(name) {
        return WRITE_PROOF_STATES[String(name || '').trim()] || null;
    }

    /* The prerequisite enable register, as a SECOND and separate vocabulary.
     *
     * It is not folded into the four states above, and none of its labels or
     * glyphs are reused from them, because the two answers are independent and
     * the dangerous combination is precisely "setpoint Confirmed" next to "enable
     * not confirmed": three of the four documented brands ignore the active-power
     * setpoint until a separate register holds the value their manual specifies,
     * and the setpoint register accepts and echoes the write anyway. A reader who
     * saw one merged severity would have no way to tell a setpoint that read back
     * wrong from a setpoint that read back perfectly and was ignored.
     *
     * Every sentence below states what the firmware actually does. Unconfirmed
     * and unverifiable are kept apart because they demand different actions: the
     * first resolves itself, the second needs a human with a manual. */

    const PREREQUISITE_STATES = Object.freeze({
        confirmed: Object.freeze({
            label: 'Enable confirmed',
            mark: '▣',
            transient: false,
            meaning: 'A read of the enable register found the value the manufacturer manual specifies. Only a read produces this state. An accepted write never does, because a write proves only that the transport took it.'
        }),
        unconfirmed: Object.freeze({
            label: 'Enable not confirmed',
            mark: '▢',
            transient: true,
            meaning: 'The enable register is not confirmed to hold. A setpoint state of Confirmed above does not contradict this: the setpoint register accepts the write and echoes it back whether or not the limit is armed, so the readback matches while the inverter keeps generating at full output. This is transient. The controller keeps reading and rewriting the register, and the inverter stays out of the commandable fleet until a read confirms it.'
        }),
        unverifiable: Object.freeze({
            label: 'Enable unverifiable',
            mark: '⨯',
            transient: false,
            meaning: 'This device needs an enable register and its assigned profile cannot describe one that is both writable and readable. That is permanent: no amount of polling will resolve it, and the inverter is refused write authority outright rather than retried. The remedy is a manual citation for the register address and its readback, not more time.'
        }),
        not_required: Object.freeze({
            label: 'No enable register',
            mark: '–',
            transient: false,
            meaning: 'The assigned profile states that this device honours an active-power setpoint with no separate enable register, so there is nothing to arm and the setpoint states above stand on their own.'
        })
    });


    /* Percent values arrive as null whenever the controller has nothing to
     * report. Null must stay null: rendering 0% for "no write has been issued"
     * would state a setpoint the device never received. */
    function formatPercent(value, absent = 'Not reported') {
        const number = Number(value);
        return value == null || !Number.isFinite(number) ? absent : `${number.toFixed(1)} %`;
    }

    /* Deliberately NOT formatPower() above, which runs its argument through
     * Number() and so turns a null into "0.00 kW". A measurement the controller
     * did not report must never be rendered as zero output: zero output would
     * itself look like a limit being obeyed. Null stays absent, exactly as in
     * formatPercent. */

    /* Writes only when the value actually changed. The lab banner is role="alert",
     * so rewriting identical text on every poll would re-announce it to a screen
     * reader several times a minute. */
    /*
     * An em dash is the convention for A VALUE that is not available, and it is
     * the right answer in a labelled slot. A whole counts strip with no counts
     * is a different thing: a lone "--" there reads as one of the counts, so the
     * strip is emptied rather than filled with a marker.
     */

    function setTextIfChanged(id, value) {
        const node = byId(id);
        if (!node) return;
        const text = value == null || value === '' ? '--' : String(value);
        if (node.textContent !== text) node.textContent = text;
    }

    /* DISCLOSURE.
     *
     * The Inverters page carried roughly ten screens of permanently visible
     * prose. Every sentence of it is true and most of it is load-bearing - it is
     * why this product refuses to command equipment on insufficient evidence -
     * but an engineer standing at a plant needs two things by default: the
     * verdict, and, if the verdict is not qualified, what evidence is missing.
     * The reasoning behind the verdict is a level down, never deleted, and
     * always reachable from the finding it explains.
     *
     * One helper so the treatment is identical everywhere and so a reader who
     * has learned to open one drawer has learned to open all of them. */
    function disclosure(summaryText, ...nodes) {
        const details = document.createElement('details');
        details.className = 'page-drawer';
        const summary = document.createElement('summary');
        summary.textContent = summaryText;
        details.append(summary, ...nodes.filter(Boolean));
        return details;
    }

    /* Same reasoning as setTextIfChanged: these badges are refreshed on the 2 s
     * status poll, and web/product-mode.js watches #mainContent for childList
     * changes, so writing an unchanged label is a DOM mutation for nothing. */
    function setBadgeIfChanged(id, label, tone) {
        const node = byId(id);
        if (!node) return;
        if (node.textContent !== label) node.textContent = label;
        const className = `subtle-badge${tone ? ` ${tone}` : ''}`;
        if (node.className !== className) node.className = className;
    }

    /* setMessage() rewrites className outright, which would drop the panel's own
     * message class. These panels keep their base class and add only the tone. */
    function setStateMessage(id, baseClass, message, tone) {
        const node = byId(id);
        if (!node) return;
        const text = message || '';
        if (node.textContent !== text) node.textContent = text;
        const className = `${baseClass}${tone ? ` ${tone}` : ''}`;
        if (node.className !== className) node.className = className;
    }

    function setNoticeLine(id, value) {
        const node = byId(id);
        if (!node) return;
        const text = typeof value === 'string' ? value.trim() : '';
        if (node.textContent !== text) node.textContent = text;
        if (node.hidden !== !text) node.hidden = !text;
    }

    /* ------------------------------------------------------------- the banner */

    /*
     * THE LAB-SIMULATOR BANNER IS GONE.
     *
     * It existed to make one thing impossible to miss: that commands were going
     * to a declared Modbus simulator rather than to a plant. There is no such
     * declaration any more -- the controller is deployed on a site, everything
     * on the wire is real equipment, and the firmware grants no authority on the
     * strength of a flag. A banner that can never fire is furniture, and one
     * that could fire falsely would be worse.
     */

    /* ------------------------------------------------- the commissioning gate */

    function gateItemElement(item) {
        const satisfied = item.satisfied === true;
        const row = document.createElement('li');
        row.className = `gate-item ${satisfied ? 'met' : 'unmet'}`;
        row.dataset.prereq = String(item.id || '');

        const mark = document.createElement('span');
        mark.className = 'gate-item-mark';
        mark.setAttribute('aria-hidden', 'true');
        mark.textContent = satisfied ? '✓' : '!';

        const title = document.createElement('span');
        title.className = 'gate-item-title';
        title.textContent = verbatim(item.title, 'Unnamed prerequisite');

        const word = document.createElement('span');
        word.className = 'gate-item-state';
        /* Workflow vocabulary: an unmet prerequisite is outstanding commissioning
         * work, not a fault that can be acknowledged. */
        word.textContent = satisfied ? STATES.workflow.complete : STATES.workflow.blocked;

        row.append(mark, title, word);

        /* The firmware's explanatory sentence, and its machine reason code so a
         * report or a support call can quote something stable. Both are shown
         * only for an unmet prerequisite: the satisfied sentence is empty. */
        const detailText = typeof item.detail === 'string' ? item.detail.trim() : '';
        if (!satisfied && detailText) {
            const detail = document.createElement('span');
            detail.className = 'gate-item-detail';
            detail.textContent = detailText;
            row.append(detail);
        }
        if (!satisfied) {
            const reason = document.createElement('span');
            reason.className = 'gate-item-reason';
            reason.textContent = `Reason code: ${verbatim(item.reason, 'unknown')}`;
            row.append(reason);
        }
        return row;
    }

    function renderCommissioningGate() {
        const list = byId('gateChecklist');
        if (!list) return;
        const gate = state.commissioningGate;
        const authority = state.status?.control_authority || null;

        /* control_authority is published by the PUBLIC /api/status, so these two
         * rows are populated whether or not an engineering session exists, and
         * both are the controller's own strings. */
        setTextIfChanged('gateModeLabel', verbatim(authority?.mode_label));
        setTextIfChanged('gateInhibitReason', verbatim(authority?.inhibit_reason, 'None reported'));
        /* Called before the early return below, so the two fault rows are cleared
         * to Unavailable rather than left showing the last good answer when the
         * gate read fails. */
        renderGatePrerequisite(gate);
        /* Also before the early return, so the evidence row clears to Unavailable
         * rather than keeping the last good answer when the gate read fails. */
        renderGateLimitEvidence(gate);

        if (!gate) {
            setTextIfChanged('gateScope', STATES.dataQuality.unavailable);
            setTextIfChanged('gateSatisfied', STATES.dataQuality.unavailable);
            setBadgeIfChanged('gateScopeBadge', STATES.dataQuality.unavailable, '');
            if (state.gateSignature !== '') {
                state.gateSignature = '';
                list.replaceChildren();
            }
            return;
        }

        setTextIfChanged('gateScope', verbatim(gate.scope));
        const total = Number(gate.prerequisite_count);
        const satisfied = Number(gate.satisfied_count);
        setTextIfChanged('gateSatisfied',
            Number.isFinite(total) && Number.isFinite(satisfied)
                ? `${satisfied} of ${total}` : STATES.dataQuality.unavailable);

        /* Commissioned and production-qualified are different answers, and the
         * badge must not collapse them. A lab-only gate is Configured, not
         * Qualified. */
        if (gate.production_qualified === true) {
            setBadgeIfChanged('gateScopeBadge', STATES.commissioning.qualified, 'good');
        } else if (gate.lab_simulator_mode === true) {
            setBadgeIfChanged('gateScopeBadge', STATES.commissioning.configured, 'warning');
        } else if (gate.commissioned === true) {
            setBadgeIfChanged('gateScopeBadge', STATES.commissioning.configured, 'warning');
        } else {
            setBadgeIfChanged('gateScopeBadge', STATES.commissioning.notConfigured, 'bad');
        }

        /* The gate summary is the firmware's own sentence and is empty once the
         * gate is satisfied. Nothing is substituted for it. */
        const summary = typeof gate.summary === 'string' ? gate.summary.trim() : '';
        setTextIfChanged('gateSummary', summary || (gate.commissioned === true
            ? 'Every prerequisite is satisfied. Read the commissioning scope above for what that authorises.'
            : STATES.dataQuality.unavailable));

        const items = Array.isArray(gate.prerequisites) ? gate.prerequisites : [];
        const signature = JSON.stringify(items);
        if (state.gateSignature !== signature) {
            state.gateSignature = signature;
            list.replaceChildren(...items.map(gateItemElement));
        }
    }

    /* The two faults, as two rows, on the panel that explains why automatic
     * control is inhibited.
     *
     * They are reported separately because they are indistinguishable in their
     * effect and unrelated in their remedy. A setpoint fault means the setpoint
     * register read back the wrong value. A prerequisite fault produces no
     * setpoint fault at all: the setpoint is accepted, echoed back and ignored,
     * so it reads as confirmed while the inverter runs unlimited. An engineer
     * shown only the first would go and check a register that is working. */
    function renderGatePrerequisite(gate) {
        if (!gate) {
            setTextIfChanged('gateWriteConfirmation', STATES.dataQuality.unavailable);
            setTextIfChanged('gatePrerequisite', STATES.dataQuality.unavailable);
            setTextIfChanged('gatePrereqCounts',
                'The controller has not reported an enable-register state. Unknown is not confirmed.');
            setNoticeLine('gatePrereqDetail', '');
            return;
        }
        setTextIfChanged('gateWriteConfirmation', gate.write_confirmation_fault === true
            ? 'Faulted: a setpoint did not read back the commanded value'
            : gate.write_confirmation_fault === false
                ? 'No setpoint fault latched'
                : STATES.dataQuality.unavailable);

        const unverifiable = Number(gate.prerequisite_unverifiable_count);
        setTextIfChanged('gatePrerequisite', gate.prerequisite_enable_fault === true
            ? (Number.isFinite(unverifiable) && unverifiable > 0
                ? 'Unverifiable: an enable register cannot be read back at all (permanent)'
                : 'Not confirmed: a setpoint would read back correctly and be ignored')
            : gate.prerequisite_enable_fault === false
                ? 'Every required enable register is confirmed by a read'
                : STATES.dataQuality.unavailable);

        setTextIfChanged('gatePrereqCounts',
            prerequisiteCountLine(gate) || STATES.dataQuality.unavailable);
        /* The controller's own sentence, verbatim. */
        setNoticeLine('gatePrereqDetail', gate.prerequisite_notice);
    }

    /* The gate panel says whether automatic control is permitted. The row below
     * says what the setpoint confirmations behind that permission actually rest
     * on, because "no setpoint fault latched" is not the same claim as "a limit
     * was demonstrated" and an engineer reading the first as the second has been
     * told a plant is limited when only a stored command was echoed back. */
    function renderGateLimitEvidence(gate) {
        if (!gate) {
            setTextIfChanged('gateLimitEvidence', STATES.dataQuality.unavailable);
            return;
        }
        const proofMeta = writeProofMeta(String(gate.write_proof || '').trim());
        const written = Number(gate.written_count);
        if (Number.isFinite(written) && written === 0) {
            setTextIfChanged('gateLimitEvidence',
                'No write has been issued, so no limit has been demonstrated');
            return;
        }
        /* Explicit === true. A missing field must not read as a demonstrated limit. */
        if (gate.limit_demonstrated === true) {
            setTextIfChanged('gateLimitEvidence',
                'Demonstrated by measurement: output was above the new limit before the command and at or below it after');
            return;
        }
        if (gate.setpoint_echo_only === true) {
            setTextIfChanged('gateLimitEvidence',
                'Setpoint echo only: the command was accepted, and the limit has not been shown to be in force');
            return;
        }
        setTextIfChanged('gateLimitEvidence', proofMeta
            ? `Not demonstrated. Weakest evidence held: ${proofMeta.label}`
            : STATES.dataQuality.unavailable);
    }

    /* ------------------------------------------------ setpoint write confirmation */


    /* The limit-evidence pill. Built like the state pill so the two read as
     * peers, and rendered IMMEDIATELY BESIDE it wherever a verdict appears, so
     * "Confirmed" can never be on screen without what confirmed it.
     *
     * An unrecognised slug falls back to the 'none' TREATMENT, which claims
     * nothing. Falling back to the measured treatment would invent a demonstrated
     * limit out of a value this build cannot interpret. */

    /* Built the same way as the setpoint pill so the two read as peers, with its
     * own word and its own glyph so they can never be mistaken for one another.
     * An unrecognised slug falls back to the unconfirmed TREATMENT and is labelled
     * with whatever the controller sent, which is the fail-closed choice: an
     * enable register this build cannot interpret has not been confirmed. */


    /* WHAT IS MISSING, IN ONE LINE.
     *
     * An engineer reading a row that is not qualified should not have to infer
     * the cause from a wall of explanation. This names the single thing standing
     * between this row and a demonstrated limit, in the order that matters: an
     * unarmed enable register outranks everything below it, because it means the
     * setpoint is being ignored no matter how well it reads back.
     *
     * It claims nothing the verdict does not already claim - it only says the
     * same thing shorter and first. */

    /* The full meaning of the setpoint state, for the drawer. Called only from
     * inside disclosure(). */



    /* WHAT CONFIRMED IT, per inverter, immediately under the setpoint figures the
     * verdict was drawn from.
     *
     * Rendered for every inverter, including the ones with nothing to show, for
     * the same reason the enable-register block is: an absent block cannot be
     * told apart from a controller that did not report, and a verdict with no
     * stated evidence is the defect. */

    /* The enable register, per inverter, immediately under the setpoint figures it
     * silently invalidates. Rendered for every inverter including the ones that
     * need no enable register, because "this model needs none" is itself an answer
     * an engineer needs, and an absent block would be indistinguishable from a
     * controller that did not report. */

    /* A legend is built as a dense row of the pills themselves plus ONE drawer
     * holding all four definitions. The vocabulary stays on screen - a reader
     * scanning rows needs to recognise the pills - while the paragraph defining
     * each one is a level down. Three legends of four definitions each was over
     * six hundred words of permanently visible text explaining a vocabulary most
     * readers of this page already know. */
    /* Built only to fill a drawer, and called only from inside disclosure(), so
     * that "this prose is not in the default view" is a property of the code
     * rather than a claim about it. */


    /*
     * A LEGEND IS DRAWN ONLY WHEN THE THING IT EXPLAINS IS ON SCREEN.
     *
     * This panel used to draw three of them permanently: twelve symbols across
     * three rows, above an empty table, three "Unavailable" counts and two
     * paragraphs about unknown evidence. A vocabulary for a table that is not
     * there teaches a reader to skip the panel, which is where the one real
     * finding would appear.
     *
     * The sentences themselves are untouched and still one click away. Nothing
     * an engineer can act on is hidden by this: a legend explains symbols, and
     * with no symbols on screen it explains nothing.
     */

    /* All four kinds of limit evidence explained once, next to the four setpoint
     * states, so a reader can see that they are two independent answers rather
     * than one scale. Built once; these sentences never change. */

    /* The fleet evidence counts. Demonstrated, echo-only and ambiguous are three
     * separate figures and are never summed: adding a stored-command echo to a
     * demonstrated limit produces a number that claims more than the evidence
     * supports, which is the whole defect. */

    /* Fleet limit evidence, stated alongside the fleet verdict and never instead
     * of it. The verdict says whether the writes were confirmed; this says what
     * confirmed them, and the two together are the only honest reading. */

    /* Fleet roll-up for the enable register, stated alongside the fleet setpoint
     * state rather than instead of it. The three counts are never summed: the
     * unverifiable ones are the only ones that will not resolve on their own, so
     * merging them into a single "problem" figure would hide the only number that
     * needs a person. */
    function prerequisiteCountLine(payload) {
        const required = Number(payload?.prerequisite_required_count);
        const unconfirmed = Number(payload?.prerequisite_unconfirmed_count);
        const unverifiable = Number(payload?.prerequisite_unverifiable_count);
        if (![required, unconfirmed, unverifiable].every(Number.isFinite)) return null;
        return `Need an enable register: ${required}`
            + ` · Not confirmed right now (transient, retried): ${unconfirmed}`
            + ` · Unverifiable (permanent, needs a manual citation): ${unverifiable}`;
    }

    /* All four enable-register states are explained once, next to the four
     * setpoint states, so an engineer can see that they are two independent
     * answers rather than one scale. Built once; these sentences never change. */



    /* ------------------------------------------------------- lab target control */

    /* ONE CONTROL PER FACT.
     *
     * Which register map a channel uses is decided once, in the profile
     * catalogue, and is read back here from the controller's own
     * inverter_assignments. This panel used to carry its own "Profile to assign"
     * select, so two controls on the same page could name two different profiles
     * for the same channel and whichever was pressed last silently won. The
     * declaration - whether the endpoint is a simulator - is this panel's own
     * fact and stays editable here. */



    /* Displays the assignment the controller holds for the selected channel.
     * The profile's own qualification word stays part of the line: choosing a
     * simulator-only profile and declaring the endpoint a simulator are two
     * separate decisions and both must be visible at the moment of declaring. */




    /* --------------------------------------------------------------- the reads */

    function engineeringAuthorized() {
        const access = window.AutomatrixEngineeringAccess;
        return Boolean(access && access.isAuthenticated());
    }

    function gateAccessNote() {
        return engineeringAuthorized()
            ? ''
            : 'The commissioning gate and the setpoint confirmation table require an authenticated engineering session. Until one exists this controller has not told this browser whether its commanded inverters are real equipment or declared simulators, so no scope is shown above.';
    }

    async function refreshCommissioningGate() {
        /* Wanted on every route, because the lab banner lives in the shell. Gated
         * on the session only: the endpoint is engineering-guarded and answering
         * 401 on every route would be exactly the socket waste the operator
         * screens were cleaned up to avoid. */
        if (!engineeringAuthorized()) {
            state.commissioningGate = null;
            setStateMessage('gateMessage', 'gate-message', gateAccessNote(), '');
            renderCommissioningGate();
            return;
        }
        try {
            state.commissioningGate = await api('/api/commissioning/gate');
            setStateMessage('gateMessage', 'gate-message', '', '');
        } catch (error) {
            state.commissioningGate = null;
            setStateMessage('gateMessage', 'gate-message', error.status === 401
                ? 'The engineering session ended. Sign in again to read the commissioning gate.'
                : `The commissioning gate could not be read: ${error.message}`, 'bad');
        }
        renderCommissioningGate();
    }

    async function refreshSolarGridStatus() {
        /* Engineering-scoped to the control and commissioning routes by the
         * shared table in web/product-mode.js. Asked for only where it is
         * permitted; its lab_simulator_notice is additional to the gate's
         * scope_notice, never a replacement for it. */
        const access = window.AutomatrixEngineeringAccess;
        if (!access || !access.mayRequest('/api/solar-grid/status')) {
            state.solarGridStatus = null;
            return;
        }
        try {
            state.solarGridStatus = await api('/api/solar-grid/status');
        } catch (error) {
            state.solarGridStatus = null;
        }
    }



    /* Renders the declarations the controller holds.
     * Deliberately not what this browser last sent: a panel that echoes its own
     * last request cannot tell an operator whether commands are reaching a
     * simulator or a plant, which is the one thing here that must not be
     * guessed. */

    /*
     * The lab-target and setpoint-confirmation panels are gone from the
     * inverter page, so nothing polls their endpoints any more. The commissioning
     * gate and the Solar-Grid status stay: the lab BANNER in the shell reads
     * them, and it is what tells an engineer on every page that a commanded
     * inverter is a declared simulator.
     *
     * The firmware is untouched. /api/inverters/write-confirmation and the
     * lab-target endpoints still exist and still answer; the controller still
     * refuses to command an unqualified profile. What is removed is the display,
     * not the gate.
     */
    function refreshLabControl() {
        refreshCommissioningGate();
        refreshSolarGridStatus();
    }

    function bindEvents() {
        window.addEventListener('hashchange', navigate);
        byId('menuButton').addEventListener('click', () => document.body.classList.contains('menu-open') ? closeMenu() : openMenu());
        document.addEventListener('click', (event) => {
            if (document.body.classList.contains('menu-open') && !event.target.closest('.sidebar') && !event.target.closest('#menuButton')) closeMenu();
        });
        byId('refreshButton').addEventListener('click', refreshStatus);
        byId('primaryMode').addEventListener('change', () => updateStaticFieldState('primary'));
        byId('fallbackMode').addEventListener('change', () => updateStaticFieldState('fallback'));
        byId('wifiForm').addEventListener('submit', handleWifiSave);
        byId('wifiRescanButton').addEventListener('click', handleRescan);
        byId('meterSaveButton').addEventListener('click', handleMeterSave);
        byId('controlSaveButton').addEventListener('click', handleControlSave);
        byId('saveJsonButton').addEventListener('click', handleJsonSave);
        byId('reloadConfigButton').addEventListener('click', async () => {
            setMessage('systemMessage', 'Reloading…');
            try { await loadConfig(); setMessage('systemMessage', 'Configuration reloaded.', 'good'); }
            catch (error) { setMessage('systemMessage', error.message, 'bad'); }
        });
        byId('exportButton').addEventListener('click', exportConfiguration);
        byId('restartButton').addEventListener('click', async () => {
            if (!window.confirm('Restart the controller now?')) return;
            try { await restartController(); }
            catch (error) { setMessage('systemMessage', error.message, 'bad'); }
        });
    }

    /* Published before start() so every later module in the bundle - the access
     * layer, the operator views, the alarm centre - reads one route table, one
     * state vocabulary and one verbatim rule instead of keeping its own. */
    window.AutomatrixUi = Object.freeze({
        ROUTES,
        NAV_GROUPS,
        STATES,
        routeName,
        routeShortName,
        verbatim,
        applyRouteChrome,
        ensureNavigationHierarchy
    });

    async function start() {
        bindEvents();
        bindSiteTelemetry();
        watchNavigation();
        if (!window.location.hash) window.location.hash = '#/dashboard';
        navigate();
        /* Pages injected after this script runs (alarms, readiness,
         * commissioning, engineering) arrive inactive; the router selects them
         * when the shared observer reports the addition. */
        window.AutomatrixEngineeringAccess?.onContentChange(applyRoute);
        await Promise.allSettled([loadConfig(), refreshStatus()]);
        /*
         * RELOAD WHEN THE FIRMWARE CHANGES UNDER US.
         *
         * The browser kept running the JavaScript it had already loaded across
         * firmware updates, so a fix could be flashed, verified on the wire, and
         * remain invisible on screen. That cost real time on this project more
         * than once, and no amount of care in the fix itself prevents it.
         *
         * One reload, on the first status that reports a different build. Guarded
         * by a flag so a controller that reboots into a different image cannot
         * put the page into a reload loop.
         */
        let loadedFirmware = null;
        let reloading = false;
        window.setInterval(async () => {
            await refreshStatus();
            const running = state.status?.firmware_version;
            if (!running) return;
            if (loadedFirmware === null) { loadedFirmware = running; return; }
            if (running !== loadedFirmware && !reloading) {
                reloading = true;
                location.reload();
            }
        }, 2000);
        window.AutomatrixEngineeringAccess?.onScopeChange(refreshSourceDetection);
        refreshSourceDetection();
        /*
         * AND KEEP ASKING.
         *
         * This ran once at start-up and once whenever the engineering scope
         * changed, and never again -- so the "Active power source" panel on the
         * plant overview went on reporting whatever the source had been when the
         * page was opened. Observed on the plant: the site changed over to the
         * generator and the panel still read GRID, Tariff 1, four minutes later.
         *
         * On the one panel whose entire question is "which supply is carrying
         * the plant", a value frozen at page-load is not stale information, it
         * is a wrong answer stated confidently.
         *
         * Ten seconds, matching the commissioning-gate poll below rather than
         * the 2 s status poll: this endpoint is engineering-gated and the
         * controller's client socket pool is small, and a changeover is a thing
         * that happens over seconds, not milliseconds.
         */
        window.setInterval(refreshSourceDetection, 10000);
        /* Signing in, signing out and changing route are the three events that
         * change what these reads may ask for, so all three re-run them. */
        window.AutomatrixEngineeringAccess?.onScopeChange(refreshLabControl);
        refreshLabControl();
        /* Deliberately slower than the 2 s status poll. A pending setpoint is
         * resolved in seconds, not milliseconds, and the controller's client
         * socket pool is small; polling these three engineering endpoints hard
         * would cost an operator their own page. */
        window.setInterval(refreshCommissioningGate, 10000);
        window.setInterval(refreshSolarGridStatus, 10000);
        /* The 5-second setpoint-confirmation poll is gone with its panel. It was
         * the most frequent engineering request this page made, on a controller
         * whose client socket pool is small, and nothing displays its answer
         * any more. */
    }

    start().catch((error) => {
        renderControllerUnavailable();
        toast(`Application startup failed: ${error.message}`, 'bad');
    });
})();
