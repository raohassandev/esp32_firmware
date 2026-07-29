(() => {
    'use strict';

    const ROUTES = {
        dashboard: { title: 'Dashboard', breadcrumb: 'Dashboard' },
        wifi: { title: 'Wi-Fi', breadcrumb: 'Wi-Fi connection' },
        meters: { title: 'Meters', breadcrumb: 'Grid meters' },
        inverters: { title: 'Inverters', breadcrumb: 'Inverters' },
        control: { title: 'PV-DG Control', breadcrumb: 'PV-DG control' },
        system: { title: 'System', breadcrumb: 'System' }
    };

    const WIFI_STATES = ['Idle', 'Scanning', 'Connecting primary', 'Connecting fallback', 'Connected', 'Setup AP', 'Disconnected'];
    const CONTROL_MODES = ['Disabled', 'Grid', 'Generator', 'Manual', 'Failsafe', 'Emergency'];

    const state = {
        route: 'dashboard',
        config: null,
        status: null,
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
        setText('flowPv', Number.isFinite(Number(status.applied_pv_kw)) ? formatPower(status.applied_pv_kw) : 'Unavailable');
        setText('flowLoad', 'Unavailable');
        setText('flowMeterAge', meterHasData ? formatAge(status.meter_age_ms) : 'Unavailable');

        const descriptor = meterHasData ? gridDescriptor(status.grid_power_kw) : gridDescriptor(null);
        setText('flowGrid', meterFresh ? formatPower(status.grid_power_kw) : meterStale ? `${formatPower(status.grid_power_kw)} stale` : 'Unavailable');
        setText('flowGridDetail', meterFresh ? descriptor.detail : meterStale ? 'Last valid sample; not current' : 'Meter data required');
        setText('gridArrow', descriptor.arrow);
        setBadge('flowState', meterFresh ? descriptor.label : meterStale ? 'Stale meter data' : 'Meter offline', meterFresh ? 'good' : meterStale ? 'warning' : 'bad');

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
        if (!window.location.hash) window.location.hash = '#/dashboard';
        navigate();
        await Promise.allSettled([loadConfig(), refreshStatus()]);
        window.setInterval(refreshStatus, 2000);
    }

    start().catch((error) => {
        renderControllerUnavailable();
        toast(`Application startup failed: ${error.message}`, 'bad');
    });
})();
(function (root, factory) {
    const api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    if (root) root.WifiUtils = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
    'use strict';

    const AUTH = {
        0: { label: 'Open', secure: false, supported: true },
        1: { label: 'WEP', secure: true, supported: false },
        2: { label: 'WPA-PSK', secure: true, supported: true },
        3: { label: 'WPA2-PSK', secure: true, supported: true },
        4: { label: 'WPA/WPA2-PSK', secure: true, supported: true },
        5: { label: 'WPA2-Enterprise', secure: true, supported: false },
        6: { label: 'WPA3-PSK', secure: true, supported: true },
        7: { label: 'WPA2/WPA3-PSK', secure: true, supported: true },
        8: { label: 'WAPI-PSK', secure: true, supported: false },
        9: { label: 'OWE', secure: false, supported: true },
        10: { label: 'WPA3-Enterprise 192-bit', secure: true, supported: false }
    };

    function authInfo(mode) {
        const numeric = Number(mode);
        return AUTH[numeric] || { label: `Security ${numeric}`, secure: true, supported: false };
    }

    function parseIpv4(text) {
        const parts = String(text || '').trim().split('.');
        if (parts.length !== 4) return null;
        let value = 0;
        for (const part of parts) {
            if (!/^\d{1,3}$/.test(part)) return null;
            const octet = Number(part);
            if (octet < 0 || octet > 255) return null;
            value = (value * 256) + octet;
        }
        return value >>> 0;
    }

    function isContiguousNetmask(text) {
        const mask = parseIpv4(text);
        if (mask === null || mask === 0 || mask === 0xffffffff) return false;
        const inverted = (~mask) >>> 0;
        return ((inverted + 1) & inverted) === 0;
    }

    function sameSubnet(ipText, gatewayText, maskText) {
        const ip = parseIpv4(ipText);
        const gateway = parseIpv4(gatewayText);
        const mask = parseIpv4(maskText);
        if (ip === null || gateway === null || mask === null) return false;
        return (ip & mask) === (gateway & mask);
    }

    function isUsableHost(ipText, maskText) {
        const ip = parseIpv4(ipText);
        const mask = parseIpv4(maskText);
        if (ip === null || mask === null || !isContiguousNetmask(maskText)) return false;
        const network = (ip & mask) >>> 0;
        const broadcast = (network | ((~mask) >>> 0)) >>> 0;
        return ip !== network && ip !== broadcast;
    }

    function validateStaticProfile(profile) {
        const errors = [];
        if (Number(profile.ip_mode) !== 1) return errors;
        if (parseIpv4(profile.static_ip) === null) errors.push(['ip', 'Enter a valid static IPv4 address.']);
        if (parseIpv4(profile.gateway) === null) errors.push(['gateway', 'Enter a valid IPv4 gateway.']);
        if (!isContiguousNetmask(profile.netmask)) errors.push(['netmask', 'Enter a contiguous IPv4 netmask.']);
        if (errors.length === 0) {
            if (!sameSubnet(profile.static_ip, profile.gateway, profile.netmask)) errors.push(['gateway', 'Gateway must be in the same subnet as the controller.']);
            if (!isUsableHost(profile.static_ip, profile.netmask)) errors.push(['ip', 'Static IP cannot be the network or broadcast address.']);
            if (!isUsableHost(profile.gateway, profile.netmask)) errors.push(['gateway', 'Gateway cannot be the network or broadcast address.']);
            if (parseIpv4(profile.static_ip) === parseIpv4(profile.gateway)) errors.push(['gateway', 'Gateway and controller IP must be different.']);
        }
        for (const key of ['dns1', 'dns2']) {
            if (profile[key] && parseIpv4(profile[key]) === null) errors.push([key, 'Enter a valid IPv4 address or leave blank.']);
        }
        return errors;
    }

    function signalLevel(rssi) {
        const value = Number(rssi);
        if (!Number.isFinite(value)) return 0;
        if (value >= -55) return 4;
        if (value >= -67) return 3;
        if (value >= -75) return 2;
        if (value >= -85) return 1;
        return 0;
    }

    return {
        authInfo,
        isContiguousNetmask,
        isUsableHost,
        parseIpv4,
        sameSubnet,
        signalLevel,
        validateStaticProfile
    };
});
(() => {
    'use strict';

    const state = {
        baseline: null,
        networks: []
    };

    const byId = (id) => document.getElementById(id);

    async function loadContext() {
        const requests = await Promise.allSettled([
            fetch('/api/config', { cache: 'no-store' }).then((response) => response.json()),
            fetch('/api/wifi/scan', { cache: 'no-store' }).then((response) => response.json())
        ]);
        if (requests[0].status === 'fulfilled') state.baseline = requests[0].value.wifi || null;
        if (requests[1].status === 'fulfilled') state.networks = requests[1].value.networks || [];
    }

    function clearGuardErrors() {
        document.querySelectorAll('.wifi-guard-error').forEach((node) => node.remove());
    }

    function showError(inputId, message) {
        const input = byId(inputId);
        const field = input && input.closest('.field');
        if (!field) return;
        const error = document.createElement('span');
        error.className = 'field-error wifi-guard-error';
        error.textContent = message;
        field.appendChild(error);
        input.focus();
    }

    function scanAllowsEmptyPassword(ssid) {
        const record = state.networks.find((network) => network.ssid === ssid);
        return record && (Number(record.auth_mode) === 0 || Number(record.auth_mode) === 9);
    }

    function guardSubmit(event) {
        clearGuardErrors();
        if (!state.baseline) return;

        for (const role of ['primary', 'fallback']) {
            const enabled = byId(`${role}Enabled`).checked;
            const ssid = byId(`${role}Ssid`).value.trim();
            const password = byId(`${role}Password`).value;
            const original = state.baseline[role] || {};
            if (enabled && ssid !== (original.ssid || '') && !password && !scanAllowsEmptyPassword(ssid)) {
                event.preventDefault();
                event.stopImmediatePropagation();
                showError(`${role}Password`, 'Enter the credential for the new SSID. A blank password is allowed only when the latest scan identifies the network as Open or OWE.');
                return;
            }
        }

        const recoveryEnabled = byId('recoveryEnabled').checked;
        const recoverySsid = byId('recoverySsid').value.trim();
        const recoveryPassword = byId('recoveryPassword').value;
        if (recoveryEnabled && recoverySsid !== (state.baseline.fallback_ap_ssid || '') && !recoveryPassword) {
            event.preventDefault();
            event.stopImmediatePropagation();
            showError('recoveryPassword', 'Enter a new recovery-AP password when changing its SSID.');
        }
    }

    function bind() {
        const form = byId('wifiForm');
        if (form) form.addEventListener('submit', guardSubmit, true);
        window.addEventListener('hashchange', () => {
            if (window.location.hash.includes('/wifi')) loadContext();
        });
        const scanButton = byId('wifiScanButton');
        if (scanButton) scanButton.addEventListener('click', () => window.setTimeout(loadContext, 1500));
    }

    bind();
    loadContext();
})();
(() => {
    'use strict';

    const utils = window.WifiUtils;
    if (!utils) return;

    const state = {
        initialWifi: null,
        scanPollTimer: null,
        selected: { primary: null, fallback: null },
        saving: false
    };

    const byId = (id) => document.getElementById(id);

    async function api(path, options = {}) {
        const response = await fetch(path, { cache: 'no-store', ...options });
        const text = await response.text();
        let payload = null;
        if (text) {
            try { payload = JSON.parse(text); }
            catch (error) { payload = { error: text }; }
        }
        if (!response.ok) throw new Error(payload && payload.error ? payload.error : text || `${response.status} ${response.statusText}`);
        return payload;
    }

    function installScanPanel() {
        const wifiPage = document.querySelector('.page[data-page="wifi"]');
        const intro = wifiPage && wifiPage.querySelector('.page-intro');
        if (!wifiPage || !intro || byId('wifiScanPanel')) return;

        const panel = document.createElement('article');
        panel.className = 'panel wifi-scan-panel';
        panel.id = 'wifiScanPanel';
        panel.innerHTML = [
            '<div class="panel-header wifi-scan-header">',
            '<div><p class="eyebrow">Radio survey</p><h3>Available Wi-Fi networks</h3><p class="wifi-scan-copy">A scan is read-only and does not change the saved connection profiles.</p></div>',
            '<div class="wifi-scan-actions"><span class="subtle-badge" id="wifiScanState">Not scanned</span><button class="button secondary" id="wifiScanButton" type="button">Scan networks</button></div>',
            '</div>',
            '<div class="wifi-scan-message" id="wifiScanMessage" role="status"></div>',
            '<div class="wifi-network-list" id="wifiNetworkList"><div class="empty-state">Run a scan to discover nearby networks.</div></div>'
        ].join('');
        intro.after(panel);
    }

    function setScanMessage(message, tone = '') {
        const node = byId('wifiScanMessage');
        if (!node) return;
        node.textContent = message || '';
        node.className = `wifi-scan-message${tone ? ` ${tone}` : ''}`;
    }

    function setScanBadge(label, tone = '') {
        const node = byId('wifiScanState');
        if (!node) return;
        node.textContent = label;
        node.className = `subtle-badge${tone ? ` ${tone}` : ''}`;
    }

    function securityBadge(record) {
        const info = utils.authInfo(record.auth_mode);
        const badge = document.createElement('span');
        badge.className = `network-security${info.supported ? '' : ' unsupported'}`;
        badge.textContent = info.label;
        return badge;
    }

    function tag(label, tone = '') {
        const node = document.createElement('span');
        node.className = `network-tag${tone ? ` ${tone}` : ''}`;
        node.textContent = label;
        return node;
    }

    function selectNetwork(role, record) {
        const prefix = role === 'primary' ? 'primary' : 'fallback';
        byId(`${prefix}Enabled`).checked = true;
        byId(`${prefix}Ssid`).value = record.ssid;
        state.selected[role] = { ssid: record.ssid, authMode: Number(record.auth_mode) };

        const info = utils.authInfo(record.auth_mode);
        const password = byId(`${prefix}Password`);
        password.value = '';
        if (info.secure) {
            password.focus();
            setScanMessage(`${record.ssid} selected as ${role}. Enter its password before saving.`, 'warning');
        } else {
            setScanMessage(`${record.ssid} selected as ${role}. This network does not require a password.`, 'good');
        }
        document.querySelector(`#${prefix}Ssid`).scrollIntoView({ behavior: 'smooth', block: 'center' });
    }

    function renderNetworks(snapshot) {
        const container = byId('wifiNetworkList');
        if (!container) return;
        container.replaceChildren();

        const networks = Array.isArray(snapshot.networks) ? snapshot.networks : [];
        if (networks.length === 0) {
            const empty = document.createElement('div');
            empty.className = 'empty-state';
            empty.textContent = snapshot.state === 'failed' ? 'The scan failed. Existing connection profiles were not changed.' : 'No visible Wi-Fi networks were found.';
            container.appendChild(empty);
            return;
        }

        networks.forEach((record) => {
            const info = utils.authInfo(record.auth_mode);
            const row = document.createElement('div');
            row.className = 'wifi-network-row';

            const identity = document.createElement('div');
            identity.className = 'wifi-network-identity';
            const name = document.createElement('strong');
            name.textContent = record.ssid;
            const metadata = document.createElement('div');
            metadata.className = 'wifi-network-meta';
            metadata.append(securityBadge(record), document.createTextNode(` Channel ${record.channel}`));
            const tags = document.createElement('div');
            tags.className = 'wifi-network-tags';
            if (record.connected) tags.appendChild(tag('Connected', 'good'));
            if (record.configured_primary) tags.appendChild(tag('Primary'));
            if (record.configured_fallback) tags.appendChild(tag('Fallback'));
            identity.append(name, metadata, tags);

            const signal = document.createElement('div');
            signal.className = 'wifi-network-signal';
            const bars = document.createElement('span');
            bars.className = `signal-bars level-${utils.signalLevel(record.rssi)}`;
            bars.setAttribute('aria-label', `${record.rssi} dBm`);
            bars.textContent = '▂▄▆█';
            const value = document.createElement('small');
            value.textContent = `${record.rssi} dBm`;
            signal.append(bars, value);

            const actions = document.createElement('div');
            actions.className = 'wifi-network-actions';
            for (const role of ['primary', 'fallback']) {
                const button = document.createElement('button');
                button.type = 'button';
                button.className = 'button secondary compact-button';
                button.textContent = role === 'primary' ? 'Use as primary' : 'Use as fallback';
                button.disabled = !info.supported;
                if (!info.supported) button.title = 'This security mode is not supported by the current commissioning form.';
                button.addEventListener('click', () => selectNetwork(role, record));
                actions.appendChild(button);
            }

            row.append(identity, signal, actions);
            container.appendChild(row);
        });
    }

    function scanStateName(value) {
        return ['idle', 'running', 'complete', 'failed'][Number(value)] || 'unknown';
    }

    async function loadScanSnapshot() {
        try {
            const snapshot = await api('/api/wifi/scan');
            const name = scanStateName(snapshot.state);
            snapshot.state = name;
            renderNetworks(snapshot);

            if (name === 'running') {
                setScanBadge('Scanning…', 'warning');
                setScanMessage('The controller is surveying nearby access points. The active profile is unchanged.');
                scheduleScanPoll();
            } else if (name === 'complete') {
                setScanBadge(`${snapshot.networks.length} found`, 'good');
                setScanMessage(`Scan completed. Results are sorted by signal strength.`, 'good');
            } else if (name === 'failed') {
                setScanBadge('Scan failed', 'bad');
                setScanMessage(snapshot.error_name ? `Scan failed: ${snapshot.error_name}` : 'The Wi-Fi scan failed.', 'bad');
            } else {
                setScanBadge('Not scanned');
            }
        } catch (error) {
            setScanBadge('Unavailable', 'bad');
            setScanMessage(`Unable to load scan results: ${error.message}`, 'bad');
        }
    }

    function scheduleScanPoll() {
        window.clearTimeout(state.scanPollTimer);
        state.scanPollTimer = window.setTimeout(loadScanSnapshot, 800);
    }

    async function requestScan() {
        const button = byId('wifiScanButton');
        if (button) button.disabled = true;
        setScanBadge('Starting…', 'warning');
        setScanMessage('Requesting a non-disruptive radio scan…');
        try {
            await api('/api/wifi/scan', { method: 'POST' });
            await loadScanSnapshot();
        } catch (error) {
            setScanBadge('Not started', 'bad');
            setScanMessage(`Scan request rejected: ${error.message}`, 'bad');
        } finally {
            if (button) button.disabled = false;
        }
    }

    function profileFromForm(prefix) {
        return {
            enabled: byId(`${prefix}Enabled`).checked,
            ssid: byId(`${prefix}Ssid`).value.trim(),
            password: byId(`${prefix}Password`).value,
            ip_mode: Number(byId(`${prefix}Mode`).value),
            static_ip: byId(`${prefix}Ip`).value.trim(),
            gateway: byId(`${prefix}Gateway`).value.trim(),
            netmask: byId(`${prefix}Netmask`).value.trim(),
            dns1: byId(`${prefix}Dns1`).value.trim(),
            dns2: byId(`${prefix}Dns2`).value.trim()
        };
    }

    function clearCommissioningErrors() {
        document.querySelectorAll('.wifi-commissioning-error').forEach((node) => node.remove());
        document.querySelectorAll('.wifi-commissioning-invalid').forEach((node) => node.classList.remove('wifi-commissioning-invalid'));
    }

    function fieldError(id, message) {
        const input = byId(id);
        const field = input && input.closest('.field');
        if (!field) return;
        field.classList.add('wifi-commissioning-invalid');
        const error = document.createElement('span');
        error.className = 'field-error wifi-commissioning-error';
        error.textContent = message;
        field.appendChild(error);
    }

    function validateProfile(prefix, profile, original) {
        let valid = true;
        if (profile.enabled && !profile.ssid) {
            fieldError(`${prefix}Ssid`, 'SSID is required when this profile is enabled.');
            valid = false;
        }
        if (profile.password && (profile.password.length < 8 || profile.password.length > 64)) {
            fieldError(`${prefix}Password`, 'Wi-Fi passwords must contain 8–64 characters.');
            valid = false;
        }
        for (const [field, message] of utils.validateStaticProfile(profile)) {
            const suffix = { ip: 'Ip', gateway: 'Gateway', netmask: 'Netmask', dns1: 'Dns1', dns2: 'Dns2' }[field];
            fieldError(`${prefix}${suffix}`, message);
            valid = false;
        }

        const selected = state.selected[prefix];
        const selectedInfo = selected && selected.ssid === profile.ssid ? utils.authInfo(selected.authMode) : null;
        if (profile.enabled && selectedInfo && selectedInfo.secure && original && original.ssid !== profile.ssid && !profile.password) {
            fieldError(`${prefix}Password`, 'Enter the password for the newly selected secured network.');
            valid = false;
        }
        return valid;
    }

    function buildWifiPayload() {
        clearCommissioningErrors();
        const initial = state.initialWifi || {};
        const primary = profileFromForm('primary');
        const fallback = profileFromForm('fallback');
        let valid = validateProfile('primary', primary, initial.primary || {});
        valid = validateProfile('fallback', fallback, initial.fallback || {}) && valid;

        if (!primary.enabled) {
            fieldError('primarySsid', 'The primary profile must remain enabled.');
            valid = false;
        }
        if (primary.enabled && fallback.enabled && primary.ssid && primary.ssid === fallback.ssid) {
            fieldError('fallbackSsid', 'Primary and fallback SSIDs must be different.');
            valid = false;
        }

        const recoveryEnabled = byId('recoveryEnabled').checked;
        const recoverySsid = byId('recoverySsid').value.trim();
        const recoveryPassword = byId('recoveryPassword').value;
        if (recoveryEnabled && !recoverySsid) {
            fieldError('recoverySsid', 'Recovery AP SSID is required when enabled.');
            valid = false;
        }
        if (recoveryPassword && (recoveryPassword.length < 8 || recoveryPassword.length > 64)) {
            fieldError('recoveryPassword', 'Recovery AP password must contain 8–64 characters.');
            valid = false;
        }

        const retries = Number(byId('wifiRetries').value);
        const backoff = Number(byId('wifiBackoff').value);
        if (!Number.isInteger(retries) || retries < 1 || retries > 20) {
            fieldError('wifiRetries', 'Retries must be between 1 and 20.');
            valid = false;
        }
        if (!Number.isInteger(backoff) || backoff < 500 || backoff > 60000) {
            fieldError('wifiBackoff', 'Reconnect delay must be 500–60000 ms.');
            valid = false;
        }
        if (!valid) throw new Error('Correct the highlighted Wi-Fi settings.');

        for (const role of ['primary', 'fallback']) {
            const profile = role === 'primary' ? primary : fallback;
            const selected = state.selected[role];
            if (selected && selected.ssid === profile.ssid && !utils.authInfo(selected.authMode).secure) {
                profile.clear_password = true;
            }
        }

        return {
            primary,
            fallback,
            scan_before_connect: byId('scanBeforeConnect').checked,
            fallback_ap_enabled: recoveryEnabled,
            fallback_ap_ssid: recoverySsid,
            fallback_ap_password: recoveryPassword,
            max_retries_per_profile: retries,
            reconnect_backoff_ms: backoff
        };
    }

    function comparableWifi(wifi) {
        const copy = JSON.parse(JSON.stringify(wifi || {}));
        for (const role of ['primary', 'fallback']) {
            if (copy[role]) {
                copy[role].password = '';
                delete copy[role].clear_password;
            }
        }
        copy.fallback_ap_password = '';
        return copy;
    }

    async function handleWifiSubmit(event) {
        event.preventDefault();
        event.stopImmediatePropagation();
        if (state.saving) return;

        const message = byId('wifiMessage');
        if (message) message.textContent = '';
        try {
            const payload = buildWifiPayload();
            if (JSON.stringify(comparableWifi(payload)) === JSON.stringify(comparableWifi(state.initialWifi))) {
                throw new Error('No Wi-Fi configuration changes were detected.');
            }

            const primaryChanged = !state.initialWifi || payload.primary.ssid !== (state.initialWifi.primary || {}).ssid;
            const warning = primaryChanged
                ? `The primary network will change to “${payload.primary.ssid}”. The controller will restart and its IP address may change. Continue?`
                : 'Save the Wi-Fi configuration and restart the controller now? Its DHCP address may change.';
            if (!window.confirm(warning)) return;

            state.saving = true;
            setWifiBusy(true);
            if (message) message.textContent = 'Saving Wi-Fi configuration…';
            await api('/api/wifi/config', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });
            if (message) message.textContent = 'Saved. Restarting controller…';
            await api('/api/system/restart', { method: 'POST' });
            if (message) message.textContent = 'Restart accepted. Rediscover the controller if its IP changes.';
        } catch (error) {
            if (message) message.textContent = error.message;
        } finally {
            state.saving = false;
            setWifiBusy(false);
        }
    }

    function setWifiBusy(busy) {
        const form = byId('wifiForm');
        if (!form) return;
        form.querySelectorAll('button').forEach((button) => { button.disabled = busy; });
    }

    async function loadInitialWifi() {
        try {
            const config = await api('/api/config');
            state.initialWifi = config.wifi || {};
        } catch (error) {
            setScanMessage(`Configuration baseline unavailable: ${error.message}`, 'bad');
        }
    }

    function protectReconnectAction(event) {
        if (!window.confirm('Disconnect now and retry the currently saved primary and fallback profiles?')) {
            event.preventDefault();
            event.stopImmediatePropagation();
        }
    }

    function compactControllerPill() {
        const pill = byId('controllerPill');
        if (!pill) return;
        const mobile = window.matchMedia('(max-width: 650px)').matches;
        const label = pill.textContent || '';
        if (mobile && label.startsWith('Online · ')) {
            pill.dataset.fullLabel = label;
            pill.title = label;
            pill.textContent = 'Online';
        } else if (!mobile && label === 'Online' && pill.dataset.fullLabel) {
            pill.textContent = pill.dataset.fullLabel;
        }
    }

    function bind() {
        installScanPanel();
        const scanButton = byId('wifiScanButton');
        if (scanButton) scanButton.addEventListener('click', requestScan);

        const form = byId('wifiForm');
        if (form) form.addEventListener('submit', handleWifiSubmit, true);

        const reconnect = byId('wifiRescanButton');
        if (reconnect) {
            reconnect.textContent = 'Reconnect saved profiles';
            reconnect.addEventListener('click', protectReconnectAction, true);
        }

        for (const role of ['primary', 'fallback']) {
            byId(`${role}Ssid`).addEventListener('input', () => { state.selected[role] = null; });
        }

        window.addEventListener('hashchange', () => {
            if (window.location.hash.includes('/wifi')) loadScanSnapshot();
        });
        window.addEventListener('resize', compactControllerPill);

        const pill = byId('controllerPill');
        if (pill) new MutationObserver(compactControllerPill).observe(pill, { childList: true });
        compactControllerPill();
    }

    async function start() {
        bind();
        await Promise.allSettled([loadInitialWifi(), loadScanSnapshot()]);
    }

    start().catch((error) => setScanMessage(`Wi-Fi commissioning failed to initialize: ${error.message}`, 'bad'));
})();
(function (root, factory) {
    const api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.PvdgDeviceUtils = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
    'use strict';

    function finite(value) {
        if (value == null || value === '' || typeof value === 'boolean') return null;
        const number = Number(value);
        return Number.isFinite(number) ? number : null;
    }

    function formatPower(value) {
        const number = finite(value);
        return number == null ? 'Unavailable' : `${number.toFixed(2)} kW`;
    }

    function formatPercent(value) {
        const number = finite(value);
        return number == null ? 'Unavailable' : `${number.toFixed(1)}%`;
    }

    function formatAge(value) {
        const milliseconds = finite(value);
        if (milliseconds == null || milliseconds < 0) return 'Never';
        if (milliseconds < 1000) return `${Math.round(milliseconds)} ms ago`;
        if (milliseconds < 60000) return `${(milliseconds / 1000).toFixed(1)} s ago`;
        if (milliseconds < 3600000) return `${(milliseconds / 60000).toFixed(1)} min ago`;
        return `${(milliseconds / 3600000).toFixed(1)} h ago`;
    }

    function meterState(meter) {
        const runtime = meter && meter.runtime ? meter.runtime : {};
        if (!meter || !meter.enabled) {
            return { label: 'Disabled', tone: 'neutral', detail: 'Polling is disabled by configuration.' };
        }
        if (runtime.initialization_failed) {
            return { label: 'Initialization failed', tone: 'bad', detail: 'The Modbus runtime could not be initialized. Review the endpoint and system resources.' };
        }
        if (runtime.online && !runtime.stale) {
            return { label: 'Online', tone: 'good', detail: 'Latest Modbus sample is current.' };
        }
        if (runtime.has_data && runtime.stale) {
            return { label: 'Stale', tone: 'warning', detail: 'Last valid value is retained but is not current.' };
        }
        return { label: 'Unavailable', tone: 'bad', detail: 'No current valid Modbus sample is available.' };
    }

    function inverterState(inverter) {
        const runtime = inverter && inverter.runtime ? inverter.runtime : {};
        if (!inverter || !inverter.enabled) {
            return { label: 'Disabled', tone: 'neutral', detail: 'Command channel is disabled by configuration.' };
        }
        if (runtime.initialization_failed) {
            return { label: 'Initialization failed', tone: 'bad', detail: 'The Modbus command channel could not be initialized.' };
        }
        if (!runtime.has_command) {
            return { label: 'Not tested', tone: 'neutral', detail: 'No command has been issued since this boot.' };
        }
        if (runtime.last_write_ok) {
            return { label: 'Last write OK', tone: 'good', detail: 'This confirms only the last command transaction, not inverter telemetry.' };
        }
        return { label: 'Last write failed', tone: 'bad', detail: 'The most recent command transaction failed.' };
    }

    function endpointLabel(endpoint) {
        if (!endpoint) return 'Unavailable';
        const host = endpoint.host || '--';
        const port = finite(endpoint.port);
        return `${host}:${port == null ? '--' : port}`;
    }

    function readinessTone(value) {
        return value === true ? 'good' : value === false ? 'bad' : 'neutral';
    }

    return {
        finite,
        formatPower,
        formatPercent,
        formatAge,
        meterState,
        inverterState,
        endpointLabel,
        readinessTone
    };
});
(() => {
    'use strict';

    const utils = window.PvdgDeviceUtils;
    if (!utils) return;

    const state = {
        telemetry: null,
        meters: null,
        inverters: null,
        loading: false,
        lastUpdated: null
    };

    const byId = (id) => document.getElementById(id);

    function element(tag, className, text) {
        const node = document.createElement(tag);
        if (className) node.className = className;
        if (text != null) node.textContent = String(text);
        return node;
    }

    function setText(id, value) {
        const node = byId(id);
        if (node) node.textContent = value == null || value === '' ? '--' : String(value);
    }

    function setBadge(node, label, tone) {
        node.textContent = label;
        node.className = `subtle-badge${tone ? ` ${tone}` : ''}`;
    }

    function summaryCard(id, label, detailId = '') {
        const card = element('div', 'device-summary-card');
        const title = element('span', '', label);
        const value = element('strong', '', '--');
        value.id = id;
        card.append(title, value);
        if (detailId) {
            const detail = element('small', '', '--');
            detail.id = detailId;
            card.append(detail);
        }
        return card;
    }

    function setSummaryTone(id, tone) {
        const value = byId(id);
        const card = value?.closest('.device-summary-card');
        if (!card) return;
        card.classList.remove('good', 'warning', 'bad');
        if (tone && tone !== 'neutral') card.classList.add(tone);
    }

    function toolbar(messageId, refreshId) {
        const bar = element('div', 'device-toolbar');
        const copy = element('div', 'device-toolbar-copy', 'Loading runtime diagnostics…');
        copy.id = messageId;
        const button = element('button', 'button secondary', 'Refresh diagnostics');
        button.type = 'button';
        button.id = refreshId;
        bar.append(copy, button);
        return bar;
    }

    function ensureDashboardScaffold() {
        const page = document.querySelector('[data-page="dashboard"]');
        if (!page || byId('operationalTelemetrySummary')) return;
        const metrics = page.querySelector('.metric-grid');
        if (!metrics) return;

        const summary = element('div', 'device-summary');
        summary.id = 'operationalTelemetrySummary';
        summary.append(
            summaryCard('operationalNetwork', 'Network', 'operationalNetworkDetail'),
            summaryCard('operationalGrid', 'Fresh grid power', 'operationalGridDetail'),
            summaryCard('operationalCapacity', 'Commandable capacity', 'operationalCapacityDetail'),
            summaryCard('operationalControl', 'Automatic control', 'operationalControlDetail')
        );

        const note = element('div', 'device-readiness-note');
        note.id = 'operationalReadinessNote';
        note.setAttribute('role', 'status');
        note.textContent = 'Loading operational readiness…';
        metrics.after(summary, note);
    }

    function ensureMeterScaffold() {
        const page = document.querySelector('[data-page="meters"]');
        if (!page || byId('meterTelemetrySummary')) return;
        const intro = page.querySelector('.page-intro');
        if (!intro) return;

        const bar = toolbar('meterTelemetryMessage', 'meterTelemetryRefresh');
        const summary = element('div', 'device-summary');
        summary.id = 'meterTelemetrySummary';
        summary.append(
            summaryCard('meterConfiguredCount', 'Configured'),
            summaryCard('meterEnabledCount', 'Enabled'),
            summaryCard('meterOnlineCount', 'Online'),
            summaryCard('meterStaleCount', 'Stale / unavailable')
        );
        const list = element('div', 'device-list');
        list.id = 'meterRuntimeList';
        intro.after(bar, summary, list);
    }

    function ensureInverterScaffold() {
        const page = document.querySelector('[data-page="inverters"]');
        if (!page || byId('inverterTelemetrySummary')) return;
        const notice = page.querySelector('.notice');
        const intro = page.querySelector('.page-intro');
        const anchor = notice || intro;
        if (!anchor) return;

        const bar = toolbar('inverterTelemetryMessage', 'inverterTelemetryRefresh');
        const summary = element('div', 'device-summary');
        summary.id = 'inverterTelemetrySummary';
        summary.append(
            summaryCard('inverterConfiguredCount', 'Configured'),
            summaryCard('inverterEnabledCount', 'Enabled'),
            summaryCard('inverterRatedTotal', 'Enabled rating'),
            summaryCard('inverterCommandTested', 'Command-tested')
        );
        const legacyList = byId('inverterList');
        if (legacyList) {
            legacyList.hidden = true;
            legacyList.setAttribute('aria-hidden', 'true');
        }
        const runtimeList = element('div', 'device-list');
        runtimeList.id = 'inverterRuntimeList';
        anchor.after(bar, summary, runtimeList);
    }

    function ensureScaffold() {
        ensureDashboardScaffold();
        ensureMeterScaffold();
        ensureInverterScaffold();
    }

    function metaItem(label, value) {
        const item = element('div', 'device-meta-item');
        item.append(element('span', '', label), element('strong', '', value));
        item.querySelector('strong').title = String(value);
        return item;
    }

    function emptyState(message, isError = false) {
        return element('div', `device-empty${isError ? ' device-error' : ''}`, message);
    }

    function modeLabel(mode) {
        const labels = ['Disabled', 'Grid', 'Generator', 'Manual', 'Failsafe', 'Emergency'];
        const index = Number(mode);
        return Number.isInteger(index) && labels[index] ? labels[index] : 'Unavailable';
    }

    function renderDashboard() {
        const data = state.telemetry;
        if (!byId('operationalTelemetrySummary')) return;
        if (!data || !data.network || !data.grid_meter || !data.inverters || !data.control) {
            setText('operationalNetwork', 'Unavailable');
            setText('operationalGrid', 'Unavailable');
            setText('operationalCapacity', 'Unavailable');
            setText('operationalControl', 'Unavailable');
            setText('operationalReadinessNote', 'Operational telemetry is unavailable. Existing dashboard values remain authoritative only where they have valid source data.');
            ['operationalNetwork', 'operationalGrid', 'operationalCapacity', 'operationalControl']
                .forEach((id) => setSummaryTone(id, 'bad'));
            return;
        }

        const networkOnline = data.network.online === true;
        setText('operationalNetwork', networkOnline ? 'Online' : 'Offline');
        setText('operationalNetworkDetail', networkOnline
            ? `${data.network.ssid || '--'} · ${data.network.ip || '--'}`
            : data.network.recovery_ap_active ? 'Recovery AP active' : 'Station unavailable');
        setSummaryTone('operationalNetwork', networkOnline ? 'good' : 'bad');

        const gridFresh = data.grid_meter.fresh === true;
        setText('operationalGrid', utils.formatPower(data.grid_meter.active_power_kw));
        let gridDetail = data.grid_meter.state || 'unavailable';
        if (!gridFresh && data.grid_meter.retained_active_power_kw != null) {
            gridDetail = `Retained ${utils.formatPower(data.grid_meter.retained_active_power_kw)} · ${utils.formatAge(data.grid_meter.data_age_ms)}`;
        } else if (gridFresh) {
            gridDetail = `Fresh · ${utils.formatAge(data.grid_meter.data_age_ms)}`;
        }
        setText('operationalGridDetail', gridDetail);
        setSummaryTone('operationalGrid', gridFresh ? 'good' : data.grid_meter.retained_active_power_kw != null ? 'warning' : 'bad');

        const commandable = utils.finite(data.inverters.commandable_rated_kw);
        setText('operationalCapacity', utils.formatPower(commandable));
        setText('operationalCapacityDetail', `${data.inverters.enabled ?? 0} enabled · ${data.inverters.initialization_failed ?? 0} init failed`);
        setSummaryTone('operationalCapacity', commandable != null && commandable > 0 ? 'good' : 'warning');

        const controlActive = data.control.enabled === true;
        setText('operationalControl', controlActive ? 'Active' : 'Disabled');
        setText('operationalControlDetail', `${modeLabel(data.control.mode)} · cycle ${utils.formatAge(data.control.last_cycle_age_ms)}`);
        setSummaryTone('operationalControl', controlActive ? 'good' : 'neutral');

        const availability = data.availability || {};
        const monitoring = availability.monitoring_ready ? 'Monitoring path ready' : 'Monitoring path not ready';
        const commandPath = availability.command_path_ready ? 'command path initialized' : 'command path unavailable';
        const auto = availability.automatic_control_active ? 'automatic control active' : 'automatic control disabled';
        setText('operationalReadinessNote', `${monitoring}; ${commandPath}; ${auto}. Measured inverter production, generator power and facility-load telemetry are not configured and remain unavailable.`);
    }

    function meterCard(meter) {
        const runtime = meter.runtime || {};
        const acquisition = meter.acquisition || {};
        const status = utils.meterState(meter);
        const card = element('article', 'device-runtime-card');
        const top = element('div', 'device-card-top');
        const heading = element('div');
        heading.append(
            element('div', 'device-card-index', `Meter ${Number(meter.index) + 1}`),
            element('h3', '', meter.name || `Meter ${Number(meter.index) + 1}`),
            element('p', 'device-state-detail', status.detail)
        );
        const badge = element('span');
        setBadge(badge, status.label, status.tone);
        top.append(heading, badge);

        const reading = element('div', 'device-reading');
        const valueBlock = element('div');
        valueBlock.append(
            element('div', 'device-reading-label', 'Active power'),
            element('strong', 'device-reading-value', utils.formatPower(runtime.active_power_kw))
        );
        let readingNote = 'No valid meter sample has been received.';
        if (runtime.has_data && runtime.stale) readingNote = `Retained value · ${utils.formatAge(runtime.data_age_ms)} · not current`;
        else if (runtime.online) readingNote = `Current sample · ${utils.formatAge(runtime.data_age_ms)}`;
        reading.append(valueBlock, element('div', 'device-reading-note', readingNote));

        const meta = element('div', 'device-meta-grid');
        const errorLabel = Number(runtime.last_error) === 0 && Number(runtime.error_count) === 0
            ? 'None'
            : runtime.last_error_name || `Error ${runtime.last_error}`;
        meta.append(
            metaItem('Endpoint', `${utils.endpointLabel(meter.endpoint)} · Unit ${meter.endpoint?.unit_id ?? '--'}`),
            metaItem('Acquisition', `FC${acquisition.function ?? '--'} · PDU ${acquisition.pdu_address ?? '--'}`),
            metaItem('Format', `Type ${acquisition.data_type ?? '--'} · Order ${acquisition.word_order ?? '--'} · ×${acquisition.scale ?? '--'}`),
            metaItem('Timing', `Poll ${acquisition.poll_ms ?? '--'} ms · Timeout ${meter.endpoint?.timeout_ms ?? '--'} ms`),
            metaItem('Last attempt', utils.formatAge(runtime.last_attempt_age_ms)),
            metaItem('Successful polls', runtime.success_count ?? 0),
            metaItem('Errors', `${runtime.error_count ?? 0} total · ${runtime.consecutive_failures ?? 0} consecutive`),
            metaItem('Last error', errorLabel)
        );
        card.append(top, reading, meta);
        return card;
    }

    function inverterCard(inverter) {
        const runtime = inverter.runtime || {};
        const command = inverter.command || {};
        const status = utils.inverterState(inverter);
        const card = element('article', 'device-runtime-card');
        const top = element('div', 'device-card-top');
        const heading = element('div');
        heading.append(
            element('div', 'device-card-index', `Inverter ${Number(inverter.index) + 1}`),
            element('h3', '', inverter.name || `Inverter ${Number(inverter.index) + 1}`),
            element('p', 'device-state-detail', status.detail)
        );
        const badge = element('span');
        setBadge(badge, status.label, status.tone);
        top.append(heading, badge);

        const reading = element('div', 'device-reading');
        const valueBlock = element('div');
        valueBlock.append(
            element('div', 'device-reading-label', 'Measured production'),
            element('strong', 'device-reading-value', 'Unavailable')
        );
        reading.append(
            valueBlock,
            element('div', 'device-reading-note', 'The current firmware has no inverter telemetry register mapping. Command results must not be treated as measured power or inverter availability.')
        );

        const meta = element('div', 'device-meta-grid');
        const commandPower = runtime.has_command ? utils.formatPower(runtime.commanded_power_kw) : 'Never commanded';
        const commandPercent = runtime.has_command ? utils.formatPercent(runtime.commanded_percent) : 'Never commanded';
        const errorLabel = !runtime.has_command || Number(runtime.last_error) === 0
            ? 'None'
            : runtime.last_error_name || `Error ${runtime.last_error}`;
        meta.append(
            metaItem('Endpoint', `${utils.endpointLabel(inverter.endpoint)} · Unit ${inverter.endpoint?.unit_id ?? '--'}`),
            metaItem('Rated power', utils.formatPower(inverter.rated_kw)),
            metaItem('Limit register', `FC${command.function ?? '--'} · PDU ${command.limit_pdu_address ?? '--'}`),
            metaItem('Allowed range', `${command.minimum_percent ?? '--'}–${command.maximum_percent ?? '--'}%`),
            metaItem('Last command', runtime.has_command ? utils.formatAge(runtime.last_command_age_ms) : 'Never'),
            metaItem('Commanded setpoint', `${commandPower} · ${commandPercent}`),
            metaItem('Write results', `${runtime.write_successes ?? 0} OK · ${runtime.write_errors ?? 0} failed`),
            metaItem('Last write error', errorLabel)
        );
        card.append(top, reading, meta);
        return card;
    }

    function renderMeters() {
        const data = state.meters;
        const list = byId('meterRuntimeList');
        if (!list) return;
        list.replaceChildren();
        if (!data || !Array.isArray(data.meters)) {
            list.append(emptyState('Meter runtime diagnostics are unavailable.', true));
            return;
        }
        setText('meterConfiguredCount', data.configured_count ?? data.meters.length);
        setText('meterEnabledCount', data.summary?.enabled ?? 0);
        setText('meterOnlineCount', data.summary?.online ?? 0);
        setText('meterStaleCount', data.summary?.stale_or_unavailable ?? 0);
        if (!data.meters.length) list.append(emptyState('No meter profiles are configured.'));
        else data.meters.forEach((meter) => list.append(meterCard(meter)));
    }

    function renderInverters() {
        const data = state.inverters;
        const list = byId('inverterRuntimeList');
        if (!list) return;
        list.replaceChildren();
        if (!data || !Array.isArray(data.inverters)) {
            list.append(emptyState('Inverter runtime diagnostics are unavailable.', true));
            return;
        }
        setText('inverterConfiguredCount', data.configured_count ?? data.inverters.length);
        setText('inverterEnabledCount', data.summary?.enabled ?? 0);
        setText('inverterRatedTotal', utils.formatPower(data.summary?.enabled_rated_kw));
        setText('inverterCommandTested', data.summary?.command_tested ?? 0);
        if (!data.inverters.length) list.append(emptyState('No inverter profiles are configured.'));
        else data.inverters.forEach((inverter) => list.append(inverterCard(inverter)));
    }

    function setLoading(loading) {
        ['meterTelemetryRefresh', 'inverterTelemetryRefresh'].forEach((id) => {
            const button = byId(id);
            if (button) {
                button.disabled = loading;
                button.textContent = loading ? 'Refreshing…' : 'Refresh diagnostics';
            }
        });
    }

    async function api(path) {
        const response = await fetch(path, { cache: 'no-store' });
        const text = await response.text();
        if (!response.ok) throw new Error(text || `${response.status} ${response.statusText}`);
        return JSON.parse(text);
    }

    function currentRoute() {
        return window.location.hash.replace(/^#\/?/, '') || 'dashboard';
    }

    function errorMessage(reason) {
        return reason && reason.message ? reason.message : String(reason || 'Unknown error');
    }

    async function refresh(force = false) {
        const route = currentRoute();
        if (state.loading || (!force && !['dashboard', 'meters', 'inverters'].includes(route))) return;
        state.loading = true;
        setLoading(true);

        try {
            if (route === 'dashboard') {
                state.telemetry = await api('/api/telemetry');
                state.lastUpdated = new Date();
                renderDashboard();
            } else if (route === 'meters') {
                setText('meterTelemetryMessage', 'Refreshing meter diagnostics…');
                state.meters = await api('/api/meters');
                state.lastUpdated = new Date();
                renderMeters();
                setText('meterTelemetryMessage', `Runtime diagnostics updated ${state.lastUpdated.toLocaleTimeString()}`);
            } else if (route === 'inverters') {
                setText('inverterTelemetryMessage', 'Refreshing inverter diagnostics…');
                state.inverters = await api('/api/inverters');
                state.lastUpdated = new Date();
                renderInverters();
                setText('inverterTelemetryMessage', `Runtime diagnostics updated ${state.lastUpdated.toLocaleTimeString()}`);
            }
        } catch (error) {
            if (route === 'dashboard') {
                state.telemetry = null;
                renderDashboard();
            } else if (route === 'meters') {
                state.meters = null;
                renderMeters();
                setText('meterTelemetryMessage', `Meter diagnostics failed: ${errorMessage(error)}`);
            } else if (route === 'inverters') {
                state.inverters = null;
                renderInverters();
                setText('inverterTelemetryMessage', `Inverter diagnostics failed: ${errorMessage(error)}`);
            }
        } finally {
            state.loading = false;
            setLoading(false);
        }
    }

    function bind() {
        byId('meterTelemetryRefresh')?.addEventListener('click', () => refresh(true));
        byId('inverterTelemetryRefresh')?.addEventListener('click', () => refresh(true));
        window.addEventListener('hashchange', () => refresh(false));
        window.setInterval(() => refresh(false), 5000);
    }

    function start() {
        ensureScaffold();
        bind();
        refresh(false);
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();
(() => {
    'use strict';

    function bindGlobalRefresh() {
        const button = document.getElementById('refreshButton');
        if (!button) return;

        /* app.js already refreshes the common controller status. Re-dispatching
         * the route event lets the read-only diagnostics module refresh the
         * active Dashboard, Meters or Inverters endpoint at the same time. */
        button.addEventListener('click', () => {
            window.dispatchEvent(new Event('hashchange'));
        });
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', bindGlobalRefresh, { once: true });
    } else {
        bindGlobalRefresh();
    }
})();
