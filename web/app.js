(() => {
    'use strict';

    const ROUTES = {
        dashboard: { title: 'Dashboard', breadcrumb: 'Dashboard' },
        wifi: { title: 'Wi-Fi', breadcrumb: 'Wi-Fi connection' },
        meters: { title: 'Meters', breadcrumb: 'Grid meters' },
        inverters: { title: 'Inverters', breadcrumb: 'Inverters' },
        control: { title: 'PV-DG Control', breadcrumb: 'PV-DG control' },
        system: { title: 'System', breadcrumb: 'System' },
        /* Routes owned by later modules still need an entry here. Without one,
         * routeFromHash() fell back to 'dashboard' and stamped that into
         * document.title, so #/alarms reported itself as "Dashboard" while
         * correctly rendering the alarms section - the route, the title and the
         * selected navigation item disagreed. */
        alarms: { title: 'Alarms', breadcrumb: 'Alarms and events' },
        readiness: { title: 'Pre-Lab Readiness', breadcrumb: 'Readiness' },
        commissioning: { title: 'Commissioning', breadcrumb: 'Commissioning' },
        engineering: { title: 'Engineering', breadcrumb: 'Engineering' }
    };

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
        lastUpdatedAt: null
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
        if (!response.ok) throw new Error(text || `${response.status} ${response.statusText}`);
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

    function navigate() {
        state.route = routeFromHash();
        all('.page').forEach((page) => page.classList.toggle('active', page.dataset.page === state.route));
        all('.nav-link').forEach((link) => link.classList.toggle('active', link.dataset.route === state.route));
        const meta = ROUTES[state.route];
        setText('pageTitle', meta.title);
        setText('breadcrumbCurrent', meta.breadcrumb);
        document.title = `${meta.title} · Automatrix PV-DG`;
        closeMenu();
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

    async function start() {
        bindEvents();
        bindSiteTelemetry();
        if (!window.location.hash) window.location.hash = '#/dashboard';
        navigate();
        await Promise.allSettled([loadConfig(), refreshStatus()]);
        window.setInterval(refreshStatus, 2000);
        window.AutomatrixEngineeringAccess?.onScopeChange(refreshSourceDetection);
        refreshSourceDetection();
    }

    start().catch((error) => {
        renderControllerUnavailable();
        toast(`Application startup failed: ${error.message}`, 'bad');
    });
})();
