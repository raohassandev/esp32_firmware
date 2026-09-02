/* shell-current-fixes.js — current-baseline shell interaction repairs.
 *
 * product-shell-v2.js still points its Theme menu action at the retired
 * `themeToggle` id, while theme.js installs `themeToggleButton`.  Keep this
 * repair outside the older shell module so current route ownership is not
 * rewritten just to fix one stale control id.
 *
 * This file owns no API, routing table, authentication or polling.
 */
(() => {
    'use strict';

    function relabelOverflow() {
        const button = document.getElementById('shellOverflowButton');
        if (!button) return;
        if (button.textContent !== 'More') button.textContent = 'More';
        button.setAttribute('aria-label', 'Open controller actions');
    }

    document.addEventListener('click', (event) => {
        const action = event.target.closest?.('.shell-menu button');
        if (!action) return;
        const label = action.querySelector('span')?.textContent?.trim();
        if (label !== 'Theme') return;
        /* product-shell-v2's stale handler runs first and safely no-ops because
         * #themeToggle no longer exists. Trigger the actual current control. */
        document.getElementById('themeToggleButton')?.click();
    });

    function start() {
        relabelOverflow();
        window.AutomatrixEngineeringAccess?.onContentChange(relabelOverflow);
        window.addEventListener('hashchange', relabelOverflow);
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();
