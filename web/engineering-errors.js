(() => {
    'use strict';

    const FRIENDLY = new Map([
        ['ESP_ERR_INVALID_RESPONSE', 'Not supported by the selected meter profile, or the meter returned an invalid response for this register group.'],
        ['ESP_ERR_TIMEOUT', 'The meter did not respond before the communication timeout. Check the connection and selected profile.'],
        ['ESP_ERR_NOT_SUPPORTED', 'This measurement group is not supported by the selected meter profile.'],
        ['ESP_ERR_INVALID_STATE', 'The controller is not ready to read this measurement group.'],
        ['ESP_FAIL', 'The measurement request failed. Check meter communication and profile selection.']
    ]);

    const ERROR_SELECTOR = '.device-error, .em500-message.bad, .action-message';

    function translate(text) {
        const source = String(text || '').trim();
        for (const [code, message] of FRIENDLY) {
            if (source.includes(code)) return { code, message };
        }
        return null;
    }

    function improveError(node) {
        if (!(node instanceof HTMLElement)) return;
        if (!node.matches(ERROR_SELECTOR)) return;
        if (node.dataset.errorFriendly === 'true') return;
        if (node.closest('.engineering-friendly-error') && !node.classList.contains('engineering-friendly-error')) return;

        const translated = translate(node.textContent);
        if (!translated) return;

        node.dataset.errorFriendly = 'true';
        node.dataset.diagnosticCode = translated.code;
        node.classList.add('engineering-friendly-error');

        const friendly = document.createElement('div');
        friendly.className = 'engineering-friendly-copy';
        friendly.dataset.errorFriendly = 'true';

        const title = document.createElement('strong');
        title.dataset.errorFriendly = 'true';
        title.textContent = translated.code === 'ESP_ERR_NOT_SUPPORTED' || translated.code === 'ESP_ERR_INVALID_RESPONSE'
            ? 'Measurement group unavailable'
            : 'Communication issue';

        const message = document.createElement('span');
        message.dataset.errorFriendly = 'true';
        message.textContent = translated.message;

        const detail = document.createElement('small');
        detail.dataset.errorFriendly = 'true';
        detail.textContent = 'Technical diagnostic recorded.';

        friendly.append(title, message, detail);

        const hasInteractiveContent = node.querySelector('button, a, input, select, textarea');
        if (hasInteractiveContent) {
            node.prepend(friendly);
        } else {
            node.replaceChildren(friendly);
        }

        const panel = node.closest('.em500-panel');
        const toggle = panel?.querySelector('.collapse-toggle');
        if (panel && translated.code === 'ESP_ERR_INVALID_RESPONSE') {
            panel.dataset.supportState = 'unavailable';
            if (toggle) toggle.textContent = 'Details';
        }
    }

    function scan(root = document) {
        if (root instanceof HTMLElement && root.matches(ERROR_SELECTOR)) improveError(root);
        root.querySelectorAll?.(ERROR_SELECTOR).forEach(improveError);
    }

    function start() {
        scan();
        const target = document.getElementById('mainContent') || document.body;
        new MutationObserver((mutations) => {
            for (const mutation of mutations) {
                mutation.addedNodes.forEach((item) => {
                    if (item instanceof HTMLElement && !item.closest('.engineering-friendly-error')) scan(item);
                });
            }
        }).observe(target, { childList: true, subtree: true });
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();