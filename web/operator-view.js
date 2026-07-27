(() => {
    'use strict';

    const byId = (id) => document.getElementById(id);
    const state = { meterBusy: false, inverterBusy: false, timer: null };

    function isOperator() {
        return document.documentElement.dataset.access !== 'engineering';
    }

    function route() {
        return location.hash.replace(/^#\/?/, '') || 'dashboard';
    }

    function node(tag, className = '', text = null) {
        const item = document.createElement(tag);
        if (className) item.className = className;
        if (text != null) item.textContent = String(text);
        return item;
    }

    function formatPower(value) {
        const number = Number(value);
        return Number.isFinite(number) ? `${number.toFixed(2)} kW` : 'Unavailable';
    }

    function formatAge(value) {
        const number = Number(value);
        if (!Number.isFinite(number) || number < 0) return 'Unavailable';
        if (number < 1000) return `${Math.round(number)} ms`;
        return `${(number / 1000).toFixed(1)} s`;
    }

    async function api(path) {
        const response = await fetch(path, { cache: 'no-store', credentials: 'same-origin' });
        const payload = await response.json().catch(() => ({}));
        if (!response.ok) throw new Error(payload.error || `HTTP ${response.status}`);
        return payload;
    }

    function metric(label, value, detail = '', tone = '') {
        const card = node('article', `operator-metric${tone ? ` ${tone}` : ''}`);
        card.append(node('span', 'operator-metric-label', label),
                    node('strong', 'operator-metric-value', value),
                    node('small', 'operator-metric-detail', detail));
        return card;
    }

    function statusBadge(label, tone) {
        return node('span', `subtle-badge ${tone || ''}`, label);
    }

    function ensureMeterView() {
        const page = document.querySelector('[data-page="meters"]');
        if (!page || byId('operatorMeterView')) return;
        const intro = page.querySelector('.page-intro');
        const section = node('section', 'operator-product-view');
        section.id = 'operatorMeterView';
        section.innerHTML = `
            <div class="operator-section-head">
                <div><p class="eyebrow">Grid monitoring</p><h3>Electrical supply status</h3><p>Live grid power and meter communication health.</p></div>
                <button class="button secondary" id="operatorMeterRefresh" type="button">Refresh status</button>
            </div>
            <div class="operator-metric-grid" id="operatorMeterMetrics"></div>
            <div class="operator-equipment-list" id="operatorMeterList"></div>
            <div class="operator-message" id="operatorMeterMessage" role="status">Loading grid status…</div>`;
        intro?.after(section);
        byId('operatorMeterRefresh')?.addEventListener('click', refreshMeter);
    }

    function ensureInverterView() {
        const page = document.querySelector('[data-page="inverters"]');
        if (!page || byId('operatorInverterView')) return;
        const notice = page.querySelector('.notice');
        const intro = page.querySelector('.page-intro');
        const anchor = notice || intro;
        const section = node('section', 'operator-product-view');
        section.id = 'operatorInverterView';
        section.innerHTML = `
            <div class="operator-section-head">
                <div><p class="eyebrow">Solar generation</p><h3>Inverter fleet status</h3><p>Installed capacity, availability and measured solar production.</p></div>
                <button class="button secondary" id="operatorInverterRefresh" type="button">Refresh status</button>
            </div>
            <div class="operator-metric-grid" id="operatorInverterMetrics"></div>
            <div class="operator-equipment-list" id="operatorInverterList"></div>
            <div class="operator-message" id="operatorInverterMessage" role="status">Loading inverter status…</div>`;
        anchor?.after(section);
        byId('operatorInverterRefresh')?.addEventListener('click', refreshInverters);
    }

    function renderMeter(data) {
        const metrics = byId('operatorMeterMetrics');
        const list = byId('operatorMeterList');
        const message = byId('operatorMeterMessage');
        if (!metrics || !list || !message) return;
        const meters = Array.isArray(data?.meters) ? data.meters : [];
        const summary = data?.summary || {};
        const primary = meters.find((item) => item.enabled) || meters[0];
        const runtime = primary?.runtime || {};
        const healthy = runtime.online === true;
        metrics.replaceChildren(
            metric('Grid power', formatPower(runtime.active_power_kw), healthy ? `Updated ${formatAge(runtime.data_age_ms)} ago` : 'Current value unavailable', healthy ? 'good' : 'bad'),
            metric('Meter communication', healthy ? 'Online' : 'Unavailable', healthy ? 'Live measurements are current' : 'Check field communication', healthy ? 'good' : 'bad'),
            metric('Active meters', String(summary.online ?? 0), `${summary.enabled ?? 0} enabled`, Number(summary.online) > 0 ? 'good' : 'warning'),
            metric('System attention', Number(summary.stale_or_unavailable) > 0 ? 'Required' : 'None', Number(summary.stale_or_unavailable) > 0 ? 'One or more meters need attention' : 'All enabled meters are healthy', Number(summary.stale_or_unavailable) > 0 ? 'warning' : 'good')
        );
        list.replaceChildren();
        meters.forEach((meter, index) => {
            const meterRuntime = meter.runtime || {};
            const online = meterRuntime.online === true;
            const card = node('article', 'operator-equipment-card');
            const title = node('div', 'operator-equipment-title');
            title.append(node('span', '', `Power meter ${index + 1}`), node('strong', '', meter.name || `Meter ${index + 1}`));
            const values = node('div', 'operator-equipment-values');
            values.append(
                node('div', '', `Power ${formatPower(meterRuntime.active_power_kw)}`),
                node('div', '', online ? `Fresh ${formatAge(meterRuntime.data_age_ms)} ago` : 'No current measurement')
            );
            card.append(title, values, statusBadge(online ? 'Online' : meter.enabled ? 'Attention' : 'Disabled', online ? 'good' : meter.enabled ? 'warning' : ''));
            list.append(card);
        });
        if (!meters.length) list.append(node('div', 'device-empty', 'No grid meter is configured.'));
        message.textContent = `Status updated ${new Date().toLocaleTimeString()}.`;
    }

    function renderInverters(config, telemetry) {
        const metrics = byId('operatorInverterMetrics');
        const list = byId('operatorInverterList');
        const message = byId('operatorInverterMessage');
        if (!metrics || !list || !message) return;
        const inverters = Array.isArray(config?.inverters) ? config.inverters : [];
        const summary = config?.summary || {};
        const telemetryItems = new Map((telemetry?.inverters || []).map((item) => [Number(item.index), item]));
        const validTelemetry = Number(telemetry?.summary?.telemetry_valid) > 0;
        const liveProduction = validTelemetry ? Number(telemetry?.summary?.measured_total_kw) : NaN;
        const onlineCount = Number(summary.online ?? telemetry?.summary?.online ?? 0);
        metrics.replaceChildren(
            metric('Installed capacity', formatPower(summary.configured_rated_kw), `${inverters.length} installed unit${inverters.length === 1 ? '' : 's'}`),
            metric('Available inverters', String(onlineCount), `${summary.enabled ?? 0} enabled`, onlineCount > 0 ? 'good' : 'warning'),
            metric('Solar production', validTelemetry && Number.isFinite(liveProduction) ? formatPower(liveProduction) : 'Not monitored', validTelemetry ? 'Measured inverter output' : 'Production monitoring requires inverter commissioning', validTelemetry ? 'good' : 'warning'),
            metric('Automatic control', Number(summary.commandable_rated_kw) > 0 ? 'Available' : 'Locked', Number(summary.commandable_rated_kw) > 0 ? 'Qualified control path ready' : 'Engineering qualification required', Number(summary.commandable_rated_kw) > 0 ? 'good' : 'warning')
        );
        list.replaceChildren();
        inverters.forEach((inverter, index) => {
            const live = telemetryItems.get(Number(inverter.index ?? index)) || {};
            const online = inverter.runtime?.online === true || live.online === true;
            const measured = live.telemetry_valid ? live.measured_power_kw : inverter.measured_power_kw;
            const card = node('article', 'operator-equipment-card');
            const title = node('div', 'operator-equipment-title');
            title.append(node('span', '', `Solar inverter ${index + 1}`), node('strong', '', inverter.name || `Inverter ${index + 1}`));
            const values = node('div', 'operator-equipment-values');
            values.append(
                node('div', '', `Rated ${formatPower(inverter.rated_kw)}`),
                node('div', '', Number.isFinite(Number(measured)) ? `Producing ${formatPower(measured)}` : 'Production not monitored')
            );
            card.append(title, values, statusBadge(online ? 'Online' : inverter.enabled ? 'Attention' : 'Disabled', online ? 'good' : inverter.enabled ? 'warning' : ''));
            list.append(card);
        });
        if (!inverters.length) list.append(node('div', 'device-empty', 'No solar inverter is configured.'));
        message.textContent = `Status updated ${new Date().toLocaleTimeString()}.`;
    }

    async function refreshMeter() {
        if (!isOperator() || route() !== 'meters' || state.meterBusy) return;
        state.meterBusy = true;
        const button = byId('operatorMeterRefresh');
        if (button) { button.disabled = true; button.textContent = 'Refreshing…'; }
        try {
            renderMeter(await api('/api/meters'));
        } catch (error) {
            const message = byId('operatorMeterMessage');
            if (message) message.textContent = `Grid status unavailable: ${error.message}`;
        } finally {
            state.meterBusy = false;
            if (button) { button.disabled = false; button.textContent = 'Refresh status'; }
        }
    }

    async function refreshInverters() {
        if (!isOperator() || route() !== 'inverters' || state.inverterBusy) return;
        state.inverterBusy = true;
        const button = byId('operatorInverterRefresh');
        if (button) { button.disabled = true; button.textContent = 'Refreshing…'; }
        try {
            const [config, telemetry] = await Promise.all([
                api('/api/inverters'),
                api('/api/inverter-telemetry')
            ]);
            renderInverters(config, telemetry);
        } catch (error) {
            const message = byId('operatorInverterMessage');
            if (message) message.textContent = `Inverter status unavailable: ${error.message}`;
        } finally {
            state.inverterBusy = false;
            if (button) { button.disabled = false; button.textContent = 'Refresh status'; }
        }
    }

    function replaceText(id, value) {
        const target = byId(id);
        if (target) target.textContent = value;
    }

    function updateProductLanguage() {
        const operator = isOperator();
        const meterLink = document.querySelector('[data-route="meters"] span:last-child');
        const inverterLink = document.querySelector('[data-route="inverters"] span:last-child');
        if (meterLink) meterLink.textContent = operator ? 'Grid Power' : 'Meters';
        if (inverterLink) inverterLink.textContent = operator ? 'Solar Inverters' : 'Inverters';

        const meterIntro = document.querySelector('[data-page="meters"] .page-intro');
        const inverterIntro = document.querySelector('[data-page="inverters"] .page-intro');
        meterIntro?.querySelector('h2')?.replaceChildren(operator ? 'Grid power' : 'Grid meters');
        meterIntro?.querySelector('p:last-child')?.replaceChildren(operator
            ? 'Monitor live electrical demand and grid-meter availability.'
            : 'Review meter communication, freshness and Modbus acquisition settings.');
        inverterIntro?.querySelector('h2')?.replaceChildren(operator ? 'Solar inverters' : 'Inverters');
        inverterIntro?.querySelector('p:last-child')?.replaceChildren(operator
            ? 'Monitor solar inverter availability, capacity and live production.'
            : 'Configured inverter endpoints and command-safety state.');

        if (operator) {
            const currentRoute = route();
            if (currentRoute === 'meters') {
                replaceText('pageTitle', 'Grid Power');
                replaceText('breadcrumbCurrent', 'Grid power');
            } else if (currentRoute === 'inverters') {
                replaceText('pageTitle', 'Solar Inverters');
                replaceText('breadcrumbCurrent', 'Solar inverters');
            }
            const flowLoad = byId('flowLoad');
            const flowLoadDetail = flowLoad?.nextElementSibling;
            if (flowLoad?.textContent === 'Unavailable') flowLoad.textContent = 'Not monitored';
            if (flowLoadDetail) flowLoadDetail.textContent = 'Optional plant-load measurement';
            const generator = document.querySelector('.flow-secondary > div:first-child');
            const generatorValue = generator?.querySelector('strong');
            const generatorDetail = generator?.querySelector('small');
            if (generatorValue?.textContent === 'Unavailable') generatorValue.textContent = 'Not connected';
            if (generatorDetail) generatorDetail.textContent = 'Optional generator measurement';
        }
    }

    function refreshRoute() {
        updateProductLanguage();
        if (!isOperator()) return;
        if (route() === 'meters') refreshMeter();
        if (route() === 'inverters') refreshInverters();
    }

    function start() {
        ensureMeterView();
        ensureInverterView();
        updateProductLanguage();
        refreshRoute();
        window.addEventListener('hashchange', refreshRoute);
        state.timer = window.setInterval(refreshRoute, 5000);
        new MutationObserver(() => updateProductLanguage()).observe(document.documentElement, {
            attributes: true, attributeFilter: ['data-access']
        });
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();
