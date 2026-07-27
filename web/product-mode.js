(() => {
    'use strict';

    const TOKEN_KEY = 'amxEngineeringToken';
    const state = { token: sessionStorage.getItem(TOKEN_KEY) || '', authenticated: false };
    const originalFetch = window.fetch.bind(window);

    window.fetch = async (input, init = {}) => {
        const url = typeof input === 'string' ? input : input?.url || '';
        const headers = new Headers(init.headers || (typeof input !== 'string' ? input.headers : undefined));
        if (state.token && url.startsWith('/api/')) headers.set('X-Engineering-Token', state.token);
        const response = await originalFetch(input, { ...init, headers });
        if (response.status === 401 && url.startsWith('/api/') && !url.includes('/engineering/')) {
            setEngineering(false);
            openLogin('Engineering session expired. Sign in again.');
        }
        return response;
    };

    function node(tag, className = '', text = '') {
        const element = document.createElement(tag);
        if (className) element.className = className;
        if (text) element.textContent = text;
        return element;
    }

    function setEngineering(authenticated) {
        state.authenticated = Boolean(authenticated);
        document.documentElement.dataset.access = state.authenticated ? 'engineering' : 'operator';
        document.body?.classList.toggle('engineering-authenticated', state.authenticated);
        document.querySelectorAll('[data-engineering-nav]').forEach((item) => item.hidden = !state.authenticated);
        const badge = document.getElementById('engineeringAccessBadge');
        if (badge) badge.textContent = state.authenticated ? 'Engineering unlocked' : 'Engineering locked';
        const logout = document.getElementById('engineeringLogout');
        if (logout) logout.hidden = !state.authenticated;
    }

    function addNavigation() {
        const nav = document.querySelector('.nav-list');
        if (!nav || document.getElementById('engineeringNav')) return;
        ['wifi', 'control', 'system'].forEach((route) => {
            const link = nav.querySelector(`[data-route="${route}"]`);
            if (link) { link.dataset.engineeringNav = 'true'; link.hidden = !state.authenticated; }
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
                <div><p class="eyebrow">Authentication</p><h3>Engineering sign in</h3><p>Use the temporary password printed on the device label or serial boot log. Change it after first access.</p></div>
                <form id="engineeringLoginForm"><label class="field"><span>Engineering password</span><input id="engineeringPassword" type="password" autocomplete="current-password" minlength="8" maxlength="64" required></label><button class="button primary" type="submit">Unlock engineering</button></form>
                <div class="action-message" id="engineeringLoginMessage" role="status"></div>
            </div>
            <div class="engineering-console" id="engineeringConsole">
                <div class="engineering-actions"><span>Authenticated engineering session. Session closes automatically after 30 minutes of inactivity.</span><button class="button secondary" id="engineeringLogout" type="button">Lock engineering</button></div>
                <div class="engineering-grid">
                    <a class="panel engineering-tile" href="#/wifi"><span>Network commissioning</span><strong>Wi-Fi and addressing</strong><small>Primary/fallback networks, recovery AP and static IP settings.</small></a>
                    <a class="panel engineering-tile" href="#/meters"><span>Meter commissioning</span><strong>Meter profiles and diagnostics</strong><small>Endpoints, scaling, register maps, raw data and advanced diagnostics.</small></a>
                    <a class="panel engineering-tile" href="#/inverters"><span>Inverter commissioning</span><strong>Profiles and communication</strong><small>Model assignment, endpoints, read-only probes and telemetry qualification.</small></a>
                    <a class="panel engineering-tile" href="#/control"><span>Control engineering</span><strong>PV-DG parameters</strong><small>Targets, deadband, timing and safety interlocks.</small></a>
                    <a class="panel engineering-tile" href="#/system"><span>Service and maintenance</span><strong>Backup and controller service</strong><small>Configuration export, advanced JSON, password management and restart.</small></a>
                </div>
                <article class="panel password-panel"><div><p class="eyebrow">Security</p><h3>Change engineering password</h3></div><form id="engineeringPasswordForm" class="password-form"><label class="field"><span>Current password</span><input id="engineeringCurrentPassword" type="password" required></label><label class="field"><span>New password</span><input id="engineeringNewPassword" type="password" minlength="10" maxlength="64" required></label><button class="button primary" type="submit">Change password</button></form><div id="engineeringPasswordMessage" class="action-message"></div></article>
            </div>`;
        main.append(page);
    }

    function openLogin(message = '') {
        location.hash = '#/engineering';
        requestAnimationFrame(() => {
            const input = document.getElementById('engineeringPassword');
            const target = document.getElementById('engineeringLoginMessage');
            if (target) target.textContent = message;
            input?.focus();
        });
    }

    async function refreshSession() {
        try {
            const response = await originalFetch('/api/engineering/session', {
                cache: 'no-store',
                headers: state.token ? { 'X-Engineering-Token': state.token } : {}
            });
            const payload = await response.json();
            if (!payload.authenticated) {
                state.token = '';
                sessionStorage.removeItem(TOKEN_KEY);
            }
            setEngineering(payload.authenticated);
        } catch {
            setEngineering(false);
        }
    }

    async function login(password) {
        const response = await originalFetch('/api/engineering/login', {
            method: 'POST', headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ password })
        });
        const payload = await response.json().catch(() => ({}));
        if (!response.ok) throw new Error(payload.error || 'Engineering login failed');
        state.token = payload.token;
        sessionStorage.setItem(TOKEN_KEY, state.token);
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
                message.textContent = result.password_change_recommended ? 'Unlocked. Change the temporary password below.' : 'Engineering access unlocked.';
            } catch (error) { message.textContent = error.message; }
        });
        document.getElementById('engineeringLogout')?.addEventListener('click', async () => {
            try { await window.fetch('/api/engineering/logout', { method: 'POST' }); } catch {}
            state.token = '';
            sessionStorage.removeItem(TOKEN_KEY);
            setEngineering(false);
            location.hash = '#/dashboard';
        });
        document.getElementById('engineeringPasswordForm')?.addEventListener('submit', async (event) => {
            event.preventDefault();
            const message = document.getElementById('engineeringPasswordMessage');
            const current = document.getElementById('engineeringCurrentPassword');
            const next = document.getElementById('engineeringNewPassword');
            try {
                const response = await window.fetch('/api/engineering/password', {
                    method: 'POST', headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ current_password: current.value, new_password: next.value })
                });
                const payload = await response.json().catch(() => ({}));
                if (!response.ok) throw new Error(payload.error || 'Password change failed');
                state.token = payload.token;
                sessionStorage.setItem(TOKEN_KEY, state.token);
                current.value = ''; next.value = '';
                message.textContent = 'Engineering password changed successfully.';
            } catch (error) { message.textContent = error.message; }
        });
    }

    function enforceRoute() {
        const route = location.hash.replace(/^#\/?/, '') || 'dashboard';
        if (['wifi', 'control', 'system'].includes(route) && !state.authenticated) openLogin('Sign in to open this engineering page.');
    }

    document.addEventListener('DOMContentLoaded', async () => {
        addNavigation();
        injectEngineeringPage();
        bindForms();
        setEngineering(false);
        await refreshSession();
        enforceRoute();
    });
    window.addEventListener('hashchange', enforceRoute);
})();
