/* product-shell-v2.js - the application shell: top bar and navigation list.
 *
 * OWNS: shell controls and applies the route hierarchy published by app.js.
 * app.js remains the single source for route names, four navigation groups and
 * ordering. This module does not flatten or rename that hierarchy.
 * DOES NOT OWN: routing, titles, breadcrumbs (app.js); backend authorization
 * (product-mode.js); page content (product-experience-v2.js).
 * Issues no request. Health arrives as the amx-controller-health event.
 */
(() => {
    'use strict';

    const ROUTE_CONTEXT = {
        dashboard: 'Plant condition and current power balance',
        meters: 'Grid measurement, freshness and availability',
        inverters: 'Solar production and fleet availability',
        control: 'Automatic-control state and safety interlocks',
        alarms: 'Conditions that require attention',
        system: 'Controller identity, service and diagnostics',
        wifi: 'Network commissioning and recovery access',
        engineering: 'Restricted commissioning and service tools',
        commissioning: 'Guided site commissioning',
        readiness: 'Engineering readiness checks before field testing'
    };

    const byId = (id) => document.getElementById(id);
    const route = () => location.hash.replace(/^#\/?/, '') || 'dashboard';
    const node = (tag, className = '', text = '') => {
        const item = document.createElement(tag);
        if (className) item.className = className;
        if (text) item.textContent = text;
        return item;
    };

    function installPageContext() {
        const heading = document.querySelector('.page-heading');
        if (!heading || heading.querySelector('.shell-page-context')) return;
        heading.append(node('div', 'shell-page-context'));
        updatePageContext();
    }

    function updatePageContext() {
        const context = document.querySelector('.shell-page-context');
        if (context) context.textContent = ROUTE_CONTEXT[route()] || 'Automatrix PV-DG controller';
    }

    let health = { tone: 'warning', label: 'Checking', rows: [] };

    function healthRows(detail) {
        return [
            ['Controller', detail.controller], ['Network', detail.network],
            ['Grid meter', detail.meter], ['Control', detail.control],
            ['Alarms', detail.alarms]
        ];
    }

    function createPopover(title) {
        const backdrop = node('div', 'shell-popover-backdrop');
        backdrop.hidden = true;
        const panel = node('section', 'shell-popover');
        panel.setAttribute('role', 'dialog');
        panel.setAttribute('aria-modal', 'true');
        const head = node('div', 'shell-popover-head');
        head.append(node('h2', '', title));
        const close = node('button', 'shell-popover-close', 'Close');
        close.type = 'button';
        close.setAttribute('aria-label', 'Close');
        head.append(close);
        const body = node('div', 'shell-popover-body');
        panel.append(head, body);
        backdrop.append(panel);
        document.body.append(backdrop);
        const hide = () => {
            backdrop.hidden = true;
            document.body.classList.remove('shell-popover-open');
        };
        close.addEventListener('click', hide);
        backdrop.addEventListener('click', (event) => {
            if (event.target === backdrop) hide();
        });
        return {
            backdrop,
            body,
            show() {
                backdrop.hidden = false;
                document.body.classList.add('shell-popover-open');
            },
            hide
        };
    }

    function installHealthControl() {
        const actions = document.querySelector('.topbar-actions');
        if (!actions || byId('shellHealthButton')) return;
        const popover = createPopover('System health');
        popover.backdrop.id = 'shellHealthPopover';
        const button = node('button', 'shell-health-button');
        button.id = 'shellHealthButton';
        button.type = 'button';
        button.innerHTML = '<span class="shell-health-dot" aria-hidden="true"></span><span class="shell-health-label">Checking</span>';
        button.addEventListener('click', () => {
            popover.body.replaceChildren();
            const list = node('div', 'shell-status-list');
            health.rows.forEach(([label, value]) => {
                const row = node('div', 'shell-status-row');
                row.append(node('span', '', label), node('strong', '', value || 'Checking'));
                list.append(row);
            });
            popover.body.append(list);
            popover.show();
        });
        actions.insertBefore(button, actions.firstChild);
        const update = () => {
            const className = `shell-health-button ${health.tone}`;
            if (button.className !== className) button.className = className;
            const label = button.querySelector('.shell-health-label');
            if (label && label.textContent !== health.label) label.textContent = health.label;
            const described = `System health: ${health.label}`;
            if (button.getAttribute('aria-label') !== described) button.setAttribute('aria-label', described);
        };
        update();
        window.addEventListener('amx-controller-health', (event) => {
            const detail = event.detail || {};
            health = {
                tone: detail.tone || 'warning',
                label: detail.label || 'Checking',
                rows: healthRows(detail)
            };
            update();
        });
    }

    function clickExisting(id) {
        const target = byId(id);
        if (target) target.click();
    }

    function installOverflowMenu() {
        const actions = document.querySelector('.topbar-actions');
        if (!actions || byId('shellOverflowButton')) return;
        const popover = createPopover('Controller menu');
        popover.backdrop.id = 'shellMenuPopover';
        const button = node('button', 'shell-overflow-button', 'Menu');
        button.id = 'shellOverflowButton';
        button.type = 'button';
        button.setAttribute('aria-label', 'Open controller menu');
        button.addEventListener('click', () => {
            popover.body.replaceChildren();
            const menu = node('div', 'shell-menu');
            const action = (label, detail, handler) => {
                const item = node('button');
                item.type = 'button';
                item.append(node('span', '', label), node('small', '', detail));
                item.addEventListener('click', () => {
                    popover.hide();
                    handler();
                });
                menu.append(item);
            };
            action('Refresh data', 'Update current readings', () => clickExisting('refreshButton'));
            action('Display density', document.documentElement.dataset.density === 'compact' ? 'Compact' : 'Comfortable', () => clickExisting('productDensityButton'));
            action('Kiosk display', document.documentElement.classList.contains('kiosk-mode') ? 'On' : 'Off', () => clickExisting('productKioskButton'));
            action('Theme', document.documentElement.dataset.theme || 'System', () => clickExisting('themeToggle'));
            action('Controller information', 'Identity and service state', () => {
                location.hash = '#/system';
            });
            action('Engineering workspace', document.documentElement.dataset.access === 'engineering' ? 'Unlocked' : 'Restricted', () => {
                const engineering = byId('engineeringNav') || byId('productEngineeringEntry');
                if (engineering) engineering.click();
                else location.hash = '#/engineering';
            });
            popover.body.append(menu);
            popover.show();
        });
        actions.append(button);
    }

    const OPERATOR_ROUTES = ['dashboard', 'meters', 'inverters', 'alarms'];
    const ENGINEERING_ROUTES = ['engineering', 'commissioning', 'readiness', 'wifi', 'control', 'system'];

    /* Pre-lab readiness is a commissioning workflow, not a running-plant task.
     * Its endpoint remains public and typing its URL still works: this is only a
     * navigation offer, never an authorization boundary. Browser hiding is not
     * security; product-mode.js and the server remain the owners of authority. */
    function applyPresentationAccess() {
        const authenticated = document.documentElement.dataset.access === 'engineering';
        const readiness = document.querySelector('.nav-list [data-route="readiness"]');
        if (readiness) {
            readiness.dataset.engineeringNav = 'true';
            readiness.hidden = !authenticated;
            readiness.setAttribute('aria-hidden', authenticated ? 'false' : 'true');
        }
        const lock = byId('engineeringLockIcon');
        if (lock) {
            lock.textContent = authenticated ? 'Unlocked' : 'Locked';
            lock.classList.add('engineering-lock-text');
        }
    }

    /* app.js owns the normal four-section hierarchy. This fallback is retained
     * for older markup that has no app-owned group headings yet; its two labels
     * keep the source contract and old deployed pages deterministic. */
    function groupNavigation() {
        const nav = document.querySelector('.nav-list');
        if (!nav) return;

        window.AutomatrixUi?.ensureNavigationHierarchy();
        applyPresentationAccess();

        if (nav.dataset.shellGrouped === 'true' && nav.querySelector('[data-nav-group]')) return;

        const ordered = [];
        const operator = OPERATOR_ROUTES
            .map((name) => nav.querySelector(`[data-route="${name}"]`))
            .filter(Boolean);
        const engineering = ENGINEERING_ROUTES
            .map((name) => nav.querySelector(`[data-route="${name}"]`))
            .filter(Boolean);
        const labels = [...nav.querySelectorAll(':scope > .experience-nav-label')];
        const operatorLabel = labels[0] || node('div', 'experience-nav-label');
        const engineeringLabel = labels[1] || node('div', 'experience-nav-label');
        operatorLabel.textContent = 'Operate';
        engineeringLabel.textContent = 'Commission & service';
        if (operator.length) ordered.push(operatorLabel, ...operator);
        if (engineering.length) ordered.push(engineeringLabel, ...engineering);
        labels.slice(2).forEach((extra) => extra.remove());

        const tail = [...nav.children].slice(-ordered.length);
        if (tail.length === ordered.length && ordered.every((item, index) => tail[index] === item)) {
            nav.dataset.shellGrouped = 'true';
            return;
        }
        nav.append(...ordered);
        nav.dataset.shellGrouped = 'true';
    }

    function removeDuplicateIntros() {
        document.querySelectorAll('.page').forEach((page) => {
            const intros = page.querySelectorAll(':scope > .page-intro');
            if (intros.length > 1) [...intros].slice(1).forEach((item) => item.remove());
        });
    }

    function start() {
        document.body.classList.add('product-shell-v2');
        installPageContext();
        installHealthControl();
        installOverflowMenu();
        groupNavigation();
        removeDuplicateIntros();
        window.addEventListener('hashchange', () => {
            updatePageContext();
            groupNavigation();
            removeDuplicateIntros();
        });
        window.addEventListener('amx-access-change', groupNavigation);
        window.AutomatrixEngineeringAccess?.onContentChange(() => {
            groupNavigation();
            removeDuplicateIntros();
        });
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();