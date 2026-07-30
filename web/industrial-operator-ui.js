(() => {
    'use strict';

    /*
     * Industrial operator presentation layer.
     *
     * This module is deliberately UI-only: it does not call controller APIs, does
     * not change routes or authorisation, and does not enable any control action.
     * It reduces operator-facing developer detail and puts advanced service tools
     * behind an explicit, session-local disclosure inside an authenticated
     * Engineering session.
     */
    const SERVICE_VIEW_KEY = 'amx-industrial-service-view-v1';

    const byId = (id) => document.getElementById(id);
    const currentAccess = () => document.documentElement.dataset.access === 'engineering'
        ? 'engineering'
        : 'operator';

    function serviceViewEnabled() {
        try {
            return sessionStorage.getItem(SERVICE_VIEW_KEY) === '1';
        } catch (_) {
            return false;
        }
    }

    function setServiceView(enabled) {
        try {
            sessionStorage.setItem(SERVICE_VIEW_KEY, enabled ? '1' : '0');
        } catch (_) {
            /* Session storage is an optional presentation preference. */
        }
        applyAudienceVisibility();
    }

    function node(tag, className = '', text = '') {
        const element = document.createElement(tag);
        if (className) element.className = className;
        if (text) element.textContent = text;
        return element;
    }

    function ensureAudienceBadge() {
        const actions = document.querySelector('.topbar-actions');
        if (!actions) return null;
        let badge = byId('industrialAudienceBadge');
        if (!badge) {
            badge = node('span', 'industrial-audience-badge');
            badge.id = 'industrialAudienceBadge';
            const refresh = byId('refreshButton');
            actions.insertBefore(badge, refresh || null);
        }
        return badge;
    }

    function syncAudienceBadge() {
        const badge = ensureAudienceBadge();
        if (!badge) return;
        const engineering = currentAccess() === 'engineering';
        badge.textContent = engineering ? 'ENGINEERING VIEW' : 'OPERATOR VIEW';
        badge.classList.toggle('engineering', engineering);
        badge.title = engineering
            ? 'Protected engineering settings are available in this browser session.'
            : 'Read-only plant operation and alarm information.';
    }

    function ensureOperatorSummaryCards() {
        const grid = document.querySelector('[data-page="dashboard"] .metric-grid');
        if (!grid || byId('industrialControlSummary')) return;

        const control = node('article', 'metric-card industrial-operator-card');
        control.id = 'industrialControlSummary';
        control.innerHTML = '<div class="metric-head"><span>Control state</span><span class="dot neutral"></span></div>'
            + '<div class="metric-value compact" data-industrial-value>Checking</div>'
            + '<div class="metric-foot" data-industrial-detail>Reading controller authority</div>';

        const alarms = node('article', 'metric-card industrial-operator-card');
        alarms.id = 'industrialAlarmSummary';
        alarms.innerHTML = '<div class="metric-head"><span>Alarm status</span><span class="dot neutral" data-industrial-dot></span></div>'
            + '<div class="metric-value compact" data-industrial-value>Checking</div>'
            + '<div class="metric-foot" data-industrial-detail>Review active plant conditions</div>';

        grid.append(control, alarms);
        syncOperatorSummaryCards();
    }

    function syncOperatorSummaryCards() {
        const controlCard = byId('industrialControlSummary');
        const alarmCard = byId('industrialAlarmSummary');
        const mode = byId('dashboardMode')?.textContent?.trim();
        const control = byId('statusControl')?.textContent?.trim();
        const alarms = byId('statusAlarms')?.textContent?.trim();

        if (controlCard) {
            const value = controlCard.querySelector('[data-industrial-value]');
            const detail = controlCard.querySelector('[data-industrial-detail]');
            if (value) value.textContent = mode && mode !== '--' ? mode : (control || 'Unavailable');
            if (detail) detail.textContent = control && control !== '--'
                ? `Controller reports: ${control}`
                : 'Control authority is unavailable';
        }

        if (alarmCard) {
            const value = alarmCard.querySelector('[data-industrial-value]');
            const detail = alarmCard.querySelector('[data-industrial-detail]');
            const dot = alarmCard.querySelector('[data-industrial-dot]');
            const text = alarms && alarms !== '--' ? alarms : 'Unavailable';
            if (value) value.textContent = text;
            const normal = /^(0|none|normal|clear)$/i.test(text);
            if (detail) detail.textContent = normal
                ? 'No active plant condition requires attention'
                : 'Open Alarms and events for the required action';
            if (dot) dot.className = `dot ${normal ? 'good' : 'warning'}`;
        }
    }

    function markInternalDashboardCards() {
        const cards = document.querySelectorAll('[data-page="dashboard"] .metric-grid .metric-card');
        cards.forEach((card) => {
            const heading = card.querySelector('.metric-head span')?.textContent?.trim();
            if (heading === 'Requested PV' || heading === 'Applied PV') {
                card.classList.add('industrial-internal-control-card');
            }
        });
    }

    function labelText(label) {
        return label.querySelector(':scope > span')?.textContent?.trim() || '';
    }

    function renameField(label, replacement) {
        const title = label.querySelector(':scope > span');
        if (title) title.textContent = replacement;
    }

    function organiseMeterConfiguration() {
        const page = document.querySelector('[data-page="meters"]');
        if (!page) return;
        const panel = page.querySelector('.dashboard-grid .form-panel');
        if (!panel) return;
        panel.classList.add('industrial-engineering-panel');

        const fieldGrid = panel.querySelector('.field-grid');
        if (!fieldGrid || panel.querySelector('.industrial-advanced-details')) return;

        const advancedNames = new Set([
            'Port',
            'Modbus PDU address',
            'Scale to kW',
            'Poll interval (ms)',
            'Timeout (ms)'
        ]);
        const details = node('details', 'industrial-advanced-details');
        const summary = node('summary', '', 'Advanced communication settings');
        const intro = node('p', 'industrial-advanced-intro',
            'Service values are normally supplied by the selected meter profile. Change them only against verified device documentation.');
        const advancedGrid = node('div', 'field-grid industrial-advanced-grid');
        details.append(summary, intro, advancedGrid);

        [...fieldGrid.querySelectorAll(':scope > label.field')].forEach((label) => {
            const name = labelText(label);
            if (name === 'Host') renameField(label, 'Meter IP address');
            if (name === 'Unit ID') renameField(label, 'Modbus device address');
            if (advancedNames.has(name)) advancedGrid.append(label);
        });

        const actions = panel.querySelector('.panel-actions');
        panel.insertBefore(details, actions || null);
    }

    function addEvidenceDisclosure(container) {
        if (!container || container.dataset.industrialDisclosure === 'ready') return;
        container.dataset.industrialDisclosure = 'ready';
        container.classList.add('industrial-evidence-block', 'is-collapsed');

        const title = container.querySelector(':scope > strong');
        const button = node('button', 'industrial-evidence-toggle', 'View technical evidence');
        button.type = 'button';
        button.setAttribute('aria-expanded', 'false');
        button.addEventListener('click', () => {
            const collapsed = container.classList.toggle('is-collapsed');
            button.textContent = collapsed ? 'View technical evidence' : 'Hide technical evidence';
            button.setAttribute('aria-expanded', collapsed ? 'false' : 'true');
        });
        if (title) title.insertAdjacentElement('afterend', button);
        else container.prepend(button);
    }

    function organiseInverterPage() {
        const confirmation = byId('writeConfirmationPanel');
        if (confirmation) confirmation.classList.add('industrial-engineering-panel');
        const labTarget = byId('labTargetPanel');
        if (labTarget) labTarget.classList.add('industrial-service-panel');

        addEvidenceDisclosure(byId('proofNotice'));
        addEvidenceDisclosure(byId('prereqNotice'));
        addEvidenceDisclosure(byId('gatePrereqNotice'));
    }

    function organiseSystemPage() {
        const advancedJson = document.querySelector('[data-page="system"] .dashboard-grid > article:nth-child(2)');
        if (advancedJson) advancedJson.classList.add('industrial-service-panel');
        const rawMeter = byId('em500Workspace');
        if (rawMeter) rawMeter.classList.add('industrial-service-panel');
    }

    function ensureServiceToggle() {
        const actions = document.querySelector('.engineering-actions');
        if (!actions || byId('industrialServiceToggle')) return;

        const wrap = node('div', 'industrial-service-toggle');
        const button = node('button', 'button secondary', 'Show advanced service tools');
        button.id = 'industrialServiceToggle';
        button.type = 'button';
        button.addEventListener('click', () => setServiceView(!serviceViewEnabled()));
        const note = node('small', '', 'Same Engineering session; no additional authority is granted.');
        wrap.append(button, note);
        actions.append(wrap);
    }

    function syncServiceToggle() {
        const button = byId('industrialServiceToggle');
        if (!button) return;
        const visible = serviceViewEnabled();
        button.textContent = visible ? 'Hide advanced service tools' : 'Show advanced service tools';
        button.setAttribute('aria-pressed', visible ? 'true' : 'false');
    }

    function applyAudienceVisibility() {
        const access = currentAccess();
        const engineering = access === 'engineering';
        const service = engineering && serviceViewEnabled();
        document.body?.setAttribute('data-audience', access);

        document.querySelectorAll('.industrial-engineering-panel').forEach((panel) => {
            panel.hidden = !engineering;
        });
        document.querySelectorAll('.industrial-service-panel').forEach((panel) => {
            panel.hidden = !service;
        });

        syncAudienceBadge();
        syncServiceToggle();
        syncOperatorSummaryCards();
    }

    function initialise() {
        ensureAudienceBadge();
        ensureOperatorSummaryCards();
        markInternalDashboardCards();
        organiseMeterConfiguration();
        organiseInverterPage();
        organiseSystemPage();
        ensureServiceToggle();
        applyAudienceVisibility();

        const watched = [
            byId('dashboardMode'), byId('statusControl'), byId('statusAlarms')
        ].filter(Boolean);
        if (watched.length) {
            const observer = new MutationObserver(syncOperatorSummaryCards);
            watched.forEach((target) => observer.observe(target, { childList: true, subtree: true, characterData: true }));
        }

        window.addEventListener('amx-access-change', () => {
            window.setTimeout(() => {
                ensureServiceToggle();
                organiseMeterConfiguration();
                organiseInverterPage();
                organiseSystemPage();
                applyAudienceVisibility();
            }, 0);
        });
        window.addEventListener('hashchange', () => window.setTimeout(applyAudienceVisibility, 0));

        const main = byId('mainContent');
        if (main) {
            new MutationObserver(() => {
                ensureOperatorSummaryCards();
                markInternalDashboardCards();
                organiseMeterConfiguration();
                organiseInverterPage();
                organiseSystemPage();
                ensureServiceToggle();
                applyAudienceVisibility();
            }).observe(main, { childList: true, subtree: true });
        }
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', initialise, { once: true });
    } else {
        initialise();
    }
})();
