(() => {
    'use strict';

    const utils = window.WifiUtils;
    if (!utils) return;

    const SCAN_POLL_INTERVAL_MS = 800;
    const SCAN_POLL_DEADLINE_MS = 30000;
    const DEFAULT_API_TIMEOUT_MS = 5000;

    const state = {
        initialWifi: null,
        scanPollTimer: null,
        scanPollStartedAt: 0,
        scanController: null,
        scanSequence: 0,
        selected: { primary: null, fallback: null },
        saving: false
    };

    const byId = (id) => document.getElementById(id);

    /* The engineering network form used to have its own route, #/wifi, and its
     * own sidebar entry beside the operator's "Wi-Fi network" -- two entries that
     * read as the same thing. The form now lives inside the network page, gated,
     * so this is the route to recognise. Renaming this predicate rather than
     * leaving it pointed at a route that no longer exists: this module would have
     * concluded it was never on screen and stopped refreshing, and the fields
     * would sit at whatever they held when the page was built. */
    function isWifiRoute() {
        return window.location.hash.replace(/^#\/?/, '').split(/[?&]/, 1)[0] === 'network';
    }

    const access = () => window.AutomatrixEngineeringAccess;

    /* /api/wifi/scan is Engineering-only. Asking for it from the operator
     * dashboard can only produce a 401, and each of those still consumes one of
     * the controller's few client sockets. */
    function scanScopeAllowed() {
        return Boolean(access()?.mayRequest('/api/wifi/scan'));
    }

    async function api(path, options = {}) {
        const {
            timeoutMs = DEFAULT_API_TIMEOUT_MS,
            signal: externalSignal,
            ...fetchOptions
        } = options;
        const controller = new AbortController();
        const abort = () => controller.abort();
        if (externalSignal) {
            if (externalSignal.aborted) controller.abort();
            else externalSignal.addEventListener('abort', abort, { once: true });
        }
        const timer = window.setTimeout(() => controller.abort(), timeoutMs);
        try {
            const response = await fetch(path, {
                cache: 'no-store',
                credentials: 'same-origin',
                ...fetchOptions,
                signal: controller.signal
            });
            const text = await response.text();
            let payload = null;
            if (text) {
                try { payload = JSON.parse(text); }
                catch (error) { payload = { error: text }; }
            }
            if (!response.ok) {
                throw new Error(payload && payload.error
                    ? payload.error
                    : text || `${response.status} ${response.statusText}`);
            }
            return payload;
        } catch (error) {
            if (error?.name === 'AbortError') {
                throw new Error(`Wi-Fi request timed out after ${Math.ceil(timeoutMs / 1000)}s`);
            }
            throw error;
        } finally {
            window.clearTimeout(timer);
            externalSignal?.removeEventListener?.('abort', abort);
        }
    }

    /* Mounted inside the engineering block on the network page, not beside the
     * operator panel above it. That panel already scans and joins; this survey
     * earns its place by showing what that one deliberately does not -- the
     * security mode of each network, and whether this radio can join it -- so it
     * belongs at the engineering depth rather than as a second search button in
     * front of a plant owner. */
    function installScanPanel() {
        const host = byId('engineeringNetworkBody');
        if (!host || byId('wifiScanPanel')) return;

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
        host.prepend(panel);
    }

    function setScanMessage(message, tone = '') {
        const target = byId('wifiScanMessage');
        if (!target) return;
        target.textContent = message || '';
        target.className = `wifi-scan-message${tone ? ` ${tone}` : ''}`;
    }

    function setScanBadge(label, tone = '') {
        const target = byId('wifiScanState');
        if (!target) return;
        target.textContent = label;
        target.className = `subtle-badge${tone ? ` ${tone}` : ''}`;
    }

    function securityBadge(record) {
        const info = utils.authInfo(record.auth_mode);
        const badge = document.createElement('span');
        badge.className = `network-security${info.supported ? '' : ' unsupported'}`;
        badge.textContent = info.label;
        return badge;
    }

    function tag(label, tone = '') {
        const target = document.createElement('span');
        target.className = `network-tag${tone ? ` ${tone}` : ''}`;
        target.textContent = label;
        return target;
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
            empty.textContent = snapshot.state === 'failed'
                ? 'The scan failed. Existing connection profiles were not changed.'
                : 'No visible Wi-Fi networks were found.';
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
                const action = document.createElement('button');
                action.type = 'button';
                action.className = 'button secondary compact-button';
                action.textContent = role === 'primary' ? 'Use as primary' : 'Use as fallback';
                action.disabled = !info.supported;
                if (!info.supported) {
                    action.title = 'This security mode is not supported by the current commissioning form.';
                }
                action.addEventListener('click', () => selectNetwork(role, record));
                actions.appendChild(action);
            }

            row.append(identity, signal, actions);
            container.appendChild(row);
        });
    }

    function scanStateName(value) {
        return ['idle', 'running', 'complete', 'failed'][Number(value)] || 'unknown';
    }

    function cancelScanPolling({ resetDeadline = false } = {}) {
        window.clearTimeout(state.scanPollTimer);
        state.scanPollTimer = null;
        state.scanController?.abort();
        state.scanController = null;
        state.scanSequence++;
        if (resetDeadline) state.scanPollStartedAt = 0;
    }

    function scheduleScanPoll() {
        window.clearTimeout(state.scanPollTimer);
        state.scanPollTimer = null;
        if (!isWifiRoute() || document.hidden) return;
        if (!state.scanPollStartedAt) state.scanPollStartedAt = Date.now();
        if (Date.now() - state.scanPollStartedAt >= SCAN_POLL_DEADLINE_MS) {
            setScanBadge('Scan delayed', 'warning');
            setScanMessage('The scan is still running after 30 seconds. Polling stopped; press Scan networks to check again.', 'warning');
            state.scanPollStartedAt = 0;
            return;
        }
        state.scanPollTimer = window.setTimeout(
            () => loadScanSnapshot({ automatic: true }),
            SCAN_POLL_INTERVAL_MS
        );
    }

    async function loadScanSnapshot({ automatic = false } = {}) {
        if (automatic && (!isWifiRoute() || document.hidden)) return;
        if (!scanScopeAllowed()) return;
        state.scanController?.abort();
        const controller = new AbortController();
        state.scanController = controller;
        const sequence = ++state.scanSequence;
        try {
            const snapshot = await api('/api/wifi/scan', {
                signal: controller.signal,
                timeoutMs: 4000
            });
            if (sequence !== state.scanSequence) return;
            const name = scanStateName(snapshot.state);
            snapshot.state = name;
            renderNetworks(snapshot);

            if (name === 'running') {
                setScanBadge('Scanning…', 'warning');
                setScanMessage('The controller is surveying nearby access points. The active profile is unchanged.');
                scheduleScanPoll();
            } else if (name === 'complete') {
                cancelScanPolling({ resetDeadline: true });
                setScanBadge(`${snapshot.networks.length} found`, 'good');
                setScanMessage('Scan completed. Results are sorted by signal strength.', 'good');
            } else if (name === 'failed') {
                cancelScanPolling({ resetDeadline: true });
                setScanBadge('Scan failed', 'bad');
                setScanMessage(snapshot.error_name
                    ? `Scan failed: ${snapshot.error_name}`
                    : 'The Wi-Fi scan failed.', 'bad');
            } else {
                cancelScanPolling({ resetDeadline: true });
                setScanBadge('Not scanned');
            }
        } catch (error) {
            if (sequence !== state.scanSequence) return;
            setScanBadge('Unavailable', 'bad');
            setScanMessage(`Unable to load scan results: ${error.message}`, 'bad');
        } finally {
            if (sequence === state.scanSequence) state.scanController = null;
        }
    }

    async function requestScan() {
        if (!scanScopeAllowed()) {
            setScanBadge('Engineering required', 'warning');
            setScanMessage('Unlock Engineering on this page before starting a radio scan.', 'warning');
            return;
        }
        const button = byId('wifiScanButton');
        if (button) button.disabled = true;
        cancelScanPolling({ resetDeadline: true });
        state.scanPollStartedAt = Date.now();
        setScanBadge('Starting…', 'warning');
        setScanMessage('Requesting a non-disruptive radio scan…');
        try {
            await api('/api/wifi/scan', { method: 'POST', timeoutMs: 5000 });
            await loadScanSnapshot();
        } catch (error) {
            cancelScanPolling({ resetDeadline: true });
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
        document.querySelectorAll('.wifi-commissioning-error').forEach((target) => target.remove());
        document.querySelectorAll('.wifi-commissioning-invalid').forEach((target) => target.classList.remove('wifi-commissioning-invalid'));
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
        if (profile.ssid.length > 32) {
            fieldError(`${prefix}Ssid`, 'SSID must contain no more than 32 characters.');
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
        const selectedInfo = selected && selected.ssid === profile.ssid
            ? utils.authInfo(selected.authMode)
            : null;
        if (profile.enabled && selectedInfo && selectedInfo.secure && original &&
            original.ssid !== profile.ssid && !profile.password) {
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

        // The recovery access point is not optional. The firmware refuses to persist
        // a configuration that would switch it off, so this always reports true and
        // the control is shown checked and disabled.
        const recoveryEnabled = true;
        const recoverySsid = byId('recoverySsid').value.trim();
        const recoveryPassword = byId('recoveryPassword').value;
        if (recoveryEnabled && !recoverySsid) {
            fieldError('recoverySsid', 'Recovery AP SSID is required when enabled.');
            valid = false;
        }
        if (recoverySsid.length > 32) {
            fieldError('recoverySsid', 'Recovery AP SSID must contain no more than 32 characters.');
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

            const primaryChanged = !state.initialWifi ||
                payload.primary.ssid !== (state.initialWifi.primary || {}).ssid;
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
                body: JSON.stringify(payload),
                timeoutMs: 8000
            });
            if (message) message.textContent = 'Saved. Restarting controller…';
            await api('/api/system/restart', { method: 'POST', timeoutMs: 5000 });
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
            const config = await api('/api/config', { timeoutMs: 5000 });
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

    /* Separate so the hashchange path can bind a button that did not exist when
     * bind() ran. Guarded by a dataset flag rather than by removeEventListener,
     * which would need the same function reference and gains nothing here. */
    function bindScanButton() {
        const scanButton = byId('wifiScanButton');
        if (!scanButton || scanButton.dataset.bound === 'true') return;
        scanButton.dataset.bound = 'true';
        scanButton.addEventListener('click', requestScan);
    }

    function bind() {
        installScanPanel();
        bindScanButton();

        const form = byId('wifiForm');
        if (form) form.addEventListener('submit', handleWifiSubmit, true);

        const reconnect = byId('wifiRescanButton');
        if (reconnect) {
            reconnect.textContent = 'Reconnect saved profiles';
            reconnect.addEventListener('click', protectReconnectAction, true);
        }

        for (const role of ['primary', 'fallback']) {
            byId(`${role}Ssid`)?.addEventListener('input', () => { state.selected[role] = null; });
        }

        window.addEventListener('hashchange', () => {
            if (!isWifiRoute()) cancelScanPolling();
        });
        /*
         * MOUNTED WHEN THE HOST APPEARS, NOT WHEN THIS MODULE LOADS.
         *
         * The block this survey lives in is built by operator-network.js the
         * first time the network page is visited -- after bind() has run, and
         * after the hashchange that took the user there. Both of those fire too
         * early and found nothing, so the survey never appeared.
         *
         * The shared observer on #mainContent is exactly the signal for this:
         * it reports when another module has changed the page. Installation is
         * idempotent, so being called on unrelated changes costs one lookup.
         */
        access()?.onContentChange(() => {
            if (!isWifiRoute()) return;
            installScanPanel();
            bindScanButton();
        });
        /* Leaving the route stops polling above; entering it - or signing in
         * while already on it - loads the survey through the shared scope hook,
         * so the data appears after login without a manual refresh. */
        access()?.onScopeChange(() => { if (isWifiRoute()) loadScanSnapshot(); });
        document.addEventListener('visibilitychange', () => {
            if (document.hidden) cancelScanPolling();
            else if (isWifiRoute()) loadScanSnapshot();
        });
        window.addEventListener('beforeunload', () => cancelScanPolling({ resetDeadline: true }));
        window.addEventListener('resize', compactControllerPill);

        const pill = byId('controllerPill');
        if (pill) new MutationObserver(compactControllerPill).observe(pill, { childList: true });
        compactControllerPill();
    }

    async function start() {
        bind();
        const tasks = [loadInitialWifi()];
        if (isWifiRoute() && !document.hidden) tasks.push(loadScanSnapshot());
        await Promise.allSettled(tasks);
    }

    start().catch((error) => setScanMessage(`Wi-Fi commissioning failed to initialize: ${error.message}`, 'bad'));
})();
