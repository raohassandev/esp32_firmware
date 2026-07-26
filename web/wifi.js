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
