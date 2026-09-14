(() => {
    'use strict';

    const POLL_MS = 30000;
    const REQUEST_TIMEOUT_MS = 7000;
    const VALID_RANGES = new Set(['15m', '1h', '24h']);
    const state = {
        range: '1h',
        timer: null,
        controller: null,
        loading: false,
        history: null,
        events: null,
        alarms: null,
        refreshedAt: null
    };

    const byId = (id) => document.getElementById(id);
    const route = () => window.location.hash.replace(/^#\/?/, '').split(/[?&]/, 1)[0] || 'dashboard';
    const node = (tag, className = '', text = '') => {
        const item = document.createElement(tag);
        if (className) item.className = className;
        if (text) item.textContent = text;
        return item;
    };

    function formatPower(value) {
        const number = Number(value);
        return Number.isFinite(number) ? `${number.toFixed(1)} kW` : '--';
    }

    function formatCount(value) {
        const number = Number(value);
        return Number.isFinite(number) ? String(Math.max(0, Math.round(number))) : '0';
    }

    function formatAge(ageMs) {
        const age = Number(ageMs);
        if (!Number.isFinite(age) || age < 0) return '--';
        if (age < 1000) return 'now';
        if (age < 60000) return `${Math.round(age / 1000)} s ago`;
        if (age < 3600000) return `${Math.round(age / 60000)} min ago`;
        return `${(age / 3600000).toFixed(1)} h ago`;
    }

    function toneClass(value) {
        const text = String(value || '').toLowerCase();
        if (/critical|fault|offline|failed|blocked|active/.test(text)) return 'bad';
        if (/warning|attention|stale|pending/.test(text)) return 'warning';
        if (/normal|healthy|online|clear|ready|good/.test(text)) return 'good';
        return 'neutral';
    }

    async function request(path, controller) {
        const timer = window.setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS);
        try {
            const response = await fetch(path, {
                cache: 'no-store',
                credentials: 'same-origin',
                signal: controller.signal
            });
            const text = await response.text();
            let payload = null;
            if (text) {
                try { payload = JSON.parse(text); }
                catch { throw new Error(`Invalid controller response from ${path}`); }
            }
            if (!response.ok) throw new Error(payload?.error || `${response.status} ${response.statusText}`);
            return payload || {};
        } finally {
            window.clearTimeout(timer);
        }
    }

    function installNav() {
        const nav = document.querySelector('.nav-list');
        if (!nav || nav.querySelector('[data-route="reports"]')) return;
        const link = node('a', 'nav-link');
        link.href = '#/reports';
        link.dataset.route = 'reports';
        link.setAttribute('aria-label', 'Reports');
        link.innerHTML = '<span aria-hidden="true">▤</span><span>Reports</span>';
        const alarms = nav.querySelector('[data-route="alarms"]');
        const readiness = nav.querySelector('[data-route="readiness"]');
        if (readiness) nav.insertBefore(link, readiness);
        else if (alarms?.nextSibling) nav.insertBefore(link, alarms.nextSibling);
        else nav.append(link);
    }

    function installPage() {
        if (byId('reportsWorkspace')) return;
        const content = byId('mainContent') || document.querySelector('.content');
        if (!content) return;
        const page = node('section', 'page reports-page');
        page.dataset.page = 'reports';
        page.id = 'reportsWorkspace';
        page.setAttribute('aria-labelledby', 'reportsTitle');
        page.innerHTML = `
            <div class="page-intro reports-intro">
                <div>
                    <p class="eyebrow">Operational reporting</p>
                    <h2 id="reportsTitle">Plant reports</h2>
                    <p>Review controller-resident operating history, alarm activity and event chronology. Export evidence without exposing Engineering configuration or credentials.</p>
                </div>
                <div class="reports-freshness" id="reportsFreshness">Not loaded</div>
            </div>
            <section class="reports-toolbar" aria-label="Report controls">
                <div class="reports-range" role="group" aria-label="Report time range">
                    <button class="op-range-button" type="button" data-report-range="15m">15 min</button>
                    <button class="op-range-button active" type="button" data-report-range="1h">1 hour</button>
                    <button class="op-range-button" type="button" data-report-range="24h">24 hours</button>
                </div>
                <div class="reports-actions">
                    <button class="button secondary" id="reportsRefresh" type="button">Refresh</button>
                    <button class="button secondary" id="reportsCsv" type="button" disabled>Export CSV</button>
                    <button class="button secondary" id="reportsJson" type="button" disabled>Export JSON</button>
                    <button class="button primary" id="reportsPrint" type="button" disabled>Print report</button>
                </div>
            </section>
            <div class="reports-boundary" role="note">
                <strong>Controller-resident operational record</strong>
                <span>This is a rolling service/operations window, not billing-grade or long-term historian data.</span>
            </div>
            <div class="reports-state" id="reportsState" role="status">Choose a range or refresh to load the report.</div>
            <section class="reports-kpis" aria-label="Report summary">
                <article class="reports-kpi"><span>Samples</span><strong id="reportsSamples">--</strong><small id="reportsCoverage">No data</small></article>
                <article class="reports-kpi"><span>Average grid</span><strong id="reportsGridAverage">--</strong><small id="reportsGridRange">Min / max --</small></article>
                <article class="reports-kpi"><span>Average solar</span><strong id="reportsSolarAverage">--</strong><small id="reportsSolarRange">Min / max --</small></article>
                <article class="reports-kpi"><span>Attention</span><strong id="reportsAttention">--</strong><small id="reportsEventsCount">No events loaded</small></article>
            </section>
            <section class="reports-grid">
                <article class="panel reports-trend-panel">
                    <div class="panel-header"><div><p class="eyebrow">Trend</p><h3>Grid and solar power</h3></div><span class="reports-legend"><i class="grid"></i>Grid <i class="solar"></i>Solar</span></div>
                    <div class="reports-chart-wrap" id="reportsChart" aria-label="Power trend chart"></div>
                </article>
                <article class="panel reports-condition-panel">
                    <div class="panel-header"><div><p class="eyebrow">Current condition</p><h3>Alarm summary</h3></div></div>
                    <div class="reports-condition-list" id="reportsAlarmSummary"><p class="muted-text">No alarm data loaded.</p></div>
                </article>
            </section>
            <section class="panel reports-events-panel">
                <div class="panel-header"><div><p class="eyebrow">Chronology</p><h3>Recent events</h3></div><span class="subtle-badge" id="reportsEventBadge">0 records</span></div>
                <div class="reports-event-list" id="reportsEventList"><p class="muted-text">No event data loaded.</p></div>
            </section>
            <section class="panel reports-table-panel">
                <div class="panel-header"><div><p class="eyebrow">Evidence</p><h3>Recent samples</h3></div><span class="subtle-badge" id="reportsSampleBadge">0 samples</span></div>
                <div class="reports-table-scroll">
                    <table class="reports-table">
                        <thead><tr><th>Age</th><th>Grid kW</th><th>Solar kW</th><th>Meter</th><th>Inverters</th><th>Control</th><th>Alarm flags</th></tr></thead>
                        <tbody id="reportsSampleRows"><tr><td colspan="7">No samples loaded.</td></tr></tbody>
                    </table>
                </div>
            </section>`;
        content.append(page);

        page.querySelectorAll('[data-report-range]').forEach((button) => {
            button.addEventListener('click', () => {
                const requested = button.dataset.reportRange;
                if (!VALID_RANGES.has(requested) || state.range === requested) return;
                state.range = requested;
                page.querySelectorAll('[data-report-range]').forEach((item) => {
                    item.classList.toggle('active', item.dataset.reportRange === requested);
                    item.setAttribute('aria-pressed', item.dataset.reportRange === requested ? 'true' : 'false');
                });
                refresh();
            });
        });
        byId('reportsRefresh')?.addEventListener('click', refresh);
        byId('reportsCsv')?.addEventListener('click', exportCsv);
        byId('reportsJson')?.addEventListener('click', exportJson);
        byId('reportsPrint')?.addEventListener('click', () => window.print());
    }

    function setState(message, tone = '') {
        const target = byId('reportsState');
        if (!target) return;
        target.textContent = message;
        target.className = `reports-state${tone ? ` ${tone}` : ''}`;
    }

    function setButtonState(enabled) {
        ['reportsCsv', 'reportsJson', 'reportsPrint'].forEach((id) => {
            const button = byId(id);
            if (button) button.disabled = !enabled;
        });
    }

    function chartSeries(samples, key) {
        return samples.map((sample, index) => {
            const value = Number(sample?.[key]);
            return Number.isFinite(value) ? { index, value } : null;
        }).filter(Boolean);
    }

    function renderChart(samples) {
        const target = byId('reportsChart');
        if (!target) return;
        target.replaceChildren();
        const grid = chartSeries(samples, 'grid_kw');
        const solar = chartSeries(samples, 'solar_kw');
        const all = [...grid, ...solar];
        if (samples.length < 2 || all.length < 2) {
            target.append(node('div', 'reports-empty', 'Not enough valid measurements to draw a trend.'));
            return;
        }
        let min = Math.min(...all.map((point) => point.value), 0);
        let max = Math.max(...all.map((point) => point.value), 0);
        if (Math.abs(max - min) < 0.001) { max += 1; min -= 1; }
        const width = 900;
        const height = 250;
        const inset = 24;
        const x = (index) => inset + ((width - 2 * inset) * index / Math.max(1, samples.length - 1));
        const y = (value) => inset + (height - 2 * inset) * (1 - ((value - min) / (max - min)));
        const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
        svg.setAttribute('viewBox', `0 0 ${width} ${height}`);
        svg.setAttribute('role', 'img');
        svg.setAttribute('aria-label', `Grid and solar power trend for ${state.range}`);
        svg.classList.add('reports-chart');
        const zero = document.createElementNS(svg.namespaceURI, 'line');
        zero.setAttribute('x1', inset); zero.setAttribute('x2', width - inset);
        zero.setAttribute('y1', y(0)); zero.setAttribute('y2', y(0));
        zero.setAttribute('class', 'reports-zero-line');
        svg.append(zero);
        const addLine = (series, className) => {
            if (series.length < 2) return;
            const polyline = document.createElementNS(svg.namespaceURI, 'polyline');
            polyline.setAttribute('points', series.map((point) => `${x(point.index).toFixed(1)},${y(point.value).toFixed(1)}`).join(' '));
            polyline.setAttribute('class', className);
            polyline.setAttribute('fill', 'none');
            polyline.setAttribute('vector-effect', 'non-scaling-stroke');
            svg.append(polyline);
        };
        addLine(grid, 'reports-grid-line');
        addLine(solar, 'reports-solar-line');
        target.append(svg);
        const scale = node('div', 'reports-chart-scale');
        scale.append(node('span', '', formatPower(max)), node('span', '', `0 kW`), node('span', '', formatPower(min)));
        target.append(scale);
    }

    function renderAlarmSummary(payload) {
        const target = byId('reportsAlarmSummary');
        if (!target) return;
        target.replaceChildren();
        const summary = payload?.summary || {};
        const entries = [
            ['Active alarms', summary.active ?? summary.active_count ?? 0],
            ['Primary active', summary.primary_active ?? 0],
            ['Unacknowledged', summary.unacknowledged ?? summary.unacknowledged_count ?? 0],
            ['Suppressed transitions', summary.suppressed_transitions ?? summary.suppressed_total ?? 0]
        ];
        entries.forEach(([label, value]) => {
            const row = node('div', 'reports-condition-row');
            row.append(node('span', '', label), node('strong', Number(value) > 0 ? 'warning-text' : 'good-text', formatCount(value)));
            target.append(row);
        });
    }

    function renderEvents(payload) {
        const target = byId('reportsEventList');
        if (!target) return;
        target.replaceChildren();
        const events = Array.isArray(payload?.events) ? payload.events : [];
        const badge = byId('reportsEventBadge');
        if (badge) badge.textContent = `${events.length} record${events.length === 1 ? '' : 's'}`;
        if (!events.length) {
            target.append(node('p', 'reports-empty', 'No operational events are stored in the current controller window.'));
            return;
        }
        events.slice(0, 12).forEach((event) => {
            const row = node('article', `reports-event ${toneClass(event.severity)}${event.active ? ' active' : ''}`);
            const main = node('div', 'reports-event-main');
            main.append(node('strong', '', event.title || 'Controller event'), node('span', '', event.detail || 'Operational state changed.'));
            const meta = node('div', 'reports-event-meta');
            meta.append(node('span', '', formatAge(event.age_ms)), node('span', '', event.severity || 'information'));
            if (event.recommended_action) main.append(node('small', '', `Action: ${event.recommended_action}`));
            row.append(main, meta);
            target.append(row);
        });
    }

    function renderSamples(samples) {
        const body = byId('reportsSampleRows');
        const badge = byId('reportsSampleBadge');
        if (badge) badge.textContent = `${samples.length} sample${samples.length === 1 ? '' : 's'}`;
        if (!body) return;
        body.replaceChildren();
        if (!samples.length) {
            const row = document.createElement('tr');
            const cell = document.createElement('td');
            cell.colSpan = 7;
            cell.textContent = 'No controller-resident samples are available for this range.';
            row.append(cell); body.append(row); return;
        }
        samples.slice(-20).reverse().forEach((sample) => {
            const row = document.createElement('tr');
            const values = [
                formatAge(sample.age_ms),
                formatPower(sample.grid_kw),
                formatPower(sample.solar_kw),
                sample.meter_online ? 'Online' : 'Unavailable',
                `${formatCount(sample.inverter_online)} / ${formatCount(sample.inverter_enabled)}`,
                sample.control_enabled ? 'Enabled' : 'Disabled',
                `0x${Math.max(0, Number(sample.alarms) || 0).toString(16).toUpperCase()}`
            ];
            values.forEach((value) => row.append(node('td', '', value)));
            body.append(row);
        });
    }

    function render() {
        const history = state.history || {};
        const samples = Array.isArray(history.samples) ? history.samples : [];
        const summary = history.summary || {};
        byId('reportsSamples').textContent = formatCount(samples.length);
        byId('reportsCoverage').textContent = samples.length
            ? `${history.range || state.range} · ${(Number(history.sample_interval_ms) / 1000).toFixed(0)} s sample interval`
            : 'No valid samples';
        byId('reportsGridAverage').textContent = formatPower(summary.grid_average_kw);
        byId('reportsGridRange').textContent = `Min ${formatPower(summary.grid_min_kw)} · Max ${formatPower(summary.grid_max_kw)}`;
        byId('reportsSolarAverage').textContent = formatPower(summary.solar_average_kw);
        byId('reportsSolarRange').textContent = `Min ${formatPower(summary.solar_min_kw)} · Max ${formatPower(summary.solar_max_kw)}`;
        const eventSummary = state.events?.summary || {};
        const attention = Number(eventSummary.active_critical || 0) + Number(eventSummary.active_warning || 0);
        byId('reportsAttention').textContent = formatCount(attention);
        byId('reportsEventsCount').textContent = `${formatCount(eventSummary.stored_events)} stored events`;
        byId('reportsFreshness').textContent = state.refreshedAt ? `Updated ${state.refreshedAt.toLocaleTimeString()}` : 'Not loaded';
        renderChart(samples);
        renderAlarmSummary(state.alarms);
        renderEvents(state.events);
        renderSamples(samples);
        setButtonState(samples.length > 0 || Array.isArray(state.events?.events));
        setState(samples.length ? `Report ready · ${samples.length} samples in the ${history.range || state.range} controller window.` : 'Report loaded, but this controller window contains no samples.', samples.length ? 'good' : 'warning');
    }

    function stop() {
        window.clearTimeout(state.timer);
        state.timer = null;
        state.controller?.abort();
        state.controller = null;
    }

    function schedule() {
        window.clearTimeout(state.timer);
        state.timer = null;
        if (route() !== 'reports' || document.hidden || state.loading) return;
        state.timer = window.setTimeout(refresh, POLL_MS);
    }

    async function refresh() {
        if (route() !== 'reports' || document.hidden || state.loading) return;
        stop();
        state.loading = true;
        const refreshButton = byId('reportsRefresh');
        if (refreshButton) refreshButton.disabled = true;
        setState('Loading controller-resident history…', 'loading');
        setButtonState(false);
        const controller = new AbortController();
        state.controller = controller;
        try {
            state.history = await request(`/api/operator/history?range=${encodeURIComponent(state.range)}`, controller);
            if (controller.signal.aborted) return;
            setState('History loaded. Loading event chronology…', 'loading');
            state.events = await request('/api/operator/events', controller);
            if (controller.signal.aborted) return;
            setState('Events loaded. Loading alarm condition table…', 'loading');
            state.alarms = await request('/api/operator/alarms', controller);
            state.refreshedAt = new Date();
            render();
        } catch (error) {
            if (error?.name !== 'AbortError') setState(`Report unavailable: ${error.message}`, 'bad');
        } finally {
            if (state.controller === controller) state.controller = null;
            state.loading = false;
            if (refreshButton) refreshButton.disabled = false;
            schedule();
        }
    }

    function download(name, type, content) {
        const blob = new Blob([content], { type });
        const url = URL.createObjectURL(blob);
        const anchor = document.createElement('a');
        anchor.href = url;
        anchor.download = name;
        document.body.append(anchor);
        anchor.click();
        anchor.remove();
        window.setTimeout(() => URL.revokeObjectURL(url), 1000);
    }

    function csvCell(value) {
        const text = value == null ? '' : String(value);
        return `"${text.replaceAll('"', '""')}"`;
    }

    function exportCsv() {
        const samples = Array.isArray(state.history?.samples) ? state.history.samples : [];
        if (!samples.length) return;
        const generatedAt = Date.now();
        const rows = [['timestamp_estimate', 'age_ms', 'grid_kw', 'solar_kw', 'meter_online', 'inverter_online', 'inverter_enabled', 'control_enabled', 'alarm_flags']];
        samples.forEach((sample) => {
            const age = Number(sample.age_ms) || 0;
            rows.push([
                new Date(generatedAt - age).toISOString(), age,
                sample.grid_kw ?? '', sample.solar_kw ?? '',
                Boolean(sample.meter_online), sample.inverter_online ?? 0,
                sample.inverter_enabled ?? 0, Boolean(sample.control_enabled), sample.alarms ?? 0
            ]);
        });
        const csv = rows.map((row) => row.map(csvCell).join(',')).join('\r\n');
        download(`automatrix-report-${state.range}-${new Date().toISOString().replaceAll(':', '-')}.csv`, 'text/csv;charset=utf-8', csv);
    }

    function exportJson() {
        if (!state.history && !state.events && !state.alarms) return;
        const bundle = {
            schema: 1,
            kind: 'automatrix_controller_resident_operational_report',
            generated_at: new Date().toISOString(),
            requested_range: state.range,
            limitations: {
                controller_resident_window: true,
                billing_grade: false,
                long_term_historian: false,
                engineering_configuration_included: false,
                credentials_included: false
            },
            history: state.history,
            events: state.events,
            alarms: state.alarms
        };
        download(`automatrix-report-${state.range}-${new Date().toISOString().replaceAll(':', '-')}.json`, 'application/json;charset=utf-8', JSON.stringify(bundle, null, 2));
    }

    function activateReportsRoute() {
        if (route() !== 'reports') return false;
        document.querySelectorAll('.page').forEach((page) => page.classList.toggle('active', page.dataset.page === 'reports'));
        document.querySelectorAll('.nav-link').forEach((link) => link.classList.toggle('active', link.dataset.route === 'reports'));
        const title = byId('pageTitle');
        const breadcrumb = byId('breadcrumbCurrent');
        const context = document.querySelector('.shell-page-context');
        if (title) title.textContent = 'Reports';
        if (breadcrumb) breadcrumb.textContent = 'Operational reports';
        if (context) context.textContent = 'Controller-resident trends, alarms and service evidence';
        document.title = 'Reports · Automatrix PV-DG';
        document.body.classList.remove('menu-open');
        const menuButton = byId('menuButton');
        if (menuButton) menuButton.setAttribute('aria-expanded', 'false');
        return true;
    }

    function start() {
        installNav();
        installPage();
        if (activateReportsRoute()) refresh();
        window.addEventListener('hashchange', () => {
            if (activateReportsRoute()) refresh();
            else stop();
        });
        document.addEventListener('visibilitychange', () => {
            if (document.hidden) stop();
            else if (route() === 'reports') refresh();
        });
        window.addEventListener('beforeunload', stop);
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();
