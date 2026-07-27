(() => {
    'use strict';

    function activate() {
        const route = location.hash.replace(/^#\/?/, '') || 'dashboard';
        if (route !== 'commissioning') return;
        if (document.documentElement.dataset.access !== 'engineering') {
            location.hash = '#/dashboard';
            return;
        }
        document.querySelectorAll('.page').forEach((page) => page.classList.toggle('active', page.dataset.page === 'commissioning'));
        document.querySelectorAll('.nav-link').forEach((link) => link.classList.toggle('active', link.dataset.route === 'commissioning'));
        const title = document.getElementById('pageTitle');
        const breadcrumb = document.getElementById('breadcrumbCurrent');
        if (title) title.textContent = 'Commissioning';
        if (breadcrumb) breadcrumb.textContent = 'Guided site commissioning';
        document.title = 'Commissioning · Automatrix PV-DG';
    }

    window.addEventListener('hashchange', () => setTimeout(activate, 0));
    new MutationObserver(() => setTimeout(activate, 0)).observe(document.documentElement, { attributes: true, attributeFilter: ['data-access'] });
    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', activate, { once: true });
    else activate();
})();
