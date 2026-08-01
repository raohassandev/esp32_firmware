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
        /* NEVER 0.00 kW FOR A MACHINE THAT REPORTED NOTHING.
         *
         * The very next column already says "No valid sample", so the table
         * contradicted itself on one line -- and the runtime card further down
         * the same page says "Unavailable" for the same inverter. A zero here
         * reads as a measurement of a machine that is there and producing
         * nothing, which is a different physical claim from silence. */
        { label: 'Measured power',
          get: (item) => (item.telemetry_valid ? formatPower(item.measured_power_kw) : '—') },
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

    /* The diagnosis itself lives in devices-utils.js, where it is unit-tested
     * (web/tests/devices-utils.test.js) rather than only rendered. This file
     * supplies the channel endpoint that names the configured unit id, which is
     * the fact the offline case most often turns on. */
    const utils = () => window.PvdgDeviceUtils;

    function endpointFor(index) {
        const channels = Array.isArray(window.PvdgInverterEndpoints) ? window.PvdgInverterEndpoints : null;
        if (!channels) return null;
        return channels.find((entry) => Number(entry.index) === Number(index)) || null;
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
        /* Above the numbers, not below them: an engineer who sees "Unavailable"
         * should read the cause in the same glance. */
        const findings = node('div', 'device-findings');
        findings.id = 'inverterTelemetryFindings';
        findings.setAttribute('role', 'status');
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
        panel.append(header, message, summary, findings, wrap);
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

    function renderFindings(data, inverters) {
        const target = byId('inverterTelemetryFindings');
        if (!target) return;
        const helpers = utils();
        if (!helpers) return;
        const findings = inverters
            .map((item) => helpers.diagnoseInverter(item, endpointFor(item.index)))
            .filter(Boolean);
        const fleet = helpers.diagnoseInverterFleet(data, findings.length);
        target.replaceChildren();
        if (fleet) target.append(node('p', 'device-finding fleet', fleet));
        findings.forEach((finding) => target.append(node('p', `device-finding ${finding.tone}`, finding.text)));
        if (!fleet && !findings.length) {
            target.append(node('p', 'device-finding good', 'Every configured inverter is answering with a fresh production sample.'));
        }
    }

    function render(data) {
        ensureScaffold();
        const summary = byId('inverterTelemetryLiveSummary');
        const rows = byId('inverterTelemetryLiveRows');
        const message = byId('inverterTelemetryLiveMessage');
        if (!summary || !rows || !message) return;
        const values = data?.summary || {};
        /* ONLY WHAT THIS ENDPOINT ALONE KNOWS.
         *
         * "Answering" and "Commandable capacity" were here too, from a
         * different poll than the roll-up above, so the page carried two
         * numbers for each and they could disagree. They live above now. What
         * remains is what only the telemetry read can say: whether anything was
         * measured, and whether the machines identified themselves. */
        summary.replaceChildren(
            summaryCard('Measured production',
                Number(values.telemetry_valid) > 0 ? formatPower(values.measured_total_kw) : '—',
                Number(values.telemetry_valid) > 0
                    ? `${values.telemetry_valid} valid telemetry channels`
                    : 'No inverter reported a measurement'),
            summaryCard('Identity verified', values.identity_verified ?? 0,
                `${values.stale ?? 0} stale channels`)
        );
        rows.replaceChildren();
        const inverters = Array.isArray(data?.inverters) ? data.inverters : [];
        renderFindings(data, inverters);
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
