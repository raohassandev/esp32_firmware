/* ui-enhancements.js - collapsible sections in the EM500 meter workspace.
 *
 * OWNS: the expand/collapse affordance on `.em500-panel`, and nothing else.
 *   Touches only #em500Content.
 * DOES NOT OWN: what those panels contain, when they are rebuilt, or which is
 *   open by default. em500-core.js renders them and declares the default as
 *   data-collapsed-by-default. This module used to decide by matching the
 *   heading text against a hardcoded list of English section names, so a
 *   wording change silently collapsed what an engineer opens the view to read.
 *   It reads no user-visible text now, and issues no request.
 */
(() => {
    'use strict';

    let observer = null;

    function decoratePanel(panel) {
        if (panel.dataset.collapsibleReady === 'true') return;
        const header = panel.querySelector(':scope > .panel-header');
        if (!header) return;
        /* Declared by the renderer that owns the panel; absent means collapsed. */
        const open = panel.dataset.collapsedByDefault === 'false';
        const name = panel.dataset.panelKey || 'section';

        const body = document.createElement('div');
        body.className = 'em500-collapsible-body';
        while (header.nextSibling) body.append(header.nextSibling);
        panel.append(body);

        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'section-toggle';
        const setState = (isOpen) => {
            body.hidden = !isOpen;
            button.setAttribute('aria-expanded', String(isOpen));
            button.textContent = isOpen ? 'Collapse' : 'Expand';
            button.setAttribute('aria-label', `${isOpen ? 'Collapse' : 'Expand'} ${name}`);
        };
        button.addEventListener('click', () => setState(body.hidden));
        header.append(button);
        setState(open);
        panel.dataset.collapsibleReady = 'true';
    }

    /* Idempotent: an already-decorated panel is skipped, so re-running over
     * unchanged content mutates nothing. Every write lands inside a panel, not
     * as a direct child of #em500Content, so the observer cannot see its own
     * work; draining the queue anyway makes that structural. */
    function decorateMeterPanels() {
        const content = document.getElementById('em500Content');
        if (!content) return;
        ensureObserver(content);
        content.querySelectorAll('.em500-panel').forEach(decoratePanel);
        observer?.takeRecords();
    }

    /* #em500Content is built by em500-core.js when the meters route is opened,
     * so it does not exist at DOMContentLoaded. Attach on first sight. */
    let observedNode = null;
    function ensureObserver(content) {
        if (observedNode === content) return;
        observer?.disconnect();
        observer = new MutationObserver(decorateMeterPanels);
        observer.observe(content, { childList: true, subtree: false });
        observedNode = content;
    }

    document.addEventListener('DOMContentLoaded', decorateMeterPanels);
    window.addEventListener('hashchange', () => window.setTimeout(decorateMeterPanels, 0));
    /* #em500Content appears well after load. Rather than poll for it, listen to
     * the one #mainContent observer the application already has. */
    window.AutomatrixEngineeringAccess?.onContentChange(decorateMeterPanels, { deep: true });
})();
