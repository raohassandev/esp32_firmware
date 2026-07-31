(() => {
    'use strict';

    const state = { saving: false, baseline: null };
    const byId = (id) => document.getElementById(id);

    async function api(path, options = {}) {
        const response = await fetch(path, { cache: 'no-store', credentials: 'same-origin', ...options });
        const text = await response.text();
        let payload = null;
        if (text) {
            try { payload = JSON.parse(text); }
            catch { payload = { error: text }; }
        }
        if (!response.ok) throw new Error(payload?.error || text || `${response.status} ${response.statusText}`);
        return payload;
    }

    async function ensureEngineeringSession() {
        const response = await fetch('/api/engineering/session', {
            cache: 'no-store',
            credentials: 'same-origin'
        });
        const session = await response.json().catch(() => ({}));
        if (!response.ok || session.authenticated !== true) {
            throw new Error('Engineering access is not active. Open Engineering and unlock access before changing the network.');
        }
        return session;
    }

    function message(text, tone = '') {
        const target = byId('wifiMessage');
        if (!target) return;
        target.textContent = text;
        target.className = `action-message${tone ? ` ${tone}` : ''}`;
    }

    function setBusy(busy) {
        state.saving = busy;
        const form = byId('wifiForm');
        form?.querySelectorAll('button, input, select').forEach((node) => {
            if (node.id !== 'wifiScanButton') node.disabled = busy;
        });
    }

    function value(id) { return byId(id)?.value?.trim() || ''; }
    function checked(id) { return Boolean(byId(id)?.checked); }

    function profile(prefix) {
        return {
            enabled: checked(`${prefix}Enabled`),
            ssid: value(`${prefix}Ssid`),
            password: byId(`${prefix}Password`)?.value || '',
            ip_mode: Number(byId(`${prefix}Mode`)?.value || 0),
            static_ip: value(`${prefix}Ip`),
            gateway: value(`${prefix}Gateway`),
            netmask: value(`${prefix}Netmask`),
            dns1: value(`${prefix}Dns1`),
            dns2: value(`${prefix}Dns2`)
        };
    }

    function payload() {
        const primary = profile('primary');
        const fallback = profile('fallback');
        if (!primary.enabled || !primary.ssid) throw new Error('Select and enable a primary Wi-Fi network.');
        if (fallback.enabled && !fallback.ssid) throw new Error('Enter the fallback network SSID or disable the fallback profile.');
        if (fallback.enabled && fallback.ssid === primary.ssid) throw new Error('Primary and fallback networks must be different.');
        for (const item of [primary, fallback]) {
            if (item.password && (item.password.length < 8 || item.password.length > 64)) {
                throw new Error('Wi-Fi passwords must contain 8–64 characters.');
            }
        }
        const recoveryPassword = byId('recoveryPassword')?.value || '';
        if (recoveryPassword && (recoveryPassword.length < 8 || recoveryPassword.length > 64)) {
            throw new Error('Recovery AP password must contain 8–64 characters.');
        }
        const retries = Number(byId('wifiRetries')?.value);
        const backoff = Number(byId('wifiBackoff')?.value);
        if (!Number.isInteger(retries) || retries < 1 || retries > 20) throw new Error('Retries must be between 1 and 20.');
        if (!Number.isInteger(backoff) || backoff < 500 || backoff > 60000) throw new Error('Reconnect delay must be 500–60000 ms.');
        return {
            primary,
            fallback,
            scan_before_connect: checked('scanBeforeConnect'),
            // Always on; the firmware rejects a request that would disable it.
            fallback_ap_enabled: true,
            fallback_ap_ssid: value('recoverySsid'),
            fallback_ap_password: recoveryPassword,
            max_retries_per_profile: retries,
            reconnect_backoff_ms: backoff
        };
    }

    async function waitForController(oldIp) {
        const started = Date.now();
        while (Date.now() - started < 45000) {
            await new Promise((resolve) => setTimeout(resolve, 1800));
            try {
                const status = await api('/api/status');
                if (status) {
                    await ensureEngineeringSession();
                    return status;
                }
            } catch {}
        }
        throw new Error(`The settings were saved, but this browser could not rediscover the controller. Reconnect to the configured Wi-Fi or recovery AP and open ${oldIp || 'the controller address'} again.`);
    }

    async function save(event) {
        event.preventDefault();
        event.stopImmediatePropagation();
        if (state.saving) return;
        let saved = false;
        try {
            setBusy(true);
            message('Checking Engineering access…');
            await ensureEngineeringSession();
            message('Validating and saving Wi-Fi settings…');
            const next = payload();
            const oldIp = location.host;
            await api('/api/wifi/config', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(next)
            });
            saved = true;
            message('Settings saved. Restarting the controller…', 'good');
            try {
                await api('/api/system/restart', { method: 'POST' });
            } catch {
                /* A dropped response is normal when the radio restarts. */
            }
            message('Controller is restarting and changing network. Keep this page open while it reconnects…', 'good');
            const status = await waitForController(oldIp);
            const detail = status?.ip ? ` New address: ${status.ip}.` : '';
            message(`Wi-Fi connection restored and Engineering access renewed.${detail}`, 'good');
            state.baseline = next;
        } catch (error) {
            message(saved ? error.message : `Wi-Fi settings were not saved: ${error.message}`, saved ? 'warning' : 'bad');
        } finally {
            setBusy(false);
        }
    }

    const access = () => window.AutomatrixEngineeringAccess;

    async function loadBaseline() {
        /* Only the Wi-Fi commissioning form uses this baseline. Probing the
         * session and the configuration from every other route wasted two
         * requests per page load against a four-socket server. */
        if (!access()?.mayUseEngineering('wifi', 'commissioning')) return;
        try {
            await ensureEngineeringSession();
            const config = await api('/api/config');
            state.baseline = config?.wifi || null;
        } catch {}
    }

    document.addEventListener('submit', (event) => {
        if (event.target?.id === 'wifiForm') save(event);
    }, true);

    document.addEventListener('DOMContentLoaded', () => {
        loadBaseline();
        access()?.onScopeChange(loadBaseline);
        const form = byId('wifiForm');
        if (form) form.dataset.networkFlow = 'resilient';
    });
})();