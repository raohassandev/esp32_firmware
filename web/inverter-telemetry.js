(() => {
    'use strict';

    const state = { loading: false, timer: null };
    const byId = (id) => document.getElementById(id);

    function node(tag, className = '', text = null) {
        const item = document.createElement(tag);
        if (className) item.className = className;
        if (text != null) item.textContent = String(text);
        return item;
    }

    function formatPower(value) {
        const number = Number(value);
        return Number.isFinite(number) ? `${number.toFixed(2)} kW` : '--';
    }

    function formatPercent(value) {
        const number = Number(value);
        return Number.isFinite(number) ? `${number.toFixed(1)}%` : '--';
    }

    function formatAge(value) {
        const ms = Number(value);
        if (!Number.isFinite(ms)) return '--';
        if (ms < 1000) return `${Math.max(0, Math.round(ms))} ms`;
        return `${(ms / 1000).toFixed(1)} s`;
    }

    function ensureScaffold() {
        const page = document.querySelector('[data-page="inverters"]');
        if (!page || byId('inverterLiveTelemetry')) return;
        const panel = node('section', 'panel');
        panel.id = 'inverterLiveTelemetry';
        const header = node('div', 'panel-header');
        const copy = node('div');
        copy.append(node('p', 'eyebrow', 'Live read-only qualification'),
                    node('h3', '', 'Inverter telemetry and readback'));
        const refresh = node('button', 'button secondary', 'Refresh telemetry');
        refresh.type = 'button';
        refresh.addEventListener('click', load);
        header.append(copy, refresh);
        const message = node('div', 'device-readiness-note', 'Waiting for inverter telemetry…');
        message.id = 'inverterTelemetryLiveMessage';
        const summary = node('div', 'device-summary');
        summary.id = 'inverterTelemetryLiveSummary';
        const list = node('div', 'device-list');
        list.id = 'inverterTelemetryLiveList';
        panel.append(header, message, summary, list);
        const config = byId('inverterConfigurationEditor');
        if (config) config.after(panel);
        else page.append(panel);
    }

    function summaryCard(label, value, detail = '') {
        const card = node('div', 'device-summary-card');
        card.append(node('span', '', label), node('strong', '', value));
        if (detail) card.append(node('small', '', detail));
        return card;
    }

    function meta(label, value) {
        const item = node('div', 'device-meta-item');
        item.append(node('span', '', label), node('strong', '', value));
        return item;
    }

    function render(data) {
        ensureScaffold();
        const summary = byId('inverterTelemetryLiveSummary');
        const list = byId('inverterTelemetryLiveList');
        const message = byId('inverterTelemetryLiveMessage');
        if (!summary || !list || !message) return;
        const values = data?.summary || {};
        summary.replaceChildren(
            summaryCard('Online', values.online ?? 0, `${data?.count ?? 0} configured runtime channels`),
            summaryCard('Measured production', formatPower(values.measured_total_kw), `${values.telemetry_valid ?? 0} valid telemetry channels`),
            summaryCard('Identity verified', values.identity_verified ?? 0, `${values.stale ?? 0} stale channels`),
            summaryCard('Commandable capacity', formatPower(values.commandable_rated_kw), `${values.command_mismatched ?? 0} readback mismatches`)
        );
        list.replaceChildren();
        const inverters = Array.isArray(data?.inverters) ? data.inverters : [];
        if (!inverters.length) {
            list.append(node('div', 'device-empty', 'No inverter runtime channels are available.'));
        }
        inverters.forEach((item) => {
            const card = node('article', 'device-runtime-card');
            const top = node('div', 'device-card-top');
            const heading = node('div');
            heading.append(node('div', 'device-card-index', `Inverter ${Number(item.index) + 1}`),
                           node('h3', '', item.online ? 'Online telemetry' : 'Telemetry unavailable'),
                           node('p', 'device-state-detail', item.telemetry_stale ? 'Stale data removed from capacity' : 'Read-only monitoring; no writes issued'));
            const badge = node('span', `subtle-badge ${item.online ? 'good' : item.telemetry_stale ? 'warning' : 'bad'}`,
                               item.online ? 'Online' : item.telemetry_stale ? 'Stale' : 'Offline');
            top.append(heading, badge);

            const reading = node('div', 'device-reading');
            const value = node('div');
            value.append(node('div', 'device-reading-label', 'Measured active power'),
                         node('strong', 'device-reading-value', formatPower(item.measured_power_kw)));
            reading.append(value, node('div', 'device-reading-note', item.telemetry_valid
                ? `Fresh sample · age ${formatAge(item.telemetry_age_ms)}`
                : 'No valid current sample'));

            const grid = node('div', 'device-meta-grid');
            grid.append(
                meta('Identity', item.identity_supported ? (item.identity_verified ? 'Verified' : 'Mismatch / unavailable') : 'Not supported'),
                meta('Readback', item.has_readback ? formatPercent(item.readback_percent) : 'Unavailable'),
                meta('Readback age', item.has_readback ? formatAge(item.readback_age_ms) : '--'),
                meta('Command mismatch', item.command_mismatch ? `YES · ${item.mismatch_count ?? 0} events` : 'No'),
                meta('Successful reads', item.read_successes ?? 0),
                meta('Read errors', `${item.read_errors ?? 0} total · ${item.consecutive_read_failures ?? 0} consecutive`),
                meta('Last error', item.last_error_name || item.last_error || 'None'),
                meta('Write status', 'Zero writes from telemetry endpoint')
            );
            card.append(top, reading, grid);
            list.append(card);
        });
        message.textContent = `Updated ${new Date().toLocaleTimeString()}. Endpoint confirms writes_issued=false.`;
    }

    async function load() {
        if (state.loading || location.hash !== '#/inverters') return;
        state.loading = true;
        ensureScaffold();
        const message = byId('inverterTelemetryLiveMessage');
        if (message) message.textContent = 'Loading read-only inverter telemetry…';
        try {
            const response = await fetch('/api/inverter-telemetry', { cache: 'no-store' });
            if (!response.ok) throw new Error(await response.text() || `HTTP ${response.status}`);
            const payload = await response.json();
            if (payload.writes_issued !== false || payload.read_only_endpoint !== true) {
                throw new Error('Telemetry endpoint safety declaration is missing');
            }
            render(payload);
        } catch (error) {
            if (message) message.textContent = `Inverter telemetry unavailable: ${error.message}`;
        } finally {
            state.loading = false;
        }
    }

    function schedule() {
        clearInterval(state.timer);
        state.timer = setInterval(load, 2000);
    }

    document.addEventListener('DOMContentLoaded', () => {
        ensureScaffold();
        if (location.hash === '#/inverters') load();
        schedule();
    });
    window.addEventListener('hashchange', () => {
        if (location.hash === '#/inverters') load();
    });
})();
