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

    const OPERATOR_NAV_LABELS = {
        dashboard: 'Overview',
        meters: 'Grid',
        inverters: 'Solar',
        alarms: 'Alarms',
        readiness: 'Readiness',
    };

    const ENGINEERING_GROUPS = [
        {
            id: 'commission',
            title: 'Commission',
            detail: 'Primary guided workflow for a new site or controlled re-commissioning.',
            routes: ['commissioning'],
        },
        {
            id: 'configure',
            title: 'Configure',
            detail: 'Expert setup tools for communications, metering, inverter profiles and control parameters.',
            routes: ['wifi', 'meters', 'inverters', 'control'],
        },
        {
            id: 'service',
            title: 'Service',
            detail: 'Backup, security and controller maintenance. Keep service actions separate from commissioning.',
            routes: ['system'],
        },
    ];

    const ROUTE_LABELS = {
        dashboard: 'Overview', meters: 'Grid', inverters: 'Solar',
        alarms: 'Alarms', readiness: 'Readiness', control: 'PV-DG control',
        commissioning: 'Guided commissioning', wifi: 'Network setup', engineering: 'Engineering home',
        system: 'Controller service',
    };

    function node(tag, className = '', text = '') {
        const item = document.createElement(tag);
        if (className) item.className = className;
        if (text) item.textContent = text;
        return item;
    }

    function normalizeOperatorNavigationLabels(root = document) {
        Object.entries(OPERATOR_NAV_LABELS).forEach(([name, label]) => {
            root.querySelectorAll(`[data-route="${name}"]`).forEach((link) => {
                const small = link.querySelector('small');
                if (small) small.textContent = label;
                const spans = link.querySelectorAll(':scope > span');
                const textSpan = spans.length > 1 ? spans[spans.length - 1] : null;
                if (textSpan) textSpan.textContent = label;
                link.setAttribute('aria-label', label);
            });
        });
    }

    function installNavigationSections() {
        const nav = document.querySelector('.nav-list');
        if (!nav) return;
        normalizeOperatorNavigationLabels(nav);
        nav.querySelectorAll('.experience-nav-label, .industrial-nav-section').forEach((item) => item.remove());

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

    function normalizeMobileNavigation() {
        const nav = byId('productMobileNav');
        if (!nav) return;
        let slot = nav.querySelector('[data-industrial-control-slot="true"]');
        if (!slot) {
            slot = nav.querySelector('[data-route="control"]');
            if (!slot) return;
            slot.dataset.industrialControlSlot = 'true';
        }
        const engineering = isEngineering();
        const target = engineering ? 'control' : 'readiness';
        slot.dataset.route = target;
        slot.href = `#/${target}`;
        const icon = slot.querySelector('span');
        const label = slot.querySelector('small');
        if (icon) icon.textContent = engineering ? '⇄' : '✓';
        if (label) label.textContent = engineering ? 'Control' : 'Readiness';
        slot.setAttribute('aria-label', engineering ? 'PV-DG control' : 'Readiness');
        normalizeOperatorNavigationLabels(nav);
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
        normalizeMobileNavigation();
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

    function activateRoute(item, targetRoute) {
        if (!targetRoute) return;
        item.dataset.industrialTargetRoute = targetRoute;
        item.tabIndex = 0;
        item.setAttribute('role', 'link');
        item.style.cursor = 'pointer';
        item.title = `Open ${targetRoute}`;
        const open = () => { location.hash = `#/${targetRoute}`; };
        item.addEventListener('click', open);
        item.addEventListener('keydown', (event) => {
            if (event.key === 'Enter' || event.key === ' ') {
                event.preventDefault();
                open();
            }
        });
    }

    function cell(label, value, tone = 'neutral', targetRoute = '') {
        const item = node('div', `industrial-command-cell ${tone}`);
        item.append(node('span', '', label), node('strong', '', value));
        activateRoute(item, targetRoute);
        if (targetRoute) item.setAttribute('aria-label', `${label}: ${value}. Open ${targetRoute}.`);
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
        const engineering = isEngineering();

        bar.replaceChildren(
            cell('Plant state', plantState, plantTone, plantTone === 'good' ? 'readiness' : 'alarms'),
            cell('Grid measurement', meter, meterTone, 'meters'),
            cell('Control safety', control, controlTone, engineering ? 'control' : 'readiness'),
            cell('Network', network, toneFromText(network), engineering ? 'wifi' : 'readiness'),
        );
    }

    function enhanceEquipmentAccess() {
        document.querySelectorAll('.op-equipment-bar, .op-inverter-row').forEach((item) => {
            if (item.dataset.industrialKeyboard === 'true') return;
            item.dataset.industrialKeyboard = 'true';
            item.tabIndex = 0;
            item.setAttribute('role', 'button');
            item.addEventListener('keydown', (event) => {
                if (event.key === 'Enter' || event.key === ' ') {
                    event.preventDefault();
                    item.click();
                }
            });
        });
    }

    function installEngineeringStyles() {
        if (byId('industrialEngineeringStyle')) return;
        const style = node('style');
        style.id = 'industrialEngineeringStyle';
        style.textContent = `
            .industrial-engineering-shell{display:grid;gap:12px}.industrial-engineering-status{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:1px;overflow:hidden;border:1px solid var(--line);border-radius:var(--industrial-radius);background:var(--line)}
            .industrial-engineering-status>div{min-width:0;padding:10px 12px;background:var(--surface-sunken)}.industrial-engineering-status span{display:block;color:var(--muted-2);font-size:9px;font-weight:850;letter-spacing:.08em;text-transform:uppercase}.industrial-engineering-status strong{display:block;margin-top:4px;overflow:hidden;color:var(--text);font-size:12px;text-overflow:ellipsis;white-space:nowrap}
            .industrial-engineering-safety{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:11px 13px;border:1px solid color-mix(in srgb,var(--yellow) 52%,var(--line));border-radius:var(--industrial-radius);background:color-mix(in srgb,var(--yellow) 8%,var(--panel));color:var(--muted);font-size:11px}.industrial-engineering-safety strong{color:var(--text)}
            .industrial-engineering-groups{display:grid;gap:12px}.industrial-engineering-group{display:grid;gap:9px;padding:13px;border:1px solid var(--line);border-radius:var(--industrial-radius);background:var(--panel)}.industrial-engineering-group-head{display:flex;align-items:start;justify-content:space-between;gap:14px;padding-bottom:9px;border-bottom:1px solid var(--line-soft)}.industrial-engineering-group-head h3{margin:0;color:var(--text);font-size:15px}.industrial-engineering-group-head p{max-width:650px;margin:3px 0 0;color:var(--muted);font-size:10px;line-height:1.45}.industrial-engineering-group-badge{padding:4px 7px;border:1px solid var(--line);border-radius:4px;color:var(--muted-2);font-size:9px;font-weight:850;letter-spacing:.08em;text-transform:uppercase}
            .industrial-engineering-group-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}.industrial-engineering-group[data-group="commission"] .industrial-engineering-group-grid,.industrial-engineering-group[data-group="service"] .industrial-engineering-group-grid{grid-template-columns:1fr}.industrial-engineering-group .engineering-tile{min-height:92px!important;padding:12px!important;border-radius:var(--industrial-radius-sm)!important;box-shadow:none!important}.industrial-engineering-group[data-group="commission"] .engineering-tile{border-left:3px solid var(--orange)!important}.industrial-engineering-group[data-group="service"] .engineering-tile{border-left:3px solid var(--yellow)!important}.industrial-engineering-group .engineering-tile strong{font-size:14px}.industrial-engineering-group .engineering-tile small{font-size:10px;line-height:1.4}
            .industrial-engineering-group .engineering-tile:focus-visible{outline:2px solid var(--orange);outline-offset:2px}.industrial-engineering-group .engineering-tile{min-height:var(--industrial-touch)}
            .industrial-engineering-secondary{display:flex;align-items:center;justify-content:space-between;gap:12px;color:var(--muted);font-size:10px}.industrial-engineering-secondary a{min-height:var(--industrial-touch);display:inline-flex;align-items:center}
            @media (max-width:900px),(max-height:560px){.industrial-engineering-status{grid-template-columns:repeat(4,minmax(0,1fr))}.industrial-engineering-status>div{padding:7px 8px}.industrial-engineering-safety{padding:8px 9px}.industrial-engineering-group{padding:9px}.industrial-engineering-group-head{padding-bottom:7px}.industrial-engineering-group-grid{grid-template-columns:repeat(2,minmax(0,1fr));gap:6px}.industrial-engineering-group .engineering-tile{min-height:72px!important;padding:9px!important}.industrial-engineering-group .engineering-tile small{display:none}.industrial-engineering-group .engineering-tile strong{font-size:12px}}
            @media (max-width:700px){.industrial-engineering-status{grid-template-columns:repeat(2,minmax(0,1fr))}.industrial-engineering-group-grid{grid-template-columns:1fr}.industrial-engineering-safety,.industrial-engineering-secondary{align-items:flex-start;flex-direction:column}}
        `;
        document.head.append(style);
    }

    function engineeringStatusCell(label, value) {
        const item = node('div');
        item.append(node('span', '', label), node('strong', '', value));
        return item;
    }

    function updateEngineeringStatus() {
        const status = byId('industrialEngineeringStatus');
        if (!status) return;
        status.replaceChildren(
            engineeringStatusCell('Access', isEngineering() ? 'Engineering unlocked' : 'Engineering locked'),
            engineeringStatusCell('Controller', statusText('statusController')),
            engineeringStatusCell('Data', statusText('statusUpdated', 'Never')),
            engineeringStatusCell('Current task', ROUTE_LABELS[route()] || route()),
        );
    }

    function composeEngineeringWorkspace() {
        installEngineeringStyles();
        const page = document.querySelector('.page[data-page="engineering"]');
        const console = byId('engineeringConsole');
        const sourceGrid = console?.querySelector('.engineering-grid');
        if (!page || !console || !sourceGrid) return;

        let shell = byId('industrialEngineeringShell');
        if (!shell) {
            shell = node('section', 'industrial-engineering-shell');
            shell.id = 'industrialEngineeringShell';

            const status = node('div', 'industrial-engineering-status');
            status.id = 'industrialEngineeringStatus';
            status.setAttribute('aria-label', 'Engineering workspace status');

            const safety = node('div', 'industrial-engineering-safety');
            const safetyCopy = node('div');
            safetyCopy.append(
                node('strong', '', 'Automatic control remains locked during commissioning.'),
                node('div', '', 'Use Guided Commissioning for a controlled workflow. Direct configuration is intended for expert service work.'),
            );
            const readiness = node('a', 'button secondary', 'Review readiness');
            readiness.href = '#/readiness';
            safety.append(safetyCopy, readiness);

            const groups = node('div', 'industrial-engineering-groups');
            groups.id = 'industrialEngineeringGroups';

            ENGINEERING_GROUPS.forEach((group) => {
                const section = node('section', 'industrial-engineering-group');
                section.dataset.group = group.id;
                const head = node('div', 'industrial-engineering-group-head');
                const copy = node('div');
                copy.append(node('h3', '', group.title), node('p', '', group.detail));
                head.append(copy, node('span', 'industrial-engineering-group-badge', group.id === 'commission' ? 'Primary workflow' : group.id === 'service' ? 'Maintenance' : 'Expert tools'));
                const grid = node('div', 'industrial-engineering-group-grid');
                grid.dataset.groupGrid = group.id;
                section.append(head, grid);
                groups.append(section);
            });

            const secondary = node('div', 'industrial-engineering-secondary');
            secondary.append(
                node('span', '', 'Configuration and service changes remain subject to the controller’s existing authentication, persistence and safety interlocks.'),
                Object.assign(node('a', 'button secondary', 'Return to plant'), { href: '#/dashboard' }),
            );

            shell.append(status, safety, groups, secondary);
            sourceGrid.before(shell);
        }

        const routeForTile = (tile) => String(tile.getAttribute('href') || '').replace(/^#\/?/, '');
        const originalTiles = [...sourceGrid.querySelectorAll(':scope > .engineering-tile')];
        const existingTiles = [...console.querySelectorAll('.engineering-tile')];
        [...new Set([...originalTiles, ...existingTiles])].forEach((tile) => {
            const tileRoute = routeForTile(tile);
            const group = ENGINEERING_GROUPS.find((entry) => entry.routes.includes(tileRoute));
            const target = group ? shell.querySelector(`[data-group-grid="${group.id}"]`) : null;
            if (target && tile.parentElement !== target) target.append(tile);
        });
        sourceGrid.hidden = true;
        sourceGrid.setAttribute('aria-hidden', 'true');
        updateEngineeringStatus();
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
        enhanceEquipmentAccess();
        composeEngineeringWorkspace();
        updateEngineeringStatus();
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
        normalizeOperatorNavigationLabels(nav);
        installNavigationSections();
        normalizeMobileNavigation();
    }

    function installRouteRefresh() {
        window.addEventListener('hashchange', () => {
            makePrimaryNavDeterministic();
            installDashboardCommandBar();
            composeEngineeringWorkspace();
            updateAllStatus();
        });
        const main = byId('mainContent');
        if (main) {
            new MutationObserver((records) => {
                if (records.some((record) => record.addedNodes.length)) {
                    makePrimaryNavDeterministic();
                    installDashboardCommandBar();
                    composeEngineeringWorkspace();
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
        normalizeMobileNavigation();
        enhanceEquipmentAccess();
        composeEngineeringWorkspace();
        observeStatus();
        installRouteRefresh();
        updateAllStatus();
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();