(() => {
    'use strict';

    const REFRESH_MS = 15000;
    const REQUEST_TIMEOUT_MS = 5000;
    const state = { payload: null, busy: false, timer: null, controller: null };
    const byId = (id) => document.getElementById(id);
    const route = () => location.hash.replace(/^#\/?/, '') || 'dashboard';
    const active = () => !document.hidden && route() === 'readiness';
    const node = (tag, className = '', text = '') => {
        const element = document.createElement(tag);
        if (className) element.className = className;
        if (text) element.textContent = text;
        return element;
    };

    async function api(path, signal) {
        const response = await fetch(path, { cache: 'no-store', credentials: 'same-origin', signal });
        const payload = await response.json().catch(() => ({}));
        if (!response.ok) throw new Error(payload.error || `HTTP ${response.status}`);
        return payload;
    }

    function ensurePage() {
        const nav = document.querySelector('.nav-list');
        if (nav && !nav.querySelector('[data-route="readiness"]')) {
            const link = document.createElement('a');
            link.className = 'nav-link';
            link.href = '#/readiness';
            link.dataset.route = 'readiness';
            link.innerHTML = '<span aria-hidden="true">✓</span><span>Readiness</span><b class="prelab-nav-state" id="prelabNavState">—</b>';
            const engineering = nav.querySelector('#engineeringNav');
            nav.insertBefore(link, engineering || null);
        }
        const main = byId('mainContent');
        if (main && !main.querySelector('[data-page="readiness"]')) {
            const page = node('section', 'page prelab-page');
            page.dataset.page = 'readiness';
            page.innerHTML = '<div class="operator-product-view" id="prelabReadinessView"></div>';
            main.append(page);
        }
    }

    function activateRoute() {
        if (route() !== 'readiness') return;
        document.querySelectorAll('.page').forEach((page) => page.classList.toggle('active', page.dataset.page === 'readiness'));
        document.querySelectorAll('.nav-link').forEach((link) => link.classList.toggle('active', link.dataset.route === 'readiness'));
        if (byId('pageTitle')) byId('pageTitle').textContent = 'Pre-Lab Readiness';
        if (byId('breadcrumbCurrent')) byId('breadcrumbCurrent').textContent = 'Release checks';
        document.title = 'Pre-Lab Readiness · Automatrix PV-DG';
    }

    function check(id, label, status, detail, action = '') { return { id, label, status, detail, action }; }

    function evaluate(payload) {
        const status = payload.status || {};
        const meters = payload.meters || {};
        const inverters = payload.inverters || {};
        const history = payload.history || {};
        const events = payload.events || {};
        const session = payload.session || {};
        const meterSummary = meters.summary || {};
        const inverterSummary = inverters.summary || {};
        const samples = Array.isArray(history.samples) ? history.samples : [];
        const alarmNames = Array.isArray(status.alarm_names) ? status.alarm_names : [];
        const activeCritical = Number(events.summary?.active_critical) || 0;
        const activeWarning = Number(events.summary?.active_warning) || 0;
        const authBypass = session.temporary_field_bypass === true;
        return [
            check('controller', 'Controller API', status && Object.keys(status).length ? 'pass' : 'block', status && Object.keys(status).length ? 'Controller API is responding.' : 'Controller status API is unavailable.', 'Check firmware boot and HTTP server.'),
            check('network', 'Primary network', status.network_online ? 'pass' : status.fallback_ap_active ? 'warn' : 'block', status.network_online ? `Connected to ${status.ssid || 'configured network'} at ${status.ip || 'an assigned address'}.` : status.fallback_ap_active ? 'Recovery access point is active; primary Wi-Fi is not connected.' : 'No active network connection.', 'Confirm the configured Wi-Fi credentials and coverage.'),
            check('meter', 'Grid measurement', Number(meterSummary.online) > 0 && !status.meter_stale ? 'pass' : Number(meterSummary.enabled) > 0 ? 'block' : 'warn', Number(meterSummary.online) > 0 && !status.meter_stale ? `${meterSummary.online} meter channel is online with fresh data.` : Number(meterSummary.enabled) > 0 ? 'An enabled meter is not providing fresh data.' : 'No meter channel is enabled.', 'Verify meter power, TCP path, unit ID, sign and scale.'),
            check('solar', 'Solar fleet', Number(inverterSummary.enabled) === 0 ? 'warn' : Number(inverterSummary.online) === Number(inverterSummary.enabled) ? 'pass' : Number(inverterSummary.online) > 0 ? 'warn' : 'block', Number(inverterSummary.enabled) === 0 ? 'No inverter is enabled yet.' : `${Number(inverterSummary.online) || 0} of ${Number(inverterSummary.enabled) || 0} enabled inverters are online.`, 'Review model assignment, IP path and read-only qualification.'),
            check('history', 'Operational history', samples.length >= 3 ? 'pass' : 'warn', samples.length >= 3 ? `${samples.length} controller-resident samples are available.` : 'History is still collecting initial samples.', 'Leave the controller running for at least 30 seconds.'),
            check('alarms', 'Active alarms', activeCritical > 0 ? 'block' : activeWarning > 0 || alarmNames.length > 0 ? 'warn' : 'pass', activeCritical > 0 ? `${activeCritical} critical condition(s) are active.` : activeWarning > 0 || alarmNames.length > 0 ? `${activeWarning + alarmNames.length} warning/alarm indication(s) require review.` : 'No active critical or warning condition.', 'Open Alarms and resolve the root cause before control testing.'),
            check('control', 'Automatic control safety', status.control_enabled ? 'block' : 'pass', status.control_enabled ? 'Automatic control is enabled during pre-lab validation.' : 'Automatic control is locked in monitoring-only mode.', 'Keep automatic control disabled until physical qualification is complete.'),
            check('writes', 'Physical inverter writes', Number(inverterSummary.commandable_rated_kw) > 0 ? 'warn' : 'pass', Number(inverterSummary.commandable_rated_kw) > 0 ? `${inverterSummary.commandable_rated_kw} kW is reported commandable; confirm this is simulator-only.` : 'No production-approved commandable inverter capacity is exposed.', 'Never enable physical writes without exact manual and readback evidence.'),
            check('engineering', 'Engineering access', authBypass || session.development_auto_unlock || session.authenticated ? 'warn' : 'pass', authBypass ? 'Temporary field-development authentication bypass is active.' : session.development_auto_unlock ? 'Development auto-unlock is active.' : session.authenticated ? 'Engineering is currently unlocked.' : 'Engineering is locked and requires authentication.', authBypass ? 'Restore production authentication before any client or resale image.' : 'Disable development access before resale.'),
            check('development', 'Development provisioning', 'warn', 'Development network provisioning is active for field testing.', 'Remove committed development credentials and provisioning before production.')
        ];
    }

    function tone(status) { return status === 'pass' ? 'good' : status === 'warn' ? 'warning' : 'bad'; }
    function summary(checks) {
        const blocked = checks.filter((item) => item.status === 'block').length;
        const warnings = checks.filter((item) => item.status === 'warn').length;
        return { blocked, warnings, ready: blocked === 0 };
    }

    function render() {
        const view = byId('prelabReadinessView');
        if (!view || route() !== 'readiness') return;
        view.replaceChildren();
        const payload = state.payload;
        if (!payload) { view.append(node('div', 'op-empty-state', 'Collecting controller readiness data…')); return; }
        const checks = evaluate(payload);
        const result = summary(checks);
        const head = node('div', 'op-section-head');
        const copy = node('div');
        copy.append(node('p', 'eyebrow', 'Field-test preparation'), node('h3', '', 'Pre-Lab Readiness'), node('p', '', 'Automated software checks before the controller is connected to real meters and inverters.'));
        const actions = node('div', 'prelab-actions');
        const refresh = node('button', 'button secondary', 'Run checks');
        refresh.type = 'button';
        refresh.addEventListener('click', () => refreshAll(true));
        const exportButton = node('button', 'button primary', 'Export snapshot');
        exportButton.type = 'button';
        exportButton.addEventListener('click', exportSnapshot);
        actions.append(refresh, exportButton);
        head.append(copy, actions);
        view.append(head);
        const banner = node('article', `prelab-banner ${result.ready ? 'ready' : 'blocked'}`);
        banner.append(node('strong', '', result.ready ? 'Software ready for controlled lab testing' : 'Lab testing is blocked'), node('span', '', `${result.blocked} blocker(s) · ${result.warnings} warning(s)`));
        view.append(banner);
        const grid = node('div', 'prelab-grid');
        checks.forEach((item) => {
            const card = node('article', `prelab-check ${tone(item.status)}`);
            const icon = node('span', 'prelab-check-icon', item.status === 'pass' ? '✓' : item.status === 'warn' ? '△' : '!');
            const body = node('div', 'prelab-check-body');
            body.append(node('strong', '', item.label), node('p', '', item.detail));
            if (item.action && item.status !== 'pass') body.append(node('small', '', item.action));
            const pill = node('span', `op-state-pill ${tone(item.status)}`, item.status === 'pass' ? 'Pass' : item.status === 'warn' ? 'Review' : 'Block');
            card.append(icon, body, pill); grid.append(card);
        });
        view.append(grid);
        const navState = byId('prelabNavState');
        if (navState) { navState.textContent = result.ready ? 'Ready' : result.blocked; navState.className = `prelab-nav-state ${result.ready ? 'good' : 'bad'}`; }
    }

    function cancelPolling() {
        if (state.timer) window.clearTimeout(state.timer);
        state.timer = null;
        state.controller?.abort();
        state.controller = null;
    }

    function schedule() {
        if (state.timer || !active()) return;
        state.timer = window.setTimeout(async () => {
            state.timer = null;
            await refreshAll(false);
            schedule();
        }, REFRESH_MS);
    }

    async function refreshAll(manual = false) {
        if (state.busy || (!manual && !active()) || (manual && route() !== 'readiness')) return;
        state.busy = true;
        state.controller?.abort();
        const controller = new AbortController();
        state.controller = controller;
        const timeout = window.setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS);
        try {
            const signal = controller.signal;
            const [status, meters, inverters, history, events, session] = await Promise.all([
                api('/api/status', signal), api('/api/meters', signal), api('/api/inverters', signal),
                api('/api/operator/history?range=15m', signal), api('/api/operator/events', signal),
                api('/api/engineering/session', signal)
            ]);
            if (active() || manual) state.payload = { status, meters, inverters, history, events, session };
        } catch (error) {
            if (error?.name !== 'AbortError' && (active() || manual)) state.payload = { error: error.message };
        } finally {
            window.clearTimeout(timeout);
            if (state.controller === controller) state.controller = null;
            state.busy = false;
            if (active() || manual) render();
        }
    }

    function exportSnapshot() {
        if (!state.payload) return;
        const checks = evaluate(state.payload);
        const report = { report_type: 'Automatrix PV-DG pre-lab readiness snapshot', generated_at: new Date().toISOString(), checks, summary: summary(checks), controller: state.payload };
        const blob = new Blob([JSON.stringify(report, null, 2)], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const link = document.createElement('a');
        link.href = url; link.download = `automatrix-prelab-${new Date().toISOString().replace(/[:.]/g, '-')}.json`;
        document.body.append(link); link.click(); link.remove(); URL.revokeObjectURL(url);
    }

    function reconcileLifecycle() {
        ensurePage(); activateRoute(); render();
        if (!active()) { cancelPolling(); return; }
        refreshAll(false).finally(schedule);
    }

    function start() {
        ensurePage(); activateRoute();
        window.addEventListener('hashchange', reconcileLifecycle);
        document.addEventListener('visibilitychange', reconcileLifecycle);
        window.addEventListener('beforeunload', cancelPolling);
        reconcileLifecycle();
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();
