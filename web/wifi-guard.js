(() => {
    'use strict';

    const state = {
        baseline: null,
        networks: []
    };

    const byId = (id) => document.getElementById(id);
    const access = () => window.AutomatrixEngineeringAccess;

    async function loadContext() {
        /* This baseline is only ever consulted while validating the Wi-Fi form,
         * so fetching it from any other route is pure waste on a socket-starved
         * controller - and the scan endpoint answers 401 without Engineering. */
        if (!access()?.mayRequest('/api/wifi/scan')) return;
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

        // Always on; see wifi.js.
        const recoveryEnabled = true;
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
        access()?.onScopeChange(loadContext);
        const scanButton = byId('wifiScanButton');
        if (scanButton) scanButton.addEventListener('click', () => window.setTimeout(loadContext, 1500));
    }

    bind();
    loadContext();
})();
