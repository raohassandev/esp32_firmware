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

    /* One row per inverter, one column per question. Declared once so the header
     * and the cells cannot drift apart. */
    const COLUMNS = [
        { label: 'Inverter', get: (item) => `Inverter ${Number(item.index) + 1}` },
        { label: 'State', get: (item) => stateLabel(item) },
        { label: 'Measured power', get: (item) => formatPower(item.measured_power_kw) },
        { label: 'Sample age', get: (item) => (item.telemetry_valid ? formatAge(item.telemetry_age_ms) : 'No valid sample') },
        { label: 'Identity', get: (item) => (item.identity_supported ? (item.identity_verified ? 'Verified' : 'Mismatch / unavailable') : 'Not supported') },
        { label: 'Readback', get: (item) => (item.has_readback ? `${formatPercent(item.readback_percent)} · ${formatAge(item.readback_age_ms)}` : 'Unavailable') },
        { label: 'Reads', get: (item) => `${item.read_successes ?? 0} ok · ${item.read_errors ?? 0} err · ${item.consecutive_read_failures ?? 0} consec` },
        { label: 'Last error', get: (item) => item.last_error_name || item.last_error || 'None' }
    ];

    function stateLabel(item) {
        if (item.online) return 'Online';
        return item.telemetry_stale ? 'Stale' : 'Offline';
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
        /* A table, not a column of cards. Every inverter answers the same eight
         * questions, so the answers belong in aligned columns where they can be
         * compared down the page. As stacked cards each channel occupied most of
         * a screen and two channels could not be seen at once. */
        const wrap = node('div', 'table-scroll');
        const table = node('table', 'device-table');
        table.id = 'inverterTelemetryLiveTable';
        const head = node('thead');
        const headRow = node('tr');
        COLUMNS.forEach((column) => {
            const cell = node('th', '', column.label);
            cell.scope = 'col';
            headRow.append(cell);
        });
        head.append(headRow);
        const body = node('tbody');
        body.id = 'inverterTelemetryLiveRows';
        table.append(head, body);
        wrap.append(table);
        panel.append(header, message, summary, wrap);
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

    function render(data) {
        ensureScaffold();
        const summary = byId('inverterTelemetryLiveSummary');
        const rows = byId('inverterTelemetryLiveRows');
        const message = byId('inverterTelemetryLiveMessage');
        if (!summary || !rows || !message) return;
        const values = data?.summary || {};
        summary.replaceChildren(
            summaryCard('Online', values.online ?? 0, `${data?.count ?? 0} configured runtime channels`),
            summaryCard('Measured production', formatPower(values.measured_total_kw), `${values.telemetry_valid ?? 0} valid telemetry channels`),
            summaryCard('Identity verified', values.identity_verified ?? 0, `${values.stale ?? 0} stale channels`),
            summaryCard('Commandable capacity', formatPower(values.commandable_rated_kw), `${values.command_mismatched ?? 0} readback mismatches`)
        );
        rows.replaceChildren();
        const inverters = Array.isArray(data?.inverters) ? data.inverters : [];
        if (!inverters.length) {
            const empty = node('tr');
            const cell = node('td', 'device-empty', 'No inverter runtime channels are available.');
            cell.colSpan = COLUMNS.length;
            empty.append(cell);
            rows.append(empty);
        }
        inverters.forEach((item) => {
            const row = node('tr');
            row.className = item.online ? 'row-online' : item.telemetry_stale ? 'row-stale' : 'row-offline';
            COLUMNS.forEach((column) => row.append(node('td', '', column.get(item))));
            rows.append(row);
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
