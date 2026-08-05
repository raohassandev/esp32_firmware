/* product-shell-v2.js - the application shell: top bar and navigation list.
 *
 * OWNS: the navigation list as a whole - item order and section labels. The
 *   only module that may reorder or insert into it; product-experience-v2.js
 *   used to add the labels while this module reordered the items beneath them.
 *   Also the top-bar controls it creates: health button, overflow menu, page
 *   context line.
 * DOES NOT OWN: routing, titles, breadcrumbs (app.js); which nav entries are
 *   visible, which follows authorisation (product-mode.js) - nothing here sets
 *   hidden on a nav item; page content (product-experience-v2.js).
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

    /* Health is data published by app.js, not text read back out of the page.
     * This used to query five rendered elements and apply English regular
     * expressions to their contents, so a copy edit silently changed the header
     * indicator - and it needed a characterData observer over a strip that
     * refreshes every two seconds. Both are gone. */
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
        /* Idempotent: an unchanged refresh writes nothing. */
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
            health = { tone: detail.tone || 'warning', label: detail.label || 'Checking', rows: healthRows(detail) };
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

    /* Navigation order and section labels, one pass, one module.
     *
     * The previous version latched after its first run, so an entry added later
     * - operator-product-suite.js adds commissioning after this module starts -
     * was never placed; and the labels came from product-experience-v2.js,
     * which could not know whether the reorder had happened. This pass is
     * repeatable instead of latched, and checks the list is not already in the
     * intended order before writing, because re-appending a node in place still
     * records a mutation. Visibility stays with product-mode.js. */
    /*
     * NAVIGATION HAS ONE OWNER, AND IT IS NOT THIS MODULE.
     *
     * There used to be a second arranger here: its own OPERATE/SERVICE route
     * lists, its own two group labels, and an append to the end of the same
     * .nav-list that app.js orders from the ROUTES table. Two modules arranging
     * one list to different schemes, and the result depended on which ran last.
     *
     * It had also gone stale in the way a second copy always does. Its lists
     * still named 'control', 'alarms' and 'readiness' -- three pages deleted from
     * the product -- and had never heard of 'generator' or 'network'. What the
     * owner saw was the consequence: Plant overview, Grid power and Solar
     * inverters pushed below the ACCESS heading with COMMISSION and MAINTAIN
     * standing empty above them.
     *
     * Its labels had already been neutered with a display:none rule rather than
     * removed, which left two empty spans in the list and the conflict intact.
     *
     * app.js keeps the route table, the group of every route and the durable
     * name of each, and produces the correct order from them. This module keeps
     * the page context, the health control and the overflow menu, which is what
     * its header claims.
     */

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
        removeDuplicateIntros();
        window.addEventListener('hashchange', () => {
            updatePageContext();
            removeDuplicateIntros();
        });
        /* One observer watches #mainContent for the whole application, in
         * product-mode.js. Subscribe rather than add a third to the same node.
         * Late nav entries arrive with their page, so grouping runs here too. */
        window.AutomatrixEngineeringAccess?.onContentChange(removeDuplicateIntros);
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();