(() => {
    'use strict';

    const nativeFetch = window.fetch.bind(window);
    let bootstrapPromise = null;
    let lastBootstrapAt = 0;

    function requestUrl(input) {
        return typeof input === 'string' ? input : input?.url || '';
    }

    function isApi(url) {
        try {
            const parsed = new URL(url, location.href);
            return parsed.origin === location.origin && parsed.pathname.startsWith('/api/');
        } catch {
            return String(url).startsWith('/api/');
        }
    }

    function isAuthEndpoint(url) {
        return /\/api\/engineering\/(session|login|logout|password)(?:\?|$)/.test(url);
    }

    async function establishSession(force = false) {
        const now = Date.now();
        if (!force && now - lastBootstrapAt < 15000) return true;
        if (bootstrapPromise) return bootstrapPromise;
        bootstrapPromise = (async () => {
            try {
                const response = await nativeFetch('/api/engineering/session', {
                    cache: 'no-store',
                    credentials: 'same-origin'
                });
                const payload = await response.json().catch(() => ({}));
                if (response.ok && payload.authenticated === true) {
                    lastBootstrapAt = Date.now();
                    window.dispatchEvent(new CustomEvent('amx-engineering-session-ready', {
                        detail: payload
                    }));
                    return true;
                }
            } catch {
                /* Controller can be restarting or moving between networks. */
            }
            return false;
        })().finally(() => { bootstrapPromise = null; });
        return bootstrapPromise;
    }

    window.fetch = async (input, init = {}) => {
        const url = requestUrl(input);
        const options = { credentials: 'same-origin', ...init };
        if (isApi(url) && !isAuthEndpoint(url)) await establishSession(false);
        let response = await nativeFetch(input, options);
        if (response.status === 401 && isApi(url) && !isAuthEndpoint(url)) {
            const restored = await establishSession(true);
            if (restored) response = await nativeFetch(input, options);
        }
        return response;
    };

    function renew() {
        if (document.visibilityState === 'visible') establishSession(true);
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', () => establishSession(true), { once: true });
    } else {
        establishSession(true);
    }
    document.addEventListener('visibilitychange', renew);
    window.addEventListener('focus', renew);
    window.addEventListener('online', renew);
    window.setInterval(() => establishSession(true), 5 * 60 * 1000);
})();