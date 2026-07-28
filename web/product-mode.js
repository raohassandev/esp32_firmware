(() => {
    'use strict';

    document.documentElement.dataset.access = 'pending';

    const PROTECTED_ROUTES = new Set(['wifi', 'control', 'system', 'commissioning']);
    const ENGINEERING_ONLY_SELECTORS = [
        '#em500Workspace',
        '#inverterProfilePicker',
        '#inverterConfigurationEditor',
        '#meterConfigurationEditor',
        '[data-page="system"] .dashboard-grid > article:nth-child(2)',
        '[data-page="system"] .panel-actions'
    ];
    const state = { authenticated: false };
    const originalFetch = window.fetch.bind(window);
    let renewalPromise = null;

    function currentRoute() {
        return location.hash.replace(/^#\/?/, '') || 'dashboard';
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
                    setEngineering(true);
                    return true;
                }
            } catch {
                /* Controller restart or network transition; do not force a false logout. */
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

    window.fetch = async (input, init = {}) => {
        const url = typeof input === 'string' ? input : input?.url || '';
        let response = await originalFetch(input, { credentials: 'same-origin', ...init });
        const protectedApi = url.startsWith('/api/') && !url.includes('/engineering/');
        const protectedRoute = currentRoute() === 'engineering' || PROTECTED_ROUTES.has(currentRoute());
        if (response.status === 401 && protectedApi && protectedRoute) {
            const retried = await retryProtectedRequest(input, init);
            if (retried) response = retried;
            if (response.status === 401) {
                setEngineering(false);
                openLogin('Engineering session expired. Sign in again.');
            }
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
        ['wifi', 'control', 'system', 'commissioning'].forEach((route) => {
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
                    <a class="panel engineering-tile" href="#/meters"><span>Meters</span><strong>Meter profiles and diagnostics</strong><small>Operator telemetry remains available without Engineering access; advanced controls remain hidden.</small></a>
                    <a class="panel engineering-tile" href="#/inverters"><span>Inverters</span><strong>Profiles and communication</strong><small>Model assignment, endpoints, read-only probes and telemetry qualification.</small></a>
                    <a class="panel engineering-tile" href="#/control"><span>Control</span><strong>PV-DG parameters</strong><small>Targets, deadband, timing and safety interlocks.</small></a>
                    <a class="panel engineering-tile" href="#/system"><span>Service</span><strong>Backup and controller service</strong><small>Configuration export, advanced JSON, password management and restart.</small></a>
                </div>
                <article class="panel password-panel"><div><p class="eyebrow">Security</p><h3>Change engineering password</h3></div><form id="engineeringPasswordForm" class="password-form"><label class="field"><span>Current password</span><input id="engineeringCurrentPassword" type="password" required></label><label class="field"><span>New password</span><input id="engineeringNewPassword" type="password" minlength="10" maxlength="64" required></label><button class="button primary" type="submit">Change password</button></form><div id="engineeringPasswordMessage" class="action-message"></div></article>
            </div>`;
        main.append(page);
    }

    function activateEngineeringRoute() {
        if (currentRoute() !== 'engineering') return;
        document.querySelectorAll('.page').forEach((page) => page.classList.toggle('active', page.dataset.page === 'engineering'));
        document.querySelectorAll('.nav-link').forEach((link) => link.classList.toggle('active', link.dataset.route === 'engineering'));
        const title = document.getElementById('pageTitle');
        const breadcrumb = document.getElementById('breadcrumbCurrent');
        if (title) title.textContent = 'Engineering';
        if (breadcrumb) breadcrumb.textContent = 'Restricted commissioning and service tools';
        document.title = 'Engineering · Automatrix PV-DG';
    }

    function openLogin(message = '') {
        if (location.hash !== '#/engineering') location.hash = '#/engineering';
        setTimeout(() => {
            activateEngineeringRoute();
            const input = document.getElementById('engineeringPassword');
            const target = document.getElementById('engineeringLoginMessage');
            if (target) target.textContent = message;
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
            return;
        }
        if (route === 'engineering') setTimeout(activateEngineeringRoute, 0);
    }

    document.addEventListener('DOMContentLoaded', async () => {
        addNavigation();
        injectEngineeringPage();
        bindForms();
        setEngineering(false);
        const main = document.getElementById('mainContent');
        if (main) new MutationObserver(enforceEngineeringDom).observe(main, { childList: true, subtree: true });
        await refreshSession();
        await enforceRoute();
    });
    window.addEventListener('hashchange', enforceRoute);
    window.addEventListener('amx-engineering-session-ready', () => setEngineering(true));
})();
