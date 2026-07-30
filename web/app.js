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
        dashboard: { name: 'Plant overview', group: 'operate', icon: '⌂' },
        meters: { name: 'Grid power', group: 'operate', icon: '▤' },
        inverters: { name: 'Solar inverters', group: 'operate', icon: '◇' },
        alarms: { name: 'Alarms and events', group: 'operate', icon: '△' },
        commissioning: { name: 'Commissioning', group: 'commission', icon: '✓' },
        readiness: { name: 'Pre-lab readiness', group: 'commission', icon: '⌾' },
        wifi: { name: 'Network setup', group: 'commission', icon: '⌁' },
        control: { name: 'PV-DG control', group: 'maintain', icon: '⇄' },
        system: { name: 'Controller', group: 'maintain', icon: '⚙' },
        engineering: { name: 'Engineering access', group: 'access', icon: '▣' }
    };

    /* Page type drives the maximum measure. Operational screens are read across
     * and want the width; a form or a guided workflow is read down one column
     * and gets worse, not better, when the label ends up on one side of a
     * monitor and its field on the other. See the density block in
     * web/product-experience-v2.css. */
    const PAGE_TYPES = {
        dashboard: 'operational', meters: 'operational', inverters: 'operational',
        alarms: 'operational',
        wifi: 'form', control: 'form', system: 'form',
        commissioning: 'guided', readiness: 'guided', engineering: 'guided'
    };

    const ROUTE_ORDER = Object.keys(ROUTES);

    function routeName(route) {
        const meta = ROUTES[route];
        return meta ? meta.name : 'Controller';
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
        refreshing: false,
        saving: false,
        lastUpdatedAt: null,
        /* Lab-simulator / commissioning-gate / write-confirmation reads. Each is
         * null until the corresponding endpoint has actually answered, so
         * "unknown" is never rendered as "fine". */
        commissioningGate: null,
        solarGridStatus: null,
        writeConfirmation: null,
        labProfiles: [],
        labTargetSending: false,
        /* Rebuild guards. web/product-mode.js keeps an unguarded MutationObserver
         * on #mainContent, so replacing these lists on every poll would re-run
         * its DOM enforcement pass several times a minute for no change at all. */
        gateSignature: '',
        confirmSignature: ''
    };

    const byId = (id) => document.getElementById(id);
    const all = (selector) => Array.from(document.querySelectorAll(selector));

    function setText(id, value) {
        const node = byId(id);
        if (node) node.textContent = value == null || value === '' ? '--' : String(value);
    }

    function setTone(id, tone) {
        const node = byId(id);
        if (!node) return;
        node.classList.remove('good-text', 'warning-text', 'bad-text', 'muted-text');
        if (tone) node.classList.add(`${tone}-text`);
    }

    function setDot(id, tone) {
        const node = byId(id);
        if (!node) return;
        node.className = `dot${tone ? ` ${tone}` : ''}`;
    }

    function setPill(id, label, tone) {
        const node = byId(id);
        if (!node) return;
        node.textContent = label;
        node.className = `live-pill ${tone || 'neutral'}`;
    }

    function setBadge(id, label, tone) {
        const node = byId(id);
        if (!node) return;
        node.textContent = label;
        node.className = `subtle-badge${tone ? ` ${tone}` : ''}`;
    }

    function setMessage(id, message, tone) {
        const node = byId(id);
        if (!node) return;
        node.textContent = message || '';
        node.className = `action-message${tone ? ` ${tone}` : ''}`;
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
        document.title = `${name} · Automatrix PV-DG`;
        all('.nav-link').forEach((link) => link.classList.toggle('active', link.dataset.route === route));
        const pageType = PAGE_TYPES[route] || 'operational';
        if (document.body && document.body.dataset.pageType !== pageType) {
            document.body.dataset.pageType = pageType;
        }
    }

    function navigate() {
        state.route = routeFromHash();
        all('.page').forEach((page) => page.classList.toggle('active', page.dataset.page === state.route));
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
            return;
        }
        try {
            state.sourceDetection = await api('/api/source-detection');
        } catch (error) {
            state.sourceDetection = null;
        }
        renderPowerFlow();
    }

    function renderControllerUnavailable() {
        setPill('controllerPill', 'Controller unavailable', 'bad');
        setText('statusController', 'Offline');
        setText('statusNetwork', 'Unavailable');
        setText('statusMeter', 'Unavailable');
        setText('statusUpdated', 'Refresh failed');
        setText('sidebarState', 'Controller unavailable');
        setTone('statusController', 'bad');
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

        if (meterFresh) {
            setText('gridPowerValue', formatPower(status.grid_power_kw));
            const descriptor = gridDescriptor(status.grid_power_kw);
            setText('gridPowerDetail', descriptor.detail);
            setDot('gridDot', Number(status.grid_power_kw) > 0 ? 'warning' : 'good');
            setText('meterHealthValue', 'Online');
            setText('meterHealthDetail', `Fresh sample · ${formatAge(status.meter_age_ms)}`);
            setDot('meterDot', 'good');
        } else if (meterStale) {
            setText('gridPowerValue', `${formatPower(status.grid_power_kw)} stale`);
            setText('gridPowerDetail', 'Last valid sample retained; not current');
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
    }

    async function refreshStatus() {
        if (state.refreshing) return;
        state.refreshing = true;
        try {
            state.status = await api('/api/status');
            state.lastUpdatedAt = new Date();
            renderStatus();
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

    function renderInverters(inverters) {
        const container = byId('inverterList');
        container.replaceChildren();
        if (!Array.isArray(inverters) || inverters.length === 0) {
            const empty = document.createElement('div');
            empty.className = 'empty-state';
            empty.textContent = 'No inverter profiles are configured.';
            container.appendChild(empty);
            return;
        }

        inverters.forEach((inverter, index) => {
            const card = document.createElement('article');
            card.className = 'asset-card';
            const title = document.createElement('h3');
            title.textContent = inverter.name || `Inverter ${index + 1}`;
            const badge = document.createElement('span');
            badge.className = 'asset-state';
            badge.textContent = inverter.enabled ? 'Configured and enabled' : 'Disabled';
            const list = document.createElement('dl');
            const rows = [
                ['Endpoint', `${inverter.host || '--'}:${inverter.port || '--'}`],
                ['Unit ID', inverter.unit_id ?? '--'],
                ['Rated power', Number.isFinite(Number(inverter.rated_kw)) ? `${Number(inverter.rated_kw).toFixed(1)} kW` : '--'],
                ['Limit PDU address', inverter.limit_address ?? '--'],
                ['Write function', inverter.limit_function ?? '--'],
                ['Raw units / %', inverter.raw_units_per_percent ?? '--'],
                ['Allowed range', `${inverter.min_percent ?? '--'}–${inverter.max_percent ?? '--'}%`]
            ];
            rows.forEach(([term, value]) => {
                const dt = document.createElement('dt');
                const dd = document.createElement('dd');
                dt.textContent = term;
                dd.textContent = String(value);
                list.append(dt, dd);
            });
            card.append(title, badge, list);
            container.appendChild(card);
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
        byId('recoveryEnabled').checked = Boolean(wifi.fallback_ap_enabled);
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
        renderInverters(config.inverters || []);
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
        wifi.fallback_ap_enabled = byId('recoveryEnabled').checked;
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
    const WRITE_CONFIRMATION_ORDER = ['confirmed', 'pending', 'unverified', 'mismatched'];

    const WRITE_CONFIRMATION_STATES = Object.freeze({
        confirmed: Object.freeze({
            label: 'Confirmed',
            mark: '✓',
            meaning: 'A readback taken after the write matched the requested setpoint within the profile tolerance. This is the only state in which the controller records a confirmed value.'
        }),
        pending: Object.freeze({
            label: 'Pending',
            mark: '⋯',
            meaning: 'The write was accepted by the transport and no readback has confirmed it yet. This is not success. A real inverter can take longer than a second to apply a setpoint, so the controller waits rather than claiming the value took effect.'
        }),
        unverified: Object.freeze({
            label: 'Unverified',
            mark: '?',
            meaning: 'Confirmation is impossible or has failed to arrive: the assigned profile carries no manual-verified readback register, the write did not reach the device, or the confirmation deadline passed with no usable post-write sample. This is neither success nor failure.'
        }),
        mismatched: Object.freeze({
            label: 'Mismatched',
            mark: '!',
            meaning: 'A readback taken after the write disagreed with the requested setpoint. The controller treats this as a fault: it drives the inverter to its safe fallback and keeps the fault latched until a readback confirms that safe value.'
        })
    });

    function writeStateMeta(name) {
        return WRITE_CONFIRMATION_STATES[String(name || '').trim()] || null;
    }

    /* Percent values arrive as null whenever the controller has nothing to
     * report. Null must stay null: rendering 0% for "no write has been issued"
     * would state a setpoint the device never received. */
    function formatPercent(value, absent = 'Not reported') {
        const number = Number(value);
        return value == null || !Number.isFinite(number) ? absent : `${number.toFixed(1)} %`;
    }

    /* Writes only when the value actually changed. The lab banner is role="alert",
     * so rewriting identical text on every poll would re-announce it to a screen
     * reader several times a minute. */
    function setTextIfChanged(id, value) {
        const node = byId(id);
        if (!node) return;
        const text = value == null || value === '' ? '--' : String(value);
        if (node.textContent !== text) node.textContent = text;
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

    function renderLabBanner() {
        const banner = byId('labSimulatorBanner');
        if (!banner) return;
        const gate = state.commissioningGate;
        const solar = state.solarGridStatus;
        /* Either publisher is sufficient. Neither answering means the scope is
         * unknown, and an unknown scope must not be presented as production. */
        const lab = gate?.lab_simulator_mode === true || solar?.lab_simulator_mode === true;
        if (banner.hidden !== !lab) banner.hidden = !lab;
        if (!lab) return;
        /* The controller's own scope slug, not a friendlier word of ours. */
        setTextIfChanged('labSimulatorScope', verbatim(gate?.scope || solar?.commissioning_scope));
        setNoticeLine('labSimulatorNotice', solar?.lab_simulator_notice);
        setNoticeLine('labSimulatorScopeNotice', gate?.scope_notice);
    }

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

    /* ------------------------------------------------ setpoint write confirmation */

    function confirmStatePill(name) {
        const meta = writeStateMeta(name);
        const pill = document.createElement('span');
        pill.className = `confirm-state-pill confirm-state-${meta ? name : 'unverified'}`;
        const mark = document.createElement('span');
        mark.setAttribute('aria-hidden', 'true');
        mark.textContent = meta ? meta.mark : '?';
        const label = document.createElement('span');
        /* An unrecognised state is shown as the controller spelled it rather than
         * silently mapped onto one of the four this build knows about. */
        label.textContent = meta ? meta.label : verbatim(name);
        pill.append(mark, label);
        return pill;
    }

    function confirmValue(label, value, note) {
        const cell = document.createElement('div');
        cell.className = 'confirm-value';
        const term = document.createElement('span');
        term.textContent = label;
        const figure = document.createElement('strong');
        figure.textContent = value;
        cell.append(term, figure);
        if (note) {
            const small = document.createElement('small');
            small.textContent = note;
            cell.append(small);
        }
        return cell;
    }

    function confirmRowElement(entry) {
        const name = String(entry.state || '').trim();
        const meta = writeStateMeta(name);
        const row = document.createElement('article');
        row.className = `confirm-row state-${meta ? name : 'unverified'}`;
        row.dataset.inverterIndex = String(entry.index);

        const head = document.createElement('div');
        head.className = 'confirm-row-head';
        const title = document.createElement('span');
        title.className = 'confirm-row-title';
        title.textContent = `Inverter ${Number(entry.index) + 1}`;
        head.append(title, confirmStatePill(name));

        const meaning = document.createElement('p');
        meaning.className = 'confirm-row-meaning';
        meaning.textContent = entry.write_issued === false
            ? 'No write has been issued to this inverter, so there is nothing to confirm.'
            : meta ? meta.meaning
            : 'The controller reported a confirmation state this interface does not recognise. It is shown above exactly as received.';

        /* Requested and confirmed are separate figures with separate labels.
         * readback_percent is shown as a third figure because it is the raw
         * observation the verdict was made from, and it is not the same claim as
         * "confirmed". */
        const values = document.createElement('div');
        values.className = 'confirm-values';
        values.append(
            confirmValue('Requested', formatPercent(entry.requested_percent, 'No write issued'),
                'What the controller wrote.'),
            confirmValue('Confirmed', formatPercent(entry.confirmed_percent, 'Not confirmed'),
                'Only set when a readback matched.'),
            confirmValue('Last readback', formatPercent(entry.readback_percent, 'No readback'),
                'The raw observation, not a verdict.')
        );

        const counters = document.createElement('div');
        counters.className = 'confirm-counters';
        [
            ['Confirmed', entry.confirmed_count],
            ['Unverified', entry.unverified_count],
            ['Mismatched', entry.mismatch_count],
            ['Write successes', entry.write_successes],
            ['Write errors', entry.write_errors]
        ].forEach(([label, value]) => {
            const item = document.createElement('span');
            item.textContent = `${label}: ${Number.isFinite(Number(value)) ? Number(value) : '--'}`;
            counters.append(item);
        });

        row.append(head, meaning, values, counters);
        return row;
    }

    function renderConfirmLegend() {
        const legend = byId('confirmLegend');
        if (!legend || legend.childElementCount) return;
        legend.replaceChildren(...WRITE_CONFIRMATION_ORDER.map((name) => {
            const item = document.createElement('div');
            item.className = 'confirm-legend-item';
            item.append(confirmStatePill(name));
            const text = document.createElement('small');
            text.textContent = WRITE_CONFIRMATION_STATES[name].meaning;
            item.append(text);
            return item;
        }));
    }

    function renderWriteConfirmation() {
        const list = byId('confirmList');
        if (!list) return;
        renderConfirmLegend();
        const payload = state.writeConfirmation;
        if (!payload) {
            setBadgeIfChanged('confirmFleetBadge', STATES.dataQuality.unavailable, '');
            if (state.confirmSignature !== '') {
                state.confirmSignature = '';
                list.replaceChildren();
            }
            return;
        }

        const fleet = String(payload.fleet_state || '').trim();
        const meta = writeStateMeta(fleet);
        setBadgeIfChanged('confirmFleetBadge', `Fleet: ${meta ? meta.label : verbatim(fleet)}`,
            fleet === 'confirmed' ? 'good' : fleet === 'mismatched' ? 'bad'
            : fleet === 'pending' ? 'warning' : '');
        setTextIfChanged('confirmFleetDetail',
            `${meta ? meta.meaning : 'The controller reported a fleet confirmation state this interface does not recognise.'}`
            + ` The fleet takes its least trustworthy member's state.`
            + (payload.confirmation_fault === true
                ? ' A confirmation fault is latched on at least one inverter.'
                : ' No confirmation fault is latched.'));

        const items = Array.isArray(payload.inverters) ? payload.inverters : [];
        const signature = JSON.stringify(items);
        if (state.confirmSignature === signature) return;
        state.confirmSignature = signature;
        if (items.length === 0) {
            const empty = document.createElement('div');
            empty.className = 'empty-state';
            empty.textContent = 'The controller reports no inverters, so there are no setpoints to confirm. An empty fleet is reported unverified, never confirmed.';
            list.replaceChildren(empty);
            return;
        }
        list.replaceChildren(...items.map(confirmRowElement));
    }

    /* ------------------------------------------------------- lab target control */

    function labTargetSelections() {
        const index = Number(byId('labTargetInverter')?.value);
        const profileId = byId('labTargetProfile')?.value || '';
        const declare = byId('labTargetDeclaration')?.value === 'true';
        return {
            index: Number.isInteger(index) && index >= 0 ? index : null,
            profileId,
            declare
        };
    }

    function renderLabTargetReadiness() {
        const button = byId('labTargetApply');
        const badge = byId('labTargetBadge');
        if (!button) return;
        const access = window.AutomatrixEngineeringAccess;
        const authorized = Boolean(access && access.isAuthenticated());
        const acknowledged = Boolean(byId('labTargetAcknowledge')?.checked);
        const { index, profileId } = labTargetSelections();
        button.disabled = state.labTargetSending || !authorized || !acknowledged
            || index === null || !profileId;
        if (badge) {
            if (!authorized) setBadge('labTargetBadge', 'Sign in to declare a lab target', '');
            else if (!state.labProfiles.length) setBadge('labTargetBadge', 'Profile catalogue unavailable', 'bad');
            else if (!acknowledged) setBadge('labTargetBadge', 'Acknowledgement required', 'warning');
            else setBadge('labTargetBadge', 'Ready to send', 'warning');
        }
    }

    function renderLabProfileOptions() {
        const select = byId('labTargetProfile');
        if (!select) return;
        const previous = select.value;
        select.replaceChildren();
        state.labProfiles.forEach((profile) => {
            const option = document.createElement('option');
            option.value = profile.id;
            /* The profile's own qualification word is part of the label: choosing
             * a simulator-only profile and choosing to declare the endpoint a
             * simulator are two separate decisions and must both be visible. */
            option.textContent = `${profile.manufacturer || 'Unknown'} ${profile.model_family || ''}`.trim()
                + ` · ${verbatim(profile.qualification, 'qualification unknown')}`
                + (profile.simulator_only === true ? ' · simulator-only profile' : '');
            select.append(option);
        });
        if (previous && [...select.options].some((option) => option.value === previous)) {
            select.value = previous;
        }
        renderLabTargetReadiness();
    }

    function ensureLabInverterOptions() {
        const select = byId('labTargetInverter');
        if (!select || select.childElementCount) return;
        /* Twelve is APP_MAX_INVERTERS, the same bound the profile picker uses.
         * The controller rejects an index outside it. */
        for (let index = 0; index < 12; index += 1) {
            const option = document.createElement('option');
            option.value = String(index);
            option.textContent = `Inverter ${index + 1}`;
            select.append(option);
        }
    }

    function renderLabTargetResult(payload) {
        const list = byId('labTargetResult');
        if (!list) return;
        list.replaceChildren();
        if (!payload) {
            list.hidden = true;
            return;
        }
        const rows = [
            ['Lab target stored', payload.lab_target === true ? 'Yes' : 'No'],
            ['Write permission after restart', verbatim(payload.write_permission_after_restart)],
            ['Profile assigned', verbatim(payload.profile_id)],
            ['Restart required', payload.restart_required === true ? 'Yes' : 'No'],
            ['Automatic control', payload.automatic_control_disabled === true
                ? 'Disabled by the controller as a result of this change'
                : 'Not reported']
        ];
        const notice = typeof payload.lab_target_notice === 'string'
            ? payload.lab_target_notice.trim() : '';
        if (notice) rows.push(['Controller notice', notice]);
        rows.forEach(([term, value]) => {
            const dt = document.createElement('dt');
            const dd = document.createElement('dd');
            dt.textContent = term;
            dd.textContent = value;
            list.append(dt, dd);
        });
        list.hidden = false;
    }

    async function applyLabTarget() {
        const { index, profileId, declare } = labTargetSelections();
        if (index === null || !profileId || state.labTargetSending) return;
        const profile = state.labProfiles.find((entry) => entry.id === profileId) || null;

        /* The consequences are restated at the moment of the decision, not only
         * in the panel above it. Nothing here names a register, a command or a
         * timing value. */
        const consequences = declare
            ? [
                `Declare Inverter ${index + 1} a Modbus simulator and assign profile ${profileId}.`,
                '',
                'This grants command authority through a profile that has not been qualified on physical equipment.',
                'It disables automatic control; the firmware does this deliberately whenever a profile assignment changes.',
                'Commissioning will report lab_simulator_only, never production, while this declaration stands.',
                'The controller must be restarted before the new write permission takes effect.'
            ].join('\n')
            : [
                `Revoke the simulator declaration on Inverter ${index + 1} and assign profile ${profileId}.`,
                '',
                'Command authority then depends only on whether the assigned profile is qualified for production writes.',
                'It disables automatic control; the firmware does this deliberately whenever a profile assignment changes.',
                'The controller must be restarted before the new write permission takes effect.'
            ].join('\n');
        if (!window.confirm(consequences)) return;

        state.labTargetSending = true;
        renderLabTargetReadiness();
        setMessage('labTargetMessage', 'Sending declaration…');
        renderLabTargetResult(null);
        try {
            const payload = await api('/api/inverter-profile-assignment', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    inverter_index: index,
                    profile_id: profileId,
                    lab_target: declare
                })
            });
            renderLabTargetResult(payload);
            /* The stored declaration is reported, not the requested one, so the
             * message repeats what came back rather than what was asked for. */
            setMessage('labTargetMessage', payload?.lab_target === true
                ? 'Saved. This inverter is now a declared lab simulator target. Restart the controller to apply the write permission below.'
                : 'Saved. No lab simulator declaration is stored for this inverter. Restart the controller to apply the write permission below.',
                payload?.lab_target === true ? 'bad' : 'good');
            setBadge('labTargetBadge', 'Sent · restart required', 'warning');
            /* The scope may have changed, so re-read the gate rather than leaving
             * the banner describing the previous state. */
            refreshCommissioningGate();
        } catch (error) {
            if (error.status === 401) {
                setMessage('labTargetMessage',
                    'Declaring a lab target requires an authenticated engineering session. Nothing was changed.',
                    'bad');
            } else {
                setMessage('labTargetMessage',
                    `Declaration failed: ${error.message} Nothing was changed.`, 'bad');
            }
            setBadge('labTargetBadge', 'Send failed', 'bad');
        } finally {
            state.labTargetSending = false;
            renderLabTargetReadiness();
        }
        if (profile && profile.simulator_only === true) {
            /* Worth saying out loud: the profile itself is simulator-only, which
             * is a separate fact from the endpoint declaration. */
            toast('The assigned profile is itself marked simulator-only.', 'warning');
        }
    }

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
            renderLabBanner();
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
        renderLabBanner();
    }

    async function refreshSolarGridStatus() {
        /* Engineering-scoped to the control and commissioning routes by the
         * shared table in web/product-mode.js. Asked for only where it is
         * permitted; its lab_simulator_notice is additional to the gate's
         * scope_notice, never a replacement for it. */
        const access = window.AutomatrixEngineeringAccess;
        if (!access || !access.mayRequest('/api/solar-grid/status')) {
            state.solarGridStatus = null;
            renderLabBanner();
            return;
        }
        try {
            state.solarGridStatus = await api('/api/solar-grid/status');
        } catch (error) {
            state.solarGridStatus = null;
        }
        renderLabBanner();
    }

    async function refreshWriteConfirmation() {
        const access = window.AutomatrixEngineeringAccess;
        if (!access || !access.mayUseEngineering('inverters', 'control', 'commissioning')) {
            state.writeConfirmation = null;
            setStateMessage('confirmMessage', 'confirm-message', gateAccessNote(), '');
            renderWriteConfirmation();
            return;
        }
        try {
            state.writeConfirmation = await api('/api/inverters/write-confirmation');
            setStateMessage('confirmMessage', 'confirm-message', '', '');
        } catch (error) {
            state.writeConfirmation = null;
            setStateMessage('confirmMessage', 'confirm-message', error.status === 401
                ? 'The engineering session ended. Sign in again to read setpoint confirmation.'
                : `Setpoint confirmation could not be read: ${error.message}`, 'bad');
        }
        renderWriteConfirmation();
    }

    async function refreshLabProfiles() {
        const access = window.AutomatrixEngineeringAccess;
        ensureLabInverterOptions();
        if (!access || !access.mayRequest('/api/inverter-profiles')) {
            state.labProfiles = [];
            renderLabProfileOptions();
            return;
        }
        try {
            const payload = await api('/api/inverter-profiles');
            state.labProfiles = Array.isArray(payload?.profiles) ? payload.profiles : [];
        } catch (error) {
            state.labProfiles = [];
        }
        renderLabProfileOptions();
    }

    function refreshLabControl() {
        refreshCommissioningGate();
        refreshSolarGridStatus();
        refreshWriteConfirmation();
        refreshLabProfiles();
    }

    function bindLabControl() {
        ensureLabInverterOptions();
        byId('labTargetAcknowledge')?.addEventListener('change', renderLabTargetReadiness);
        byId('labTargetInverter')?.addEventListener('change', renderLabTargetReadiness);
        byId('labTargetProfile')?.addEventListener('change', renderLabTargetReadiness);
        byId('labTargetDeclaration')?.addEventListener('change', renderLabTargetReadiness);
        byId('labTargetApply')?.addEventListener('click', applyLabTarget);
        renderLabTargetReadiness();
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
        verbatim,
        applyRouteChrome,
        ensureNavigationHierarchy
    });

    async function start() {
        bindEvents();
        bindSiteTelemetry();
        bindLabControl();
        watchNavigation();
        if (!window.location.hash) window.location.hash = '#/dashboard';
        navigate();
        await Promise.allSettled([loadConfig(), refreshStatus()]);
        window.setInterval(refreshStatus, 2000);
        window.AutomatrixEngineeringAccess?.onScopeChange(refreshSourceDetection);
        refreshSourceDetection();
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
        window.setInterval(refreshWriteConfirmation, 5000);
    }

    start().catch((error) => {
        renderControllerUnavailable();
        toast(`Application startup failed: ${error.message}`, 'bad');
    });
})();
