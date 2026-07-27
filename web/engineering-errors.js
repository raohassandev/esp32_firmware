(() => {
    'use strict';

    const FRIENDLY = new Map([
        ['ESP_ERR_INVALID_RESPONSE', 'Not supported by the selected meter profile, or the meter returned an invalid response for this register group.'],
        ['ESP_ERR_TIMEOUT', 'The meter did not respond before the communication timeout. Check the connection and selected profile.'],
        ['ESP_ERR_NOT_SUPPORTED', 'This measurement group is not supported by the selected meter profile.'],
        ['ESP_ERR_INVALID_STATE', 'The controller is not ready to read this measurement group.'],
        ['ESP_FAIL', 'The measurement request failed. Check meter communication and profile selection.']
    ]);

    function translate(text) {
        const source = String(text || '').trim();
        for (const [code, message] of FRIENDLY) {
            if (source.includes(code)) return { code, message };
        }
        return null;
    }

    function improveError(node) {
        if (!(node instanceof HTMLElement) || node.dataset.errorFriendly === 'true') return;
        const translated = translate(node.textContent);
        if (!translated) return;
        node.dataset.errorFriendly = 'true';
        node.classList.add('engineering-friendly-error');
        node.replaceChildren();
        const title = document.createElement('strong');
        title.textContent = translated.code === 'ESP_ERR_NOT_SUPPORTED' || translated.code === 'ESP_ERR_INVALID_RESPONSE'
            ? 'Measurement group unavailable'
            : 'Communication issue';
        const message = document.createElement('span');
        message.textContent = translated.message;
        const detail = document.createElement('small');
        detail.textContent = `Diagnostic code: ${translated.code}`;
        node.append(title, message, detail);

        const panel = node.closest('.em500-panel');
        const toggle = panel?.querySelector('.collapse-toggle');
        if (panel && translated.code === 'ESP_ERR_INVALID_RESPONSE') {
            panel.dataset.supportState = 'unavailable';
            if (toggle) toggle.textContent = 'Details';
        }
    }

    function scan(root = document) {
        root.querySelectorAll?.('.device-error, .em500-message.bad, .action-message').forEach(improveError);
    }

    function start() {
        scan();
        const target = document.getElementById('mainContent') || document.body;
        new MutationObserver((mutations) => {
            for (const mutation of mutations) {
                mutation.addedNodes.forEach((item) => {
                    if (item instanceof HTMLElement) {
                        improveError(item);
                        scan(item);
                    }
                });
                if (mutation.type === 'characterData' && mutation.target.parentElement) {
                    improveError(mutation.target.parentElement);
                }
            }
        }).observe(target, { childList: true, subtree: true, characterData: true });
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();