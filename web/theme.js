(() => {
    'use strict';

    const STORAGE_KEY = 'automatrix-pvdg-theme';
    const VALID_THEMES = new Set(['light', 'dark']);

    function preferredTheme() {
        try {
            const saved = localStorage.getItem(STORAGE_KEY);
            if (VALID_THEMES.has(saved)) return saved;
        } catch (_) {
            // Storage can be unavailable in privacy-restricted browsers.
        }
        return window.matchMedia?.('(prefers-color-scheme: light)').matches ? 'light' : 'dark';
    }

    function themeColor(theme) {
        return theme === 'light' ? '#f4f7fb' : '#081525';
    }

    function updateButton(button, theme) {
        const next = theme === 'dark' ? 'light' : 'dark';
        button.textContent = theme === 'dark' ? '☀' : '☾';
        button.setAttribute('aria-label', `Switch to ${next} theme`);
        button.setAttribute('title', `Switch to ${next} theme`);
        button.setAttribute('aria-pressed', theme === 'dark' ? 'true' : 'false');
        button.dataset.theme = theme;
    }

    function applyTheme(theme, persist = false) {
        const selected = VALID_THEMES.has(theme) ? theme : preferredTheme();
        document.documentElement.dataset.theme = selected;
        document.documentElement.style.colorScheme = selected;
        const meta = document.querySelector('meta[name="theme-color"]');
        if (meta) meta.setAttribute('content', themeColor(selected));
        const button = document.getElementById('themeToggleButton');
        if (button) updateButton(button, selected);
        if (persist) {
            try {
                localStorage.setItem(STORAGE_KEY, selected);
            } catch (_) {
                // Theme still applies for the current page.
            }
        }
        window.dispatchEvent(new CustomEvent('automatrix-theme-changed', {
            detail: { theme: selected }
        }));
        return selected;
    }

    function installButton() {
        const actions = document.querySelector('.topbar-actions');
        if (!actions || document.getElementById('themeToggleButton')) return;
        const button = document.createElement('button');
        button.id = 'themeToggleButton';
        button.type = 'button';
        button.className = 'icon-button theme-toggle-button';
        button.addEventListener('click', () => {
            const current = document.documentElement.dataset.theme || preferredTheme();
            applyTheme(current === 'dark' ? 'light' : 'dark', true);
        });
        const refresh = document.getElementById('refreshButton');
        if (refresh) actions.insertBefore(button, refresh);
        else actions.appendChild(button);
        updateButton(button, document.documentElement.dataset.theme || preferredTheme());
    }

    applyTheme(preferredTheme());
    document.addEventListener('DOMContentLoaded', () => {
        installButton();
        applyTheme(document.documentElement.dataset.theme || preferredTheme());
    });

    window.AutomatrixTheme = Object.freeze({ applyTheme, preferredTheme });
})();
