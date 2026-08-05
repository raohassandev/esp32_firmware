(() => {
    'use strict';

    const PREF_KEY = 'amx-product-preferences-v1';
    const state = {
        preferences: { density: 'comfortable', kiosk: false },
        payload: null,
        modal: null,
        timer: null,
    };
    const byId = (id) => document.getElementById(id);
    const isEngineering = () => document.documentElement.dataset.access === 'engineering';
    const currentRoute = () => location.hash.replace(/^#\/?/, '') || 'dashboard';

    function node(tag, className = '', text = null) {
        const item = document.createElement(tag);
        if (className) item.className = className;
        if (text != null) item.textContent = String(text);
        return item;
    }

    async function api(path) {
        /* Through the shared reader: several modules poll these same paths on
         * their own timers, and the controller has very few client sockets. A
         * GET already in flight or answered a moment ago is reused instead of
         * asked again. See web/shared-fetch.js. */
        if (window.AutomatrixFetch) return window.AutomatrixFetch.get(path);
        const response = await fetch(path, { cache: 'no-store', credentials: 'same-origin' });
        const payload = await response.json().catch(() => ({}));
        if (!response.ok) throw new Error(payload.error || `HTTP ${response.status}`);
        return payload;
    }

    function loadPreferences() {
        try {
            const saved = JSON.parse(localStorage.getItem(PREF_KEY) || '{}');
            if (saved.density === 'compact' || saved.density === 'comfortable') state.preferences.density = saved.density;
            state.preferences.kiosk = saved.kiosk === true;
        } catch (_) { /* use safe defaults */ }
        applyPreferences();
    }

    function savePreferences() {
        localStorage.setItem(PREF_KEY, JSON.stringify(state.preferences));
        applyPreferences();
    }

    function applyPreferences() {
        document.documentElement.dataset.density = state.preferences.density;
        document.documentElement.classList.toggle('kiosk-mode', state.preferences.kiosk);
        const density = byId('productDensityButton');
        if (density) density.textContent = state.preferences.density === 'compact' ? 'Comfortable view' : 'Compact view';
        const kiosk = byId('productKioskButton');
        if (kiosk) kiosk.textContent = state.preferences.kiosk ? 'Exit kiosk' : 'Kiosk mode';
    }

    /* This function used to open by renaming five sidebar entries from a list
     * of its own - Overview / Grid Power / Solar / Control / Controller. Those
     * writes never survived: ensureNavigationHierarchy() in web/app.js reapplies
     * the route table's `name` on every mutation, so the only thing the list
     * produced was a flash of the wrong name and a second place to look when
     * the sidebar and the page title disagreed. The route table names the
     * pages; this function arranges and hides them. */
    function normalizeNavigation() {
        const wifi = document.querySelector('.nav-link[data-route="wifi"]');
        if (wifi) wifi.hidden = !isEngineering();
        const alarms = document.querySelector('.nav-link[data-route="alarms"]');
        const system = document.querySelector('.nav-link[data-route="system"]');
        if (alarms && system && alarms.nextElementSibling !== system) system.before(alarms);
        ensureEngineeringEntry();
        ensureMobileNavigation();
        /* Signing in and out changes which entries the bar may offer, and this
         * function is what the data-access observer in start() re-runs.
         * ensureMobileNavigation() only builds the bar once, so without this
         * the bar kept whatever it was given at load. */
        updateMobileNavigation();
    }

    function ensureEngineeringEntry() {
        const sidebar = byId('sidebar');
        if (!sidebar || byId('productEngineeringEntry')) return;
        const button = node('button', 'product-engineering-entry');
        button.id = 'productEngineeringEntry';
        button.type = 'button';
        button.innerHTML = '<span aria-hidden="true">🔒</span><span>Engineering</span>';
        button.addEventListener('click', () => {
            if (isEngineering()) location.hash = '#/system';
            else {
                const trigger = byId('engineeringAccessButton') || document.querySelector('[data-engineering-login]');
                if (trigger) trigger.click();
                else location.hash = '#/system';
            }
        });
        const footer = sidebar.querySelector('.sidebar-footer');
        sidebar.insertBefore(button, footer || null);
    }

    /* Which routes the bar offers is this module's decision. What they are
     * CALLED is not: the icon and both labels come from the route table in
     * web/app.js, the same record the sidebar, the page title and the
     * breadcrumb read. The bar used to carry its own five names, so a phone
     * called these pages Overview and Grid while everything else called them
     * Plant overview and Grid power.
     *
     * The narrow column shows the route's short form and the accessible name
     * stays the full one, so the label an operator is told over the phone is
     * what a screen reader announces and what a long-press reveals - a
     * rendering of one name, not a second name. */
    const MOBILE_ROUTES = ['dashboard', 'meters', 'inverters', 'alarms', 'control'];

    function ensureMobileNavigation() {
        if (byId('productMobileNav')) return;
        const ui = window.AutomatrixUi;
        const nav = node('nav', 'product-mobile-nav');
        nav.id = 'productMobileNav';
        nav.setAttribute('aria-label', 'Operator shortcuts');
        MOBILE_ROUTES.forEach((route) => {
            const meta = ui?.ROUTES?.[route];
            /* No invented fallback name. If the route table has not published
             * yet the entry is not built, and normalizeNavigation() builds it
             * on the next pass, rather than shipping a made-up label. */
            if (!meta) return;
            const link = node('a', 'product-mobile-link');
            link.href = `#/${route}`;
            link.dataset.route = route;
            link.append(node('span', '', meta.icon), node('small', '', ui.routeShortName(route)));
            link.firstElementChild.setAttribute('aria-hidden', 'true');
            link.setAttribute('aria-label', ui.routeName(route));
            link.title = ui.routeName(route);
            nav.append(link);
        });
        if (!nav.childElementCount) return;
        document.body.append(nav);
        updateMobileNavigation();
    }

    /* Selection, and whether the entry may be offered at all.
     *
     * The bar carried PV-DG control unconditionally. That route is protected,
     * so a signed-out operator who tapped it was answered with the Engineering
     * sign-in page: the one navigation entry in the product that could not
     * reach the page it named. It is now hidden exactly while it is
     * unreachable, from the same set web/product-mode.js enforces, so the two
     * cannot drift. Routes that are merely engineering-flavoured but openable -
     * grid power, solar inverters - are untouched here; what an operator should
     * be offered is a product decision, not this function's. */
    function updateMobileNavigation() {
        const route = currentRoute();
        const access = window.AutomatrixEngineeringAccess;
        document.querySelectorAll('.product-mobile-link').forEach((link) => {
            link.classList.toggle('active', link.dataset.route === route);
            const unreachable = Boolean(access?.isProtectedRoute?.(link.dataset.route)) && !isEngineering();
            if (link.hidden !== unreachable) link.hidden = unreachable;
        });
    }

    function ensureTopbarControls() {
        const actions = document.querySelector('.topbar-actions');
        if (!actions || byId('productDensityButton')) return;
        const density = node('button', 'icon-button product-tool-button', 'Compact view');
        density.id = 'productDensityButton';
        density.type = 'button';
        density.title = 'Change information density';
        density.addEventListener('click', () => {
            state.preferences.density = state.preferences.density === 'compact' ? 'comfortable' : 'compact';
            savePreferences();
        });
        const kiosk = node('button', 'icon-button product-tool-button', 'Kiosk mode');
        kiosk.id = 'productKioskButton';
        kiosk.type = 'button';
        kiosk.title = 'Toggle full-screen plant display';
        kiosk.addEventListener('click', async () => {
            state.preferences.kiosk = !state.preferences.kiosk;
            savePreferences();
            try {
                if (state.preferences.kiosk && !document.fullscreenElement) await document.documentElement.requestFullscreen?.();
                else if (!state.preferences.kiosk && document.fullscreenElement) await document.exitFullscreen?.();
            } catch (_) { /* browser may block fullscreen */ }
        });
        actions.insertBefore(density, actions.lastElementChild);
        actions.insertBefore(kiosk, actions.lastElementChild);
        applyPreferences();
    }

    function ensureModal() {
        if (state.modal) return state.modal;
        const backdrop = node('div', 'product-modal-backdrop');
        backdrop.hidden = true;
        const dialog = node('section', 'product-modal');
        dialog.setAttribute('role', 'dialog');
        dialog.setAttribute('aria-modal', 'true');
        const close = node('button', 'product-modal-close', '×');
        close.type = 'button';
        close.setAttribute('aria-label', 'Close equipment details');
        const content = node('div', 'product-modal-content');
        close.addEventListener('click', () => closeModal());
        backdrop.addEventListener('click', (event) => { if (event.target === backdrop) closeModal(); });
        dialog.append(close, content);
        backdrop.append(dialog);
        document.body.append(backdrop);
        state.modal = { backdrop, content };
        return state.modal;
    }

    function closeModal() {
        if (!state.modal) return;
        state.modal.backdrop.hidden = true;
        document.body.classList.remove('modal-open');
    }

    function formatPower(value) {
        const number = Number(value);
        return Number.isFinite(number) ? `${number.toFixed(Math.abs(number) >= 100 ? 1 : 2)} kW` : 'Not available';
    }

    function formatAge(value) {
        const ms = Number(value);
        if (!Number.isFinite(ms) || ms < 0) return 'Unknown';
        if (ms < 1000) return 'Just now';
        if (ms < 60000) return `${Math.round(ms / 1000)} seconds ago`;
        return `${Math.round(ms / 60000)} minutes ago`;
    }

    function detailRow(label, value, tone = '') {
        const row = node('div', `product-detail-row ${tone}`);
        row.append(node('span', '', label), node('strong', '', value));
        return row;
    }

    function openMeterDetails(index) {
        const meter = state.payload?.meters?.meters?.find((item) => Number(item.index) === Number(index));
        if (!meter) return;
        const runtime = meter.runtime || {};
        const modal = ensureModal();
        modal.content.replaceChildren();
        modal.content.append(
            node('p', 'eyebrow', 'Grid measurement equipment'),
            node('h2', '', meter.name || `Grid meter ${Number(index) + 1}`),
            node('p', 'product-modal-intro', runtime.online ? 'This meter is providing current grid measurements.' : 'This meter requires attention before its data can be used.'),
            detailRow('Operating state', runtime.online ? 'Online' : meter.enabled ? 'Attention required' : 'Disabled', runtime.online ? 'good' : 'warning'),
            detailRow('Current grid power', formatPower(runtime.active_power_kw)),
            detailRow('Last measurement', formatAge(runtime.data_age_ms)),
            detailRow('Use in automatic control', runtime.online ? 'Available for control interlock' : 'Blocked for safety')
        );
        if (isEngineering()) {
            const action = node('button', 'button secondary', 'Open engineering diagnostics');
            action.type = 'button';
            action.addEventListener('click', () => { closeModal(); location.hash = '#/meters'; });
            modal.content.append(action);
        }
        modal.backdrop.hidden = false;
        document.body.classList.add('modal-open');
    }

    function openInverterDetails(index) {
        const inverter = state.payload?.inverters?.inverters?.find((item) => Number(item.index) === Number(index));
        const live = state.payload?.inverterTelemetry?.inverters?.find((item) => Number(item.index) === Number(index));
        if (!inverter) return;
        const online = inverter.runtime?.online === true || live?.online === true;
        const measured = live?.telemetry_valid ? live.measured_power_kw : inverter.measured_power_kw;
        const rated = Number(inverter.rated_kw) || 0;
        const utilization = rated > 0 && Number.isFinite(Number(measured)) ? Math.max(0, Math.min(100, Number(measured) / rated * 100)) : NaN;
        const modal = ensureModal();
        modal.content.replaceChildren();
        modal.content.append(
            node('p', 'eyebrow', 'Solar generation equipment'),
            node('h2', '', inverter.name || `Solar inverter ${Number(index) + 1}`),
            node('p', 'product-modal-intro', online ? 'This inverter is available to the monitoring system.' : 'This inverter is not currently available.'),
            detailRow('Operating state', online ? 'Online' : inverter.enabled ? 'Attention required' : 'Disabled', online ? 'good' : 'warning'),
            detailRow('Rated capacity', formatPower(rated)),
            detailRow('Measured production', formatPower(measured)),
            detailRow('Capacity utilization', Number.isFinite(utilization) ? `${utilization.toFixed(1)}%` : 'Not monitored'),
            detailRow('Automatic-control eligibility', live?.telemetry_valid && online ? 'Telemetry ready; profile qualification still applies' : 'Not available')
        );
        if (isEngineering()) {
            const action = node('button', 'button secondary', 'Open engineering configuration');
            action.type = 'button';
            action.addEventListener('click', () => { closeModal(); location.hash = '#/inverters'; });
            modal.content.append(action);
        }
        modal.backdrop.hidden = false;
        document.body.classList.add('modal-open');
    }

    function attachEquipmentDrilldown() {
        document.addEventListener('click', (event) => {
            const meter = event.target.closest('.op-equipment-bar');
            if (meter && currentRoute() === 'meters') {
                const rows = [...document.querySelectorAll('.op-equipment-bar')];
                openMeterDetails(rows.indexOf(meter));
                return;
            }
            const inverter = event.target.closest('.op-inverter-row');
            if (inverter && currentRoute() === 'inverters') {
                const rows = [...document.querySelectorAll('.op-inverter-row')];
                openInverterDetails(rows.indexOf(inverter));
            }
        });
    }

    /* The page and its navigation entry are established independently.
     *
     * These two used to share one guard: an early return on the page already
     * existing skipped the navigation entry as well. Commissioning is not the
     * only module that creates [data-page="commissioning"] -
     * web/commissioning-release-v3.js builds the same section whenever it
     * starts on that route, and it starts from an access-scope change - so
     * whichever ran first decided whether Commissioning appeared in the
     * sidebar at all. A page reached only by typing its URL is a page the
     * operator reports as missing, and nothing about the ordering that
     * currently saves us is guaranteed. web/operator-operations.js and
     * web/prelab-readiness.js already keep the two checks apart; this matches
     * them. Placement within the sidebar remains app.js's decision. */
    function ensureCommissioningPage() {
        const main = byId('mainContent');
        if (main && !main.querySelector('[data-page="commissioning"]')) {
            const page = node('section', 'page engineering-only');
            page.dataset.page = 'commissioning';
            page.innerHTML = '<div id="commissioningWizard" class="commissioning-wizard"></div>';
            main.append(page);
        }
        const nav = document.querySelector('.nav-list');
        if (nav && !nav.querySelector('[data-route="commissioning"]')) {
            const link = node('a', 'nav-link engineering-only');
            link.href = '#/commissioning';
            link.dataset.route = 'commissioning';
            link.innerHTML = '<span aria-hidden="true">✓</span><span>Commissioning</span>';
            nav.append(link);
            window.AutomatrixUi?.ensureNavigationHierarchy();
        }
    }

    function commissioningSteps() {
        const payload = state.payload || {};
        const status = payload.status || {};
        const meters = payload.meters?.summary || {};
        const inverters = payload.inverters?.summary || {};
        const telemetry = payload.inverterTelemetry?.summary || {};
        return [
            { id: 'network', title: 'Network connection', route: 'network', complete: status.network_online === true, detail: status.network_online ? `${status.ssid || 'Network'} connected` : 'Connect the controller to the site network.' },
            { id: 'meter', title: 'Grid measurement', route: 'meters', complete: Number(meters.online) > 0, detail: Number(meters.online) > 0 ? `${meters.online} meter online` : 'Configure and verify the primary grid meter.' },
            { id: 'inverters', title: 'Solar inverter fleet', route: 'inverters', complete: Number(inverters.enabled) > 0, detail: Number(inverters.enabled) > 0 ? `${inverters.enabled} inverter channel(s) enabled` : 'Configure inverter endpoints and rated capacity.' },
            { id: 'readonly', title: 'Read-only verification', route: 'inverters', complete: Number(telemetry.telemetry_valid) > 0, detail: Number(telemetry.telemetry_valid) > 0 ? `${telemetry.telemetry_valid} telemetry channel(s) verified` : 'Run read-only probes and compare live values.' },
            { id: 'safety', title: 'Safety readiness', route: 'control', complete: status.meter_online && !status.meter_stale && Number(status.alarms || 0) === 0, detail: Number(status.alarms || 0) === 0 ? 'No active safety alarm' : 'Resolve active safety conditions.' },
            { id: 'handover', title: 'Site handover report', route: 'commissioning', complete: false, detail: 'Export the commissioning report after all checks are complete.' },
        ];
    }

    function renderCommissioning() {
        const root = byId('commissioningWizard');
        if (!root || !isEngineering() || currentRoute() !== 'commissioning') return;
        const steps = commissioningSteps();
        const completed = steps.slice(0, -1).filter((step) => step.complete).length;
        const total = steps.length - 1;
        root.replaceChildren();
        const heading = node('div', 'op-section-head');
        const copy = node('div');
        copy.append(node('p', 'eyebrow', 'Protected engineering workflow'), node('h2', '', 'Guided site commissioning'), node('p', '', 'Complete the site in a safe order. Automatic control remains locked until profile and physical qualification are approved.'));
        const progress = node('div', 'commissioning-progress');
        progress.append(node('strong', '', `${completed}/${total}`), node('span', '', 'required checks complete'));
        heading.append(copy, progress);
        root.append(heading);
        const bar = node('div', 'commissioning-progress-bar');
        const fill = node('span');
        fill.style.width = `${total ? completed / total * 100 : 0}%`;
        bar.append(fill);
        root.append(bar);
        const list = node('div', 'commissioning-step-list');
        steps.forEach((step, index) => {
            const card = node('article', `commissioning-step ${step.complete ? 'complete' : ''}`);
            const number = node('span', 'commissioning-step-number', step.complete ? '✓' : index + 1);
            const info = node('div');
            info.append(node('h3', '', step.title), node('p', '', step.detail));
            const action = node('button', 'button secondary', step.id === 'handover' ? 'Export report' : step.complete ? 'Review' : 'Open step');
            action.type = 'button';
            action.addEventListener('click', () => step.id === 'handover' ? exportCommissioningReport() : location.hash = `#/${step.route}`);
            card.append(number, info, action);
            list.append(card);
        });
        root.append(list);
        const warning = node('article', 'notice warning');
        warning.innerHTML = '<strong>Production control gate:</strong><span>Simulator verification is not physical inverter approval. Keep automatic control disabled until exact manuals, bench read/write qualification, command readback, and rollback tests are complete.</span>';
        root.append(warning);
    }

    function downloadJson(filename, data) {
        const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const link = document.createElement('a');
        link.href = url;
        link.download = filename;
        document.body.append(link);
        link.click();
        link.remove();
        URL.revokeObjectURL(url);
    }

    function exportCommissioningReport() {
        const payload = state.payload || {};
        const steps = commissioningSteps();
        const report = {
            report_type: 'Automatrix PV-DG commissioning report',
            generated_at: new Date().toISOString(),
            controller: {
                ip: payload.status?.ip || null,
                network: payload.status?.ssid || null,
                online: payload.status?.network_online === true,
                automatic_control_enabled: payload.status?.control_enabled === true,
                alarm_count: Array.isArray(payload.status?.alarm_names) ? payload.status.alarm_names.length : 0,
            },
            checks: steps.slice(0, -1).map((step) => ({ id: step.id, title: step.title, passed: step.complete, detail: step.detail })),
            meters: (payload.meters?.meters || []).map((meter) => ({ name: meter.name, enabled: meter.enabled, online: meter.runtime?.online === true, active_power_kw: meter.runtime?.active_power_kw ?? null })),
            inverters: (payload.inverters?.inverters || []).map((inverter) => ({ name: inverter.name, enabled: inverter.enabled, rated_kw: inverter.rated_kw, online: inverter.runtime?.online === true, measured_power_kw: inverter.measured_power_kw ?? null })),
            safety_statement: 'Automatic control and physical inverter writes remain locked until exact model-specific qualification and production approval.',
        };
        const stamp = new Date().toISOString().slice(0, 19).replace(/[:T]/g, '-');
        downloadJson(`automatrix-commissioning-${stamp}.json`, report);
    }

    async function refreshPayload() {
        const [status, meters, inverters, inverterTelemetry] = await Promise.all([
            api('/api/status'), api('/api/meters'), api('/api/inverters'), api('/api/inverter-telemetry')
        ]);
        state.payload = { status, meters, inverters, inverterTelemetry };
        renderCommissioning();
    }

    function handleRoute() {
        /* Arriving on the page reads immediately; waiting for the next ten
         * second tick would show a commissioning report up to ten seconds
         * older than the moment it was opened. */
        if (currentRoute() === 'commissioning') refreshPayload().catch(() => {});
        normalizeNavigation();
        updateMobileNavigation();
        ensureCommissioningPage();
        if (currentRoute() === 'commissioning' && !isEngineering()) location.hash = '#/dashboard';
        renderCommissioning();
    }

    function start() {
        loadPreferences();
        ensureTopbarControls();
        ensureModal();
        ensureCommissioningPage();
        normalizeNavigation();
        attachEquipmentDrilldown();
        if (currentRoute() === 'commissioning') {
            refreshPayload().catch((error) => console.warn('Product suite refresh failed:', error));
        }
        window.addEventListener('hashchange', handleRoute);
        document.addEventListener('keydown', (event) => { if (event.key === 'Escape') closeModal(); });
        document.addEventListener('fullscreenchange', () => {
            if (!document.fullscreenElement && state.preferences.kiosk) {
                state.preferences.kiosk = false;
                savePreferences();
            }
        });
        new MutationObserver(() => {
            normalizeNavigation();
            ensureCommissioningPage();
            renderCommissioning();
        }).observe(document.documentElement, { attributes: true, attributeFilter: ['data-access'] });
        /*
         * POLLED ONLY WHERE IT IS READ.
         *
         * This payload feeds renderCommissioning() and nothing else, yet it was
         * fetched every ten seconds on every page: four endpoints -- status,
         * meters, inverters and inverter-telemetry -- that another module was
         * already asking for, on a controller with very few client sockets. The
         * duplicates the owner found in the network tab were largely these.
         */
        state.timer = window.setInterval(() => {
            if (currentRoute() !== 'commissioning') return;
            refreshPayload().catch(() => {});
        }, 10000);
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();
