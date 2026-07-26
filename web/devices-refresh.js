(() => {
    'use strict';

    function bindGlobalRefresh() {
        const button = document.getElementById('refreshButton');
        if (!button) return;

        /* app.js already refreshes the common controller status. Re-dispatching
         * the route event lets the read-only diagnostics module refresh the
         * active Dashboard, Meters or Inverters endpoint at the same time. */
        button.addEventListener('click', () => {
            window.dispatchEvent(new Event('hashchange'));
        });
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', bindGlobalRefresh, { once: true });
    } else {
        bindGlobalRefresh();
    }
})();
