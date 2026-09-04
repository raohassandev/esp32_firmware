(() => {
    'use strict';

    const byId = (id) => document.getElementById(id);
    const route = () => location.hash.replace(/^#\/?/, '') || 'dashboard';
    const isEngineering = () => document.documentElement.dataset.access === 'engineering';

    const NAV_GROUPS = [
        { title: 'Operate', routes: ['dashboard', 'meters', 'inverters', 'alarms', 'readiness'] },
        { title: 'Engineer', routes: ['control', 'commissioning', 'wifi', 'engineering'] },
        { title: 'Service', routes: ['system'] },
    ];

    function node(tag, className = '', text = '') {
        const item = document.createElement(tag);
        if (className) item.className = className;
        if (text) item.textContent = text;
        return item;
    }

    function installNavigationSections() {
        const nav = document.querySelector('.nav-list');
        if (!nav) return;
        nav.querySelectorAll('.industrial-nav-section').forEach((item) => item.remove());

        NAV_GROUPS.forEach((group) => {
            const links = group.routes
                .map((name) => nav.querySelector(`.nav-link[data-route="${name}"]`))
                .filter(Boolean);
            if (!links.length) return;

            const label = node('div', 'industrial-nav-section', group.title);
            label.dataset.industrialNavGroup = group.title.toLowerCase();
            nav.insertBefore(label, links[0]);
        });
        updateRoleVisibility();
    }

    function updateRoleVisibility() {
        const engineering = isEngineering();
        const label = document.querySelector('[data-industrial-nav-group="engineer"]');
        if (!label) return;
        const engineeringRoutes = new Set(['control', 'commissioning', 'wifi', 'engineering']);
        const hasVisibleLink = [...document.querySelectorAll('.nav-link[data-route]')]
            .some((link) => engineeringRoutes.has(link.dataset.route) && !link.hidden && getComputedStyle(link).display !== 'none');
        label.hidden = !engineering && !hasVisibleLink;
    }

    function installRoleBadge() {
        const actions = document.querySelector('.topbar-actions');
        if (!actions || byId('industrialRoleBadge')) return;
        const badge = node('div', 'industrial-role-badge operator', 'Operator');
        badge.id = 'industrialRoleBadge';
        badge.setAttribute('aria-label', 'Current access role: Operator');
        actions.insertBefore(badge, actions.firstChild);
        updateRoleBadge();
    }

    function updateRoleBadge() {
        const badge = byId('industrialRoleBadge');
        if (!badge) return;
        const engineering = isEngineering();
        badge.className = `industrial-role-badge ${engineering ? 'engineering' : 'operator'}`;
        badge.textContent = engineering ? 'Engineering' : 'Operator';
        badge.setAttribute('aria-label', `Current access role: ${engineering ? 'Engineering' : 'Operator'}`);
        updateRoleVisibility();
    }

    function statusText(id, fallback = 'Checking') {
        return (byId(id)?.textContent || fallback).trim();
    }

    function toneFromText(value, normalWords = []) {
        const text = String(value || '').toLowerCase();
        if (/critical|fault|offline|unavailable|stale|failed|blocked|trip|alarm/.test(text)) return 'bad';
        if (/warning|attention|disabled|checking|setup|unknown|recovery|pending/.test(text)) return 'warning';
        if (normalWords.some((word) => text.includes(word)) || /online|normal|ready|healthy|connected|clear|enabled/.test(text)) return 'good';
        return 'neutral';
    }

    function installAlarmControl() {
        const actions = document.querySelector('.topbar-actions');
        if (!actions || byId('industrialAlarmButton')) return;
        const button = node('button', 'industrial-alarm-button warning');
        button.id = 'industrialAlarmButton';
        button.type = 'button';
        const label = node('span', '', 'Alarms');
        const value = node('strong', '', '--');
        value.id = 'industrialAlarmValue';
        button.append(label, value);
        button.addEventListener('click', () => { location.hash = '#/alarms'; });
        actions.insertBefore(button, actions.firstChild?.nextSibling || null);
        updateAlarmControl();
    }

    function updateAlarmControl() {
        const button = byId('industrialAlarmButton');
        const value = byId('industrialAlarmValue');
        if (!button || !value) return;
        const alarms = statusText('statusAlarms', '--');
        const numeric = Number((alarms.match(/\d+/) || [])[0]);
        const clear = /none|clear|0\b|no active/i.test(alarms) || numeric === 0;
        const tone = clear ? 'good' : toneFromText(alarms);
        button.className = `industrial-alarm-button ${tone === 'neutral' ? 'warning' : tone}`;
        value.textContent = alarms;
        button.setAttribute('aria-label', `Alarms: ${alarms}. Open alarm view.`);
    }

    function installFreshness() {
        const actions = document.querySelector('.topbar-actions');
        if (!actions || byId('industrialFreshness')) return;
        const item = node('div', 'industrial-freshness warning', 'Data: checking');
        item.id = 'industrialFreshness';
        actions.insertBefore(item, byId('refreshButton') || actions.lastElementChild);
        updateFreshness();
    }

    function updateFreshness() {
        const item = byId('industrialFreshness');
        if (!item) return;
        const value = statusText('statusUpdated', 'Never');
        const tone = /never|stale|unknown|unavailable/i.test(value) ? 'bad' : /checking|--/i.test(value) ? 'warning' : 'good';
        item.className = `industrial-freshness ${tone}`;
        item.textContent = `Data: ${value}`;
        item.title = 'Age of the latest controller status update';
    }

    function cell(label, value, tone = 'neutral') {
        const item = node('div', `industrial-command-cell ${tone}`);
        item.append(node('span', '', label), node('strong', '', value));
        return item;
    }

    function installDashboardCommandBar() {
        const dashboard = document.querySelector('.page[data-page="dashboard"]');
        if (!dashboard || byId('industrialCommandBar')) return;
        const metricGrid = dashboard.querySelector('.metric-grid');
        if (!metricGrid) return;
        const bar = node('section', 'industrial-command-bar');
        bar.id = 'industrialCommandBar';
        bar.setAttribute('aria-label', 'Plant operating summary');
        metricGrid.before(bar);
        updateDashboardCommandBar();
    }

    function updateDashboardCommandBar() {
        const bar = byId('industrialCommandBar');
        if (!bar) return;
        const controller = statusText('statusController');
        const network = statusText('statusNetwork');
        const meter = statusText('statusMeter');
        const control = statusText('statusControl');
        const alarms = statusText('statusAlarms');

        const controllerTone = toneFromText(controller, ['running']);
        const meterTone = toneFromText(meter, ['fresh']);
        const controlTone = toneFromText(control, ['safe']);
        const alarmTone = /none|clear|0\b|no active/i.test(alarms) ? 'good' : toneFromText(alarms);
        const plantTone = [controllerTone, meterTone, alarmTone].includes('bad') ? 'bad' :
            [controllerTone, meterTone, controlTone, alarmTone].includes('warning') ? 'warning' : 'good';
        const plantState = plantTone === 'bad' ? 'Attention required' : plantTone === 'warning' ? 'Review condition' : 'Plant normal';

        bar.replaceChildren(
            cell('Plant state', plantState, plantTone),
            cell('Grid measurement', meter, meterTone),
            cell('Control safety', control, controlTone),
            cell('Network', network, toneFromText(network)),
        );
    }

    function applyGlobalState() {
        const root = document.documentElement;
        const controller = statusText('statusController');
        const network = statusText('statusNetwork');
        const meter = statusText('statusMeter');
        const alarms = statusText('statusAlarms');
        const updated = statusText('statusUpdated', 'Never');
        const offline = /offline|unavailable|failed/i.test(`${controller} ${network}`);
        const stale = /stale|never|unavailable/i.test(`${meter} ${updated}`);
        const attention = !offline && (/alarm|critical|warning|attention|stale|disabled|checking/i.test(`${alarms} ${meter}`) || stale);
        root.classList.toggle('industrial-state-offline', offline);
        root.classList.toggle('industrial-state-stale', stale);
        root.classList.toggle('industrial-state-attention', attention);
    }

    function updateAllStatus() {
        updateRoleBadge();
        updateAlarmControl();
        updateFreshness();
        updateDashboardCommandBar();
        applyGlobalState();
    }

    function observeStatus() {
        const strip = document.querySelector('.status-strip');
        if (strip) {
            new MutationObserver(updateAllStatus).observe(strip, {
                childList: true,
                subtree: true,
                characterData: true,
            });
        }
        const root = document.documentElement;
        new MutationObserver((records) => {
            if (records.some((record) => record.attributeName === 'data-access')) updateAllStatus();
        }).observe(root, { attributes: true, attributeFilter: ['data-access'] });
    }

    function makePrimaryNavDeterministic() {
        const nav = document.querySelector('.nav-list');
        if (!nav) return;
        const order = ['dashboard', 'meters', 'inverters', 'alarms', 'readiness', 'control', 'commissioning', 'wifi', 'engineering', 'system'];
        order.forEach((name) => {
            const link = nav.querySelector(`.nav-link[data-route="${name}"]`);
            if (link) nav.append(link);
        });
        installNavigationSections();
    }

    function installRouteRefresh() {
        window.addEventListener('hashchange', () => {
            makePrimaryNavDeterministic();
            installDashboardCommandBar();
            updateAllStatus();
        });
        const main = byId('mainContent');
        if (main) {
            new MutationObserver((records) => {
                if (records.some((record) => record.addedNodes.length)) {
                    makePrimaryNavDeterministic();
                    installDashboardCommandBar();
                    updateAllStatus();
                }
            }).observe(main, { childList: true });
        }
    }

    function start() {
        document.documentElement.classList.add('industrial-ui-v1');
        document.body.classList.add('industrial-ui-v1');
        makePrimaryNavDeterministic();
        installRoleBadge();
        installAlarmControl();
        installFreshness();
        installDashboardCommandBar();
        observeStatus();
        installRouteRefresh();
        updateAllStatus();
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();
