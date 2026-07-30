/* product-mode.js - engineering access, request scope, shared DOM observer.
 *
 * OWNS: engineering authentication state and documentElement.dataset.access;
 *   which DOM regions are engineering-only and their hidden/aria-hidden state
 *   (presentation, NOT authorization - the server's default-deny gateway in
 *   engineering_guard.c is the barrier); the request-scope predicate, route
 *   AND authorisation, that every module consults before an Engineering
 *   request (audit S4); the one MutationObserver on #mainContent, republished
 *   as onContentChange; the Engineering nav entry, page and forms.
 * DOES NOT OWN: routing. It never activates a page or writes the title or
 *   breadcrumb - app.js does - it only sends an unauthorised caller to sign
 *   in. Navigation order and labels belong to product-shell-v2.js.
 */
(() => {
    'use strict';

    document.documentElement.dataset.access = 'pending';

    /* Development convenience: pre-fills the Engineering sign-in field on the bench.
     * Kept EMPTY in the repository - this project is public, so a real controller
     * password committed here would be published permanently. Set it locally while
     * working on a bench unit and do not commit that change.
     * tests/production_release_gate.py blocks a production release while it is non-empty. */
    const DEV_DEFAULT_ENGINEERING_PASSWORD = '';

    const PROTECTED_ROUTES = new Set(['wifi', 'control', 'system', 'commissioning']);
    const ENGINEERING_ONLY_SELECTORS = [
        '#em500Workspace',
        '#inverterProfilePicker',
        '#inverterConfigurationEditor',
        '#meterConfigurationEditor',
        '#controlRampEditor',
        '[data-page="system"] .dashboard-grid > article:nth-child(2)',
        '[data-page="system"] .panel-actions'
    ];
    /* Engineering-only APIs, paired with the routes that legitimately need them.
     * Modules ask before they fetch, because an unconditional request costs more
     * here than a console line: the controller serves a very small client socket
     * pool, so every request that can only answer 401 holds a socket another
     * client needs; each response also allocates on a device whose minimum free
     * heap has been measured close to its own critical threshold; and the failure
     * is printed on the customer-facing operator screen. See finding S4 in
     * docs/UI_VISUAL_AUDIT_2026-07-29.md. */
    const ENGINEERING_ONLY_ENDPOINTS = [
        { path: '/api/wifi/scan', routes: ['wifi', 'commissioning'] },
        { path: '/api/wifi/config', routes: ['wifi', 'commissioning'] },
        { path: '/api/solar-grid/config', routes: ['control', 'commissioning'] },
        { path: '/api/solar-grid/status', routes: ['control', 'commissioning'] },
        { path: '/api/inverter-profiles', routes: ['inverters', 'commissioning'] },
        { path: '/api/inverter-profile-assignment', routes: ['inverters', 'commissioning'] },
        { path: '/api/inverter-probe', routes: ['inverters', 'commissioning'] },
        { path: '/api/inverters/config', routes: ['inverters', 'commissioning'] },
        { path: '/api/meters/em500/', routes: ['meters', 'commissioning'] }
    ];

    const state = { authenticated: false, sessionExpired: false };
    const originalFetch = window.fetch.bind(window);
    let renewalPromise = null;

    function currentRoute() {
        return location.hash.replace(/^#\/?/, '') || 'dashboard';
    }

    /* The single scope predicate: authorised AND standing on a route that needs
     * the data. Modules must reuse this rather than reading auth state directly. */
    function engineeringScopeAllows(routes) {
        if (!state.authenticated) return false;
        if (!Array.isArray(routes) || routes.length === 0) return true;
        return routes.includes(currentRoute());
    }

    function engineeringEndpointScope(path) {
        const url = String(path || '').split('?', 1)[0];
        return ENGINEERING_ONLY_ENDPOINTS.find((entry) => url.startsWith(entry.path)) || null;
    }

    /* Operator endpoints (/api/status, /api/telemetry, /api/meters, /api/operator/*)
     * are not listed above and therefore stay permitted without authentication. */
    function mayRequest(path) {
        const scope = engineeringEndpointScope(path);
        return scope ? engineeringScopeAllows(scope.routes) : true;
    }

    /* Route changes and sign-in/sign-out are the only two events that can widen
     * or narrow a module's scope, so both re-run the caller's gated loader.
     * This is what makes data appear straight after login without a refresh. */
    function onScopeChange(handler) {
        if (typeof handler !== 'function') return;
        window.addEventListener('amx-access-change', handler);
        window.addEventListener('hashchange', handler);
    }

    /* ------------------------------------------------ single content observer
     *
     * product-shell-v2.js and product-experience-v2.js each installed their own
     * observer on #mainContent alongside this module's visibility observer,
     * every one of them re-deriving "did a page appear?" from the same records.
     * There is now one, and it cannot react to its own subscribers' writes:
     * records produced while subscribers run are drained with takeRecords() and
     * discarded before the observer is armed again, so a subscriber that starts
     * adding children to #mainContent still cannot open a feedback loop. */
    const contentSubscribers = [];
    let contentObserver = null;
    let notifying = false;

    /* A subscriber hears only about a page added to or removed from
     * #mainContent; descendant churn inside a live meter workspace is constant
     * and of no interest to the shell. `{ deep: true }` opts in. */
    function onContentChange(handler, options = {}) {
        if (typeof handler !== 'function') return;
        contentSubscribers.push({ handler, deep: Boolean(options.deep) });
    }

    function notifyContentChange(records) {
        if (notifying) return;
        const structural = records.some((record) => record.target === document.getElementById('mainContent'));
        notifying = true;
        try {
            enforceEngineeringDom();
            contentSubscribers.forEach((entry) => {
                if (!structural && !entry.deep) return;
                try { entry.handler(); } catch {}
            });
        } finally {
            contentObserver?.takeRecords();
            notifying = false;
        }
    }

    async function renewEngineeringSession() {
        if (renewalPromise) return renewalPromise;
        renewalPromise = (async () => {
            try {
                const response = await originalFetch('/api/engineering/session', {
                    cache: 'no-store', credentials: 'same-origin'
                });
                const payload = await response.json().catch(() => ({}));
                if (response.ok && payload.authenticated === true) {
                    state.sessionExpired = false;
                    setEngineering(true);
                    return true;
                }
            } catch {
                /* Controller restart or network transition; do not force a route change. */
            }
            return false;
        })().finally(() => { renewalPromise = null; });
        return renewalPromise;
    }

    async function retryProtectedRequest(input, init) {
        const renewed = await renewEngineeringSession();
        if (!renewed) return null;
        return originalFetch(input, { credentials: 'same-origin', ...init });
    }

    function markSessionExpired(message = 'Engineering session expired. Sign in again.') {
        state.sessionExpired = true;
        setEngineering(false);
        const target = document.getElementById('engineeringLoginMessage');
        if (target) target.textContent = message;
        window.dispatchEvent(new CustomEvent('amx-engineering-auth-required', {
            detail: { route: currentRoute(), message }
        }));
    }

    window.fetch = async (input, init = {}) => {
        const url = typeof input === 'string' ? input : input?.url || '';
        let response = await originalFetch(input, { credentials: 'same-origin', ...init });
        const protectedApi = url.startsWith('/api/') && !url.includes('/engineering/');
        const protectedRoute = PROTECTED_ROUTES.has(currentRoute());
        if (response.status === 401 && protectedApi && protectedRoute) {
            const retried = await retryProtectedRequest(input, init);
            if (retried) response = retried;
            if (response.status === 401) markSessionExpired();
        }
        return response;
    };

    function node(tag, className = '', text = '') {
        const element = document.createElement(tag);
        if (className) element.className = className;
        if (text) element.textContent = text;
        return element;
    }

    function enforceEngineeringDom() {
        ENGINEERING_ONLY_SELECTORS.forEach((selector) => {
            document.querySelectorAll(selector).forEach((item) => {
                item.hidden = !state.authenticated;
                item.setAttribute('aria-hidden', state.authenticated ? 'false' : 'true');
            });
        });
    }

    function setEngineering(authenticated) {
        state.authenticated = Boolean(authenticated);
        document.documentElement.dataset.access = state.authenticated ? 'engineering' : 'operator';
        document.body?.classList.toggle('engineering-authenticated', state.authenticated);
        document.querySelectorAll('[data-engineering-nav]').forEach((item) => item.hidden = !state.authenticated);
        const badge = document.getElementById('engineeringAccessBadge');
        if (badge) badge.textContent = state.authenticated ? 'Engineering unlocked' : 'Engineering locked';
        const lock = document.getElementById('engineeringLockIcon');
        if (lock) lock.textContent = state.authenticated ? '🔓' : '🔒';
        const logout = document.getElementById('engineeringLogout');
        if (logout) logout.hidden = !state.authenticated;
        enforceEngineeringDom();
        window.dispatchEvent(new CustomEvent('amx-access-change', {
            detail: { authenticated: state.authenticated }
        }));
    }

    function addNavigation() {
        const nav = document.querySelector('.nav-list');
        if (!nav || document.getElementById('engineeringNav')) return;
        ['wifi', 'control', 'system', 'meters', 'inverters', 'commissioning'].forEach((route) => {
            const link = nav.querySelector(`[data-route="${route}"]`);
            if (link) {
                link.dataset.engineeringNav = 'true';
                link.hidden = !state.authenticated;
            }
        });
        const engineering = document.createElement('a');
        engineering.id = 'engineeringNav';
        engineering.className = 'nav-link engineering-link';
        engineering.href = '#/engineering';
        engineering.dataset.route = 'engineering';
        engineering.innerHTML = '<span aria-hidden="true">▣</span><span>Engineering</span><b id="engineeringLockIcon">🔒</b>';
        nav.append(engineering);
    }

    function injectEngineeringPage() {
        const main = document.getElementById('mainContent');
        if (!main || document.querySelector('[data-page="engineering"]')) return;
        const page = node('section', 'page engineering-page');
        page.dataset.page = 'engineering';
        page.innerHTML = `
            <div class="page-intro"><div><p class="eyebrow">Restricted access</p><h2>Engineering and commissioning</h2><p>Protected tools for authorized commissioning and service personnel.</p></div><span class="subtle-badge" id="engineeringAccessBadge">Engineering locked</span></div>
            <div class="engineering-login panel" id="engineeringLoginPanel">
                <div><p class="eyebrow">Authentication</p><h3>Engineering sign in</h3><p>Use the device engineering password. The session remains active while commissioning work continues.</p></div>
                <form id="engineeringLoginForm"><label class="field"><span>Engineering password</span><input id="engineeringPassword" type="password" autocomplete="current-password" minlength="8" maxlength="64" required></label><button class="button primary" type="submit">Unlock engineering</button></form>
                <div class="action-message" id="engineeringLoginMessage" role="status"></div>
            </div>
            <div class="engineering-console" id="engineeringConsole">
                <div class="engineering-actions"><span>Engineering access is active. Automatic control remains locked during commissioning.</span><button class="button secondary" id="engineeringLogout" type="button">Lock engineering</button></div>
                <div class="engineering-grid">
                    <a class="panel engineering-tile primary-workflow" href="#/commissioning"><span>Guided commissioning</span><strong>Commission a site and its devices</strong><small>Site details, devices, channels, Modbus tuning, connection qualification, controller health and report.</small></a>
                    <a class="panel engineering-tile" href="#/wifi"><span>Network</span><strong>Wi-Fi and addressing</strong><small>Primary/fallback networks, recovery AP and static IP settings.</small></a>
                    <a class="panel engineering-tile" href="#/meters"><span>Meters</span><strong>Meter profiles and diagnostics</strong><small>Endpoints, scaling, register maps, raw data and advanced diagnostics.</small></a>
                    <a class="panel engineering-tile" href="#/inverters"><span>Inverters</span><strong>Profiles and communication</strong><small>Model assignment, endpoints, read-only probes and telemetry qualification.</small></a>
                    <a class="panel engineering-tile" href="#/control"><span>Control</span><strong>PV-DG parameters</strong><small>Targets, deadband, timing and safety interlocks.</small></a>
                    <a class="panel engineering-tile" href="#/system"><span>Service</span><strong>Backup and controller service</strong><small>Configuration export, advanced JSON, password management and restart.</small></a>
                </div>
                <article class="panel password-panel"><div><p class="eyebrow">Security</p><h3>Change engineering password</h3></div><form id="engineeringPasswordForm" class="password-form"><label class="field"><span>Current password</span><input id="engineeringCurrentPassword" type="password" required></label><label class="field"><span>New password</span><input id="engineeringNewPassword" type="password" minlength="10" maxlength="64" required></label><button class="button primary" type="submit">Change password</button></form><div id="engineeringPasswordMessage" class="action-message"></div></article>
            </div>`;
        main.append(page);
    }

    /* Route selection and the page title belong to app.js. This module only
     * moves the user to the sign-in route; the router notices the hash change,
     * or the content observer notices the injected page, and activates it. */
    function openLogin(message = '') {
        if (location.hash !== '#/engineering') location.hash = '#/engineering';
        setTimeout(() => {
            const input = document.getElementById('engineeringPassword');
            const target = document.getElementById('engineeringLoginMessage');
            if (target) target.textContent = message;
            if (input && !input.value && DEV_DEFAULT_ENGINEERING_PASSWORD) {
                input.value = DEV_DEFAULT_ENGINEERING_PASSWORD;
            }
            input?.focus();
        }, 0);
    }

    async function refreshSession() {
        const authenticated = await renewEngineeringSession();
        if (!authenticated) setEngineering(false);
    }

    async function login(password) {
        const response = await originalFetch('/api/engineering/login', {
            method: 'POST',
            credentials: 'same-origin',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ password })
        });
        const payload = await response.json().catch(() => ({}));
        if (!response.ok) throw new Error(payload.error || 'Engineering login failed');
        state.sessionExpired = false;
        setEngineering(true);
        return payload;
    }

    function bindForms() {
        document.getElementById('engineeringLoginForm')?.addEventListener('submit', async (event) => {
            event.preventDefault();
            const message = document.getElementById('engineeringLoginMessage');
            const password = document.getElementById('engineeringPassword');
            try {
                message.textContent = 'Authenticating…';
                const result = await login(password.value);
                password.value = '';
                message.textContent = result.password_change_recommended
                    ? 'Unlocked. Change the temporary password below.'
                    : 'Engineering access unlocked.';
            } catch (error) {
                message.textContent = error.message;
            }
        });
        document.getElementById('engineeringLogout')?.addEventListener('click', async () => {
            try {
                await originalFetch('/api/engineering/logout', { method: 'POST', credentials: 'same-origin' });
            } catch {}
            state.sessionExpired = false;
            setEngineering(false);
            location.hash = '#/dashboard';
        });
        document.getElementById('engineeringPasswordForm')?.addEventListener('submit', async (event) => {
            event.preventDefault();
            const message = document.getElementById('engineeringPasswordMessage');
            const current = document.getElementById('engineeringCurrentPassword');
            const next = document.getElementById('engineeringNewPassword');
            try {
                const response = await originalFetch('/api/engineering/password', {
                    method: 'POST', credentials: 'same-origin',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ current_password: current.value, new_password: next.value })
                });
                const payload = await response.json().catch(() => ({}));
                if (!response.ok) throw new Error(payload.error || 'Password change failed');
                current.value = '';
                next.value = '';
                message.textContent = 'Engineering password changed successfully.';
            } catch (error) {
                message.textContent = error.message;
            }
        });
    }

    async function enforceRoute() {
        const route = currentRoute();
        if (PROTECTED_ROUTES.has(route) && !state.authenticated) {
            const restored = await renewEngineeringSession();
            if (!restored) openLogin('Sign in to open this engineering page.');
        }
    }

    document.addEventListener('DOMContentLoaded', async () => {
        addNavigation();
        injectEngineeringPage();
        bindForms();
        setEngineering(false);
        const main = document.getElementById('mainContent');
        if (main) {
            contentObserver = new MutationObserver(notifyContentChange);
            contentObserver.observe(main, { childList: true, subtree: true });
            /* The Engineering page was injected a few lines above, before the
             * observer existed. Announce it once so the router can select it. */
            contentSubscribers.forEach((entry) => { try { entry.handler(); } catch {} });
        }
        await refreshSession();
        await enforceRoute();
    });
    window.addEventListener('hashchange', enforceRoute);

    window.AutomatrixEngineeringAccess = Object.freeze({
        isAuthenticated: () => state.authenticated,
        renew: renewEngineeringSession,
        requireRoute: enforceRoute,
        currentRoute,
        mayRequest,
        mayUseEngineering: (...routes) => engineeringScopeAllows(routes.flat()),
        onScopeChange,
        onContentChange
    });
})();
