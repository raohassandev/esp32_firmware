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
        readiness: 'Software and field-test readiness checks'
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

    function healthSnapshot() {
        const controller = byId('statusController')?.textContent || 'Checking';
        const network = byId('statusNetwork')?.textContent || 'Checking';
        const meter = byId('statusMeter')?.textContent || 'Checking';
        const control = byId('statusControl')?.textContent || 'Checking';
        const alarms = byId('statusAlarms')?.textContent || 'Checking';
        const offline = /offline|unavailable/i.test(`${controller} ${network}`);
        const critical = /critical|active|offline|stale/i.test(`${alarms} ${meter}`);
        const attention = /disabled|checking|setup|recovery/i.test(`${control} ${network}`);
        return {
            tone: offline || critical ? 'bad' : attention ? 'warning' : 'good',
            label: offline || critical ? 'Attention' : attention ? 'Review' : 'Normal',
            rows: [['Controller', controller], ['Network', network], ['Grid meter', meter], ['Control', control], ['Alarms', alarms]]
        };
    }

    function createPopover(title) {
        const backdrop = node('div', 'shell-popover-backdrop');
        backdrop.hidden = true;
        const panel = node('section', 'shell-popover');
        panel.setAttribute('role', 'dialog');
        panel.setAttribute('aria-modal', 'true');
        const head = node('div', 'shell-popover-head');
        head.append(node('h2', '', title));
        const close = node('button', 'shell-popover-close', '×');
        close.type = 'button';
        close.setAttribute('aria-label', 'Close');
        head.append(close);
        const body = node('div', 'shell-popover-body');
        panel.append(head, body);
        backdrop.append(panel);
        document.body.append(backdrop);
        const hide = () => { backdrop.hidden = true; document.body.classList.remove('shell-popover-open'); };
        close.addEventListener('click', hide);
        backdrop.addEventListener('click', (event) => { if (event.target === backdrop) hide(); });
        return { backdrop, body, show() { backdrop.hidden = false; document.body.classList.add('shell-popover-open'); }, hide };
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
            const snapshot = healthSnapshot();
            popover.body.replaceChildren();
            const list = node('div', 'shell-status-list');
            snapshot.rows.forEach(([label, value]) => {
                const row = node('div', 'shell-status-row');
                row.append(node('span', '', label), node('strong', '', value));
                list.append(row);
            });
            popover.body.append(list);
            popover.show();
        });
        actions.insertBefore(button, actions.firstChild);
        const update = () => {
            const snapshot = healthSnapshot();
            button.className = `shell-health-button ${snapshot.tone}`;
            const label = button.querySelector('.shell-health-label');
            if (label) label.textContent = snapshot.label;
            button.setAttribute('aria-label', `System health: ${snapshot.label}`);
        };
        update();
        const strip = document.querySelector('.status-strip');
        if (strip) new MutationObserver(update).observe(strip, { childList: true, subtree: true, characterData: true });
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
        const button = node('button', 'shell-overflow-button', '⋮');
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
                item.addEventListener('click', () => { popover.hide(); handler(); });
                menu.append(item);
            };
            action('Refresh data', 'Update current readings', () => clickExisting('refreshButton'));
            action('Display density', document.documentElement.dataset.density === 'compact' ? 'Compact' : 'Comfortable', () => clickExisting('productDensityButton'));
            action('Kiosk display', document.documentElement.classList.contains('kiosk-mode') ? 'On' : 'Off', () => clickExisting('productKioskButton'));
            action('Theme', document.documentElement.dataset.theme || 'System', () => clickExisting('themeToggle'));
            action('Controller information', 'Identity and service state', () => { location.hash = '#/system'; });
            action('Engineering workspace', document.documentElement.dataset.access === 'engineering' ? 'Development access' : 'Restricted', () => {
                const engineering = byId('engineeringNav') || byId('productEngineeringEntry');
                if (engineering) engineering.click(); else location.hash = '#/engineering';
            });
            popover.body.append(menu);
            popover.show();
        });
        actions.append(button);
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
        /* Navigation ordering/grouping is owned by Industrial UI v1. Product
           Shell V2 retains health, overflow, route context and duplicate-intro
           cleanup only; a second nav owner caused startup reorder/flicker. */
        removeDuplicateIntros();
        window.addEventListener('hashchange', () => {
            updatePageContext();
            removeDuplicateIntros();
        });
        const main = byId('mainContent');
        if (main) {
            new MutationObserver((records) => {
                if (records.some((record) => record.target === main && record.addedNodes.length)) removeDuplicateIntros();
            }).observe(main, { childList: true });
        }
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();