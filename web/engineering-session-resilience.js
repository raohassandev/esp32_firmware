(() => {
    'use strict';

    const RENEW_MS = 5 * 60 * 1000;
    const REQUEST_TIMEOUT_MS = 5000;
    const nativeFetch = window.fetch.bind(window);
    let bootstrapPromise = null;
    let lastBootstrapAt = 0;
    let renewTimer = null;
    let sessionController = null;

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

    function cancelRenewal() {
        if (renewTimer) window.clearTimeout(renewTimer);
        renewTimer = null;
    }

    function cancelSessionRequest() {
        sessionController?.abort();
        sessionController = null;
    }

    async function establishSession(force = false) {
        const now = Date.now();
        if (!force && now - lastBootstrapAt < 15000) return true;
        if (bootstrapPromise) return bootstrapPromise;
        bootstrapPromise = (async () => {
            const controller = new AbortController();
            sessionController = controller;
            const timeout = window.setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS);
            try {
                const response = await nativeFetch('/api/engineering/session', {
                    cache: 'no-store',
                    credentials: 'same-origin',
                    signal: controller.signal
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
                /* Controller can be restarting, moving networks, or this browser
                 * request can reach its own bounded deadline. */
            } finally {
                window.clearTimeout(timeout);
                if (sessionController === controller) sessionController = null;
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

    function scheduleRenewal(delay = RENEW_MS) {
        cancelRenewal();
        if (document.visibilityState !== 'visible') return;
        renewTimer = window.setTimeout(async () => {
            renewTimer = null;
            await establishSession(true);
            scheduleRenewal();
        }, delay);
    }

    function renew() {
        cancelRenewal();
        if (document.visibilityState !== 'visible') {
            cancelSessionRequest();
            return;
        }
        establishSession(true).finally(() => scheduleRenewal());
    }

    function start() {
        establishSession(true).finally(() => scheduleRenewal());
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', start, { once: true });
    } else {
        start();
    }
    document.addEventListener('visibilitychange', renew);
    window.addEventListener('focus', renew);
    window.addEventListener('online', renew);
    window.addEventListener('beforeunload', () => {
        cancelRenewal();
        cancelSessionRequest();
    });
})();