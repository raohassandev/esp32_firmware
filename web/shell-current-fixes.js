/* shell-current-fixes.js — current-baseline shell interaction repairs.
 *
 * product-shell-v2.js still points its Theme menu action at the retired
 * `themeToggle` id, while theme.js installs `themeToggleButton`. Keep this
 * repair outside the older shell module so route ownership is unchanged.
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
        document.getElementById('themeToggleButton')?.click();
    });

    function start() {
        relabelOverflow();
        window.addEventListener('hashchange', relabelOverflow);
        window.addEventListener('amx-access-change', relabelOverflow);
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();
