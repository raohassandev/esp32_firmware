/* ui-enhancements.js - small interaction hardening that does not own data.
 *
 * OWNS: the expand/collapse affordance on `.em500-panel`, removal of stale
 * operator-only presentation at an access transition, and narrow-shell control
 * consolidation. DOES NOT OWN: panel contents, data refreshes, routing,
 * authentication or any controller request.
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

/* The plant verdict is deliberately an operator-only summary. The verdict
 * renderer exits in Engineering mode, but an already-rendered rail from the
 * preceding operator session used to remain in the DOM with its last value.
 * Remove it at the access transition; the underlying controller-owned source,
 * meter, control and alarm panels remain available and continue to refresh. */
(() => {
    'use strict';

    function removeStaleOperatorVerdict() {
        if (document.documentElement.dataset.access !== 'engineering') return;
        document.getElementById('plantVerdictRail')?.remove();
    }

    window.addEventListener('amx-access-change', removeStaleOperatorVerdict);
    document.addEventListener('DOMContentLoaded', removeStaleOperatorVerdict, { once: true });
})();

/* The shell already has one controller menu containing Refresh and Theme.
 * Keeping separate 44px topbar buttons as well made the 1024px action cluster
 * crop its labels and left almost no title at 390px. On tablet and phone those
 * duplicate actions stay available through the menu, while the navigation
 * button, page title, health state and menu remain persistent. */
(() => {
    'use strict';

    const style = document.createElement('style');
    style.id = 'narrowShellControlStyles';
    style.textContent = `
.shell-overflow-button{width:auto;min-width:58px;padding:0 12px;font-size:12px;font-weight:800}
@media(max-width:1180px){
  body.product-shell-v2 #refreshButton,
  body.product-shell-v2 #themeToggleButton{display:none!important}
  body.product-shell-v2 .topbar-actions{gap:6px}
}
@media(max-width:650px){
  body.product-shell-v2 .topbar{gap:6px}
  body.product-shell-v2 .page-heading h1{max-width:46vw}
  .shell-overflow-button{min-width:50px;padding-inline:8px;font-size:11px}
}
@media(max-width:420px){
  body.product-shell-v2 .page-heading h1{max-width:42vw}
  body.product-shell-v2 .topbar-actions{gap:4px}
}
`;
    document.head.append(style);

    /* product-shell-v2 predates theme.js's current button id. Bridge the menu's
     * Theme item to the installed control without changing either owner. */
    document.addEventListener('click', (event) => {
        const item = event.target.closest?.('.shell-menu button:nth-child(4)');
        if (!item) return;
        document.getElementById('themeToggleButton')?.click();
    });
})();
