(() => {
    'use strict';

    const OPEN_BY_DEFAULT = new Set(['Voltage', 'Current', 'Active power']);

    function decorateMeterPanels() {
        const content = document.getElementById('em500Content');
        if (!content) return;
        content.querySelectorAll('.em500-panel').forEach((panel) => {
            if (panel.dataset.collapsibleReady === 'true') return;
            const header = panel.querySelector(':scope > .panel-header');
            const title = header?.querySelector('h3')?.textContent?.trim() || 'Section';
            if (!header) return;

            const body = document.createElement('div');
            body.className = 'em500-collapsible-body';
            while (header.nextSibling) body.append(header.nextSibling);
            panel.append(body);

            const button = document.createElement('button');
            button.type = 'button';
            button.className = 'section-toggle';
            button.setAttribute('aria-expanded', OPEN_BY_DEFAULT.has(title) ? 'true' : 'false');
            const setState = (open) => {
                body.hidden = !open;
                button.setAttribute('aria-expanded', String(open));
                button.textContent = open ? 'Collapse' : 'Expand';
                button.setAttribute('aria-label', `${open ? 'Collapse' : 'Expand'} ${title}`);
            };
            button.addEventListener('click', () => setState(body.hidden));
            header.append(button);
            setState(OPEN_BY_DEFAULT.has(title));
            panel.dataset.collapsibleReady = 'true';
        });
    }

    function start() {
        decorateMeterPanels();
        const content = document.getElementById('em500Content');
        if (content) {
            new MutationObserver(decorateMeterPanels).observe(content, {
                childList: true,
                subtree: false
            });
        }
    }

    document.addEventListener('DOMContentLoaded', start);
    window.addEventListener('hashchange', () => window.setTimeout(decorateMeterPanels, 0));
})();