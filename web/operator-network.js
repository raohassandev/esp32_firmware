/* operator-network.js - the site owner's own Wi-Fi page.
 *
 * OWNS: the #/network route and its page. Nothing else.
 * DOES NOT OWN: routing (app.js), navigation order (product-shell-v2.js), or
 *   any engineering network setting - those stay on #/wifi behind a session.
 *
 * WHY THIS EXISTS SEPARATELY FROM #/wifi. The engineering network page asks an
 * engineer's questions: static addressing, the recovery access point, retry
 * timing, a fallback profile. A factory owner who has changed their router does
 * not have those questions or that password, and making them wait for a site
 * visit to retype one passphrase is the whole complaint this answers.
 *
 * So this page can do exactly two things - list what is in range, and join one
 * with a passphrase - against two endpoints that can do exactly those two
 * things. What it cannot touch is what keeps a controller findable: the recovery
 * AP, static addressing, the fallback profile. A unit can be moved from here; it
 * cannot be locked away.
 */
(() => {
    'use strict';

    const byId = (id) => document.getElementById(id);
    const node = (tag, className = '', text = null) => {
        const item = document.createElement(tag);
        if (className) item.className = className;
        if (text != null) item.textContent = String(text);
        return item;
    };
    const route = () => location.hash.replace(/^#\/?/, '') || 'dashboard';

    const state = { networks: [], busy: false, message: '', ok: false, selected: '' };

    async function api(path, options = {}) {
        const response = await fetch(path, { cache: 'no-store', credentials: 'same-origin', ...options });
        const payload = await response.json().catch(() => ({}));
        if (!response.ok) throw new Error(payload.error || `${response.status} ${response.statusText}`);
        return payload;
    }

    /* Signal as words, not as dBm. -60 means nothing to the reader this page is
     * for, and a number they cannot interpret invites them to worry about it. */
    function signalWord(rssi) {
        const value = Number(rssi);
        if (!Number.isFinite(value)) return 'unknown';
        if (value >= -55) return 'strong';
        if (value >= -70) return 'good';
        if (value >= -80) return 'weak';
        return 'very weak';
    }

    function ensurePage() {
        const main = byId('mainContent');
        if (!main) return null;
        let page = main.querySelector('[data-page="network"]');
        if (page) return page;
        page = node('section', 'page');
        page.dataset.page = 'network';
        page.innerHTML = `
            <article class="panel form-panel" id="operatorNetworkPanel">
                <div class="panel-header">
                    <div><p class="eyebrow">Connection</p><h3>Wi-Fi network</h3></div>
                    <span class="subtle-badge" id="operatorNetworkBadge">Checking</span>
                </div>
                <dl class="derived-fact" id="operatorNetworkNow">
                    <dt>Connected to</dt><dd id="operatorNetworkSsid">--</dd>
                    <dt>Address on the network</dt><dd id="operatorNetworkIp">--</dd>
                    <dt>Signal</dt><dd id="operatorNetworkSignal">--</dd>
                </dl>
                <!-- Stated on the page rather than in a manual: it is the one
                     fact that makes changing the network safe to attempt. -->
                <div class="notice safe">
                    <strong>You cannot lose the controller by changing this.</strong>
                    <span id="operatorNetworkRecovery">If the new network does not work, the controller's own setup network stays available.</span>
                </div>
                <div class="field-grid">
                    <label class="field"><span>Network name</span><input id="operatorNetworkPick" list="operatorNetworkList" autocomplete="off" placeholder="Choose or type a name"></label>
                    <label class="field"><span>Wi-Fi password</span><input id="operatorNetworkPassword" type="password" autocomplete="new-password" placeholder="Leave empty for an open network"></label>
                </div>
                <datalist id="operatorNetworkList"></datalist>
                <div class="device-readiness-note" id="operatorNetworkScanNote" role="status">Looking for networks…</div>
                <div class="panel-actions">
                    <button class="button secondary" id="operatorNetworkRescan" type="button">Search again</button>
                    <button class="button primary" id="operatorNetworkJoin" type="button">Save and restart</button>
                </div>
                <div class="action-message" id="operatorNetworkMessage" role="status"></div>
            </article>`;
        main.append(page);
        byId('operatorNetworkRescan').addEventListener('click', () => { loadScan(true); });
        byId('operatorNetworkJoin').addEventListener('click', join);
        return page;
    }

    function renderStatus(status) {
        const ssid = byId('operatorNetworkSsid');
        if (!ssid) return;
        const online = Boolean(status && status.network_online);
        ssid.textContent = (status && status.ssid) || 'not connected';
        byId('operatorNetworkIp').textContent = (status && status.ip) || '--';
        /* "unknown" rather than a number when there is no association: a signal
         * strength for a network you are not on is not a measurement. */
        byId('operatorNetworkSignal').textContent = online ? signalWord(status.rssi) : 'not connected';
        const badge = byId('operatorNetworkBadge');
        badge.textContent = online ? 'Connected' : 'Not connected';
        badge.className = `subtle-badge${online ? ' good' : ' warning'}`;
        const recovery = byId('operatorNetworkRecovery');
        if (recovery && status && status.recovery_ap_ssid) {
            recovery.textContent = `If the new network does not work, join "${status.recovery_ap_ssid}" from a phone and set it again.`;
        }
    }

    function renderScan() {
        const list = byId('operatorNetworkList');
        const note = byId('operatorNetworkScanNote');
        if (!list || !note) return;
        list.replaceChildren();
        /* Strongest first: the reader is choosing the network they are standing
         * in, and it is almost always the loudest one. */
        const sorted = [...state.networks].sort((a, b) => Number(b.rssi) - Number(a.rssi));
        for (const network of sorted) {
            if (!network.ssid) continue;
            const option = document.createElement('option');
            option.value = network.ssid;
            option.label = `${network.ssid} — ${signalWord(network.rssi)}`;
            list.append(option);
        }
        note.textContent = sorted.length
            ? `${sorted.length} network(s) in range. Choose one, or type a name if it is hidden.`
            : 'No networks found yet. Press "Search again", or type the name if it is hidden.';
    }

    async function loadScan(announce) {
        if (announce) setMessage('Searching…', true);
        try {
            const payload = await api('/api/network/scan');
            state.networks = Array.isArray(payload.networks) ? payload.networks : [];
        } catch (error) {
            state.networks = [];
            setMessage(`Could not search for networks: ${error.message}`, false);
        }
        renderScan();
    }

    function setMessage(text, ok) {
        const target = byId('operatorNetworkMessage');
        if (!target) return;
        target.textContent = text;
        target.className = `action-message${ok ? ' good' : text ? ' bad' : ''}`;
    }

    async function join() {
        if (state.busy) return;
        const ssid = (byId('operatorNetworkPick')?.value || '').trim();
        const password = byId('operatorNetworkPassword')?.value || '';
        if (!ssid) { setMessage('Choose a network first.', false); return; }
        /* Checked here as well as in the firmware so the reader is told before
         * the controller restarts rather than after it fails to connect. */
        if (password.length > 0 && password.length < 8) {
            setMessage('A Wi-Fi password is at least 8 characters. Leave it empty only for an open network.', false);
            return;
        }
        if (!window.confirm(`Connect this controller to "${ssid}"?\n\nIt restarts to join. If it cannot, its own setup network stays available.`)) return;

        state.busy = true;
        setMessage('Saving…', true);
        try {
            const saved = await api('/api/network/join', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ ssid, password })
            });
            setMessage(saved.notice || 'Saved. Restart the controller to join this network.', true);
            if (saved.restart_required && window.confirm('Restart now to join the network?')) {
                await api('/api/system/restart', { method: 'POST' });
                setMessage('Restarting. Reopen this page in about twenty seconds — on the new network.', true);
            }
        } catch (error) {
            setMessage(`Could not save: ${error.message}`, false);
        }
        state.busy = false;
    }

    async function refresh() {
        if (route() !== 'network') return;
        if (!ensurePage()) return;
        try { renderStatus(await api('/api/status')); } catch { renderStatus(null); }
    }

    function onRoute() {
        if (route() !== 'network') return;
        ensurePage();
        refresh();
        loadScan(false);
    }

    function start() {
        onRoute();
        window.addEventListener('hashchange', onRoute);
        /* Status only, and only while the page is open. The scan is not polled:
         * it costs a radio sweep and the reader asks for it by pressing a
         * button. */
        window.setInterval(refresh, 5000);
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();
