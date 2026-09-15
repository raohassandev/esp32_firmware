(() => {
    'use strict';

    const POLL_MS = 30000;
    const REQUEST_TIMEOUT_MS = 7000;
    const VALID_RANGES = new Set(['15m', '1h', '24h']);
    const ENDPOINTS = {
        history: (range) => `/api/operator/history?range=${encodeURIComponent(range)}`,
        events: () => '/api/operator/events',
        alarms: () => '/api/operator/alarms'
    };
    const state = {
        range: '1h',
        timer: null,
        controller: null,
        loading: false,
        history: null,
        events: null,
        alarms: null,
        refreshedAt: null,
        quality: {
            history: { available: false, error: 'Not loaded' },
            events: { available: false, error: 'Not loaded' },
            alarms: { available: false, error: 'Not loaded' }
        }
    };

    const byId = (id) => document.getElementById(id);
    const route = () => window.location.hash.replace(/^#\/?/, '').split(/[?&]/, 1)[0] || 'dashboard';
    const node = (tag, className = '', text = '') => {
        const item = document.createElement(tag);
        if (className) item.className = className;
        if (text) item.textContent = text;
        return item;
    };
    const statusText = (id, fallback = '--') => (byId(id)?.textContent || fallback).trim();

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

    function estimatedTimestamp(ageMs) {
        if (!state.refreshedAt) return '--';
        const age = Number(ageMs);
        if (!Number.isFinite(age) || age < 0) return '--';
        return new Date(state.refreshedAt.getTime() - age).toLocaleString();
    }

    function toneClass(value) {
        const text = String(value || '').toLowerCase();
        if (/critical|fault|offline|failed|blocked|active/.test(text)) return 'bad';
        if (/warning|attention|stale|pending/.test(text)) return 'warning';
        if (/normal|healthy|online|clear|ready|good/.test(text)) return 'good';
        return 'neutral';
    }

    function escapeHtml(value) {
        return String(value ?? '')
            .replaceAll('&', '&amp;')
            .replaceAll('<', '&lt;')
            .replaceAll('>', '&gt;')
            .replaceAll('"', '&quot;')
            .replaceAll("'", '&#039;');
    }

    async function request(path, parentSignal) {
        const controller = new AbortController();
        const abortFromParent = () => controller.abort();
        if (parentSignal?.aborted) controller.abort();
        else parentSignal?.addEventListener('abort', abortFromParent, { once: true });
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
        } catch (error) {
            if (error?.name === 'AbortError' && !parentSignal?.aborted) {
                throw new Error(`Timed out after ${REQUEST_TIMEOUT_MS / 1000} s`);
            }
            throw error;
        } finally {
            window.clearTimeout(timer);
            parentSignal?.removeEventListener('abort', abortFromParent);
        }
    }

    function installNav() {
        const nav = document.querySelector('.nav-list');
        if (!nav) return;
        let link = nav.querySelector('[data-route="reports"]');
        if (!link) {
            link = node('a', 'nav-link');
            link.href = '#/reports';
            link.dataset.route = 'reports';
            link.setAttribute('aria-label', 'Reports');
            link.innerHTML = '<span aria-hidden="true">▤</span><span>Reports</span>';
            nav.append(link);
        }
        placeNav();
    }

    function placeNav() {
        const nav = document.querySelector('.nav-list');
        const link = nav?.querySelector('[data-route="reports"]');
        if (!nav || !link) return;
        /* Industrial UI v1 owns the primary grouping/reorder pass. Reports is a
           dynamic route, so place its own link after that pass at the end of the
           operator group rather than creating another global nav owner. */
        const control = nav.querySelector('[data-route="control"]');
        if (control && link.nextElementSibling !== control) nav.insertBefore(link, control);
        const span = link.querySelector(':scope > span:last-child');
        if (span) span.textContent = 'Reports';
        link.setAttribute('aria-label', 'Reports');
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
                    <p>Review controller-resident operating history, alarm activity and event chronology. Export service evidence without exposing Engineering configuration or credentials.</p>
                </div>
                <div class="reports-freshness" id="reportsFreshness">Not loaded</div>
            </div>
            <section class="reports-toolbar" aria-label="Report controls">
                <div class="reports-range" role="group" aria-label="Report time range">
                    <button class="op-range-button" type="button" data-report-range="15m" aria-pressed="false">15 min</button>
                    <button class="op-range-button active" type="button" data-report-range="1h" aria-pressed="true">1 hour</button>
                    <button class="op-range-button" type="button" data-report-range="24h" aria-pressed="false">24 hours</button>
                </div>
                <div class="reports-actions">
                    <button class="button secondary" id="reportsRefresh" type="button">Refresh</button>
                    <button class="button secondary" id="reportsCsv" type="button" disabled>CSV</button>
                    <button class="button secondary" id="reportsJson" type="button" disabled>JSON</button>
                    <button class="button secondary" id="reportsHtml" type="button" disabled>HTML</button>
                    <button class="button primary" id="reportsPrint" type="button" disabled>Print</button>
                </div>
            </section>
            <div class="reports-boundary" role="note">
                <strong>Controller-resident operational record</strong>
                <span>This is a rolling service/operations window, not billing-grade or long-term historian data. Sample timestamps are estimates reconstructed from controller-reported age.</span>
            </div>
            <section class="reports-meta" aria-label="Report metadata">
                <div><span>Generated</span><strong id="reportsMetaGenerated">--</strong></div>
                <div><span>Window</span><strong id="reportsMetaWindow">1 hour</strong></div>
                <div><span>Controller</span><strong id="reportsMetaController">--</strong></div>
                <div><span>Data freshness</span><strong id="reportsMetaFreshness">--</strong></div>
            </section>
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
                        <thead><tr><th>Estimated time</th><th>Age</th><th>Grid kW</th><th>Solar kW</th><th>Meter</th><th>Inverters</th><th>Control</th><th>Alarm flags</th></tr></thead>
                        <tbody id="reportsSampleRows"><tr><td colspan="8">No samples loaded.</td></tr></tbody>
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
        byId('reportsHtml')?.addEventListener('click', exportHtml);
        byId('reportsPrint')?.addEventListener('click', () => window.print());
    }

    function setState(message, tone = '') {
        const target = byId('reportsState');
        if (!target) return;
        target.textContent = message;
        target.className = `reports-state${tone ? ` ${tone}` : ''}`;
    }

    function setButtonState() {
        const samples = Array.isArray(state.history?.samples) ? state.history.samples : [];
        const anyData = ['history', 'events', 'alarms'].some((key) => state.quality[key].available);
        const csv = byId('reportsCsv');
        if (csv) csv.disabled = samples.length === 0;
        ['reportsJson', 'reportsHtml', 'reportsPrint'].forEach((id) => {
            const button = byId(id);
            if (button) button.disabled = !anyData;
        });
    }

    function chartSeries(samples, key) {
        return samples.map((sample, index) => {
            const value = Number(sample?.[key]);
            return Number.isFinite(value) ? { index, value } : null;
        }).filter(Boolean);
    }

    function renderChart(samples, available = true) {
        const target = byId('reportsChart');
        if (!target) return;
        target.replaceChildren();
        if (!available) {
            target.append(node('div', 'reports-empty bad-text', 'History endpoint unavailable for this refresh.'));
            return;
        }
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
        scale.append(node('span', '', formatPower(max)), node('span', '', '0 kW'), node('span', '', formatPower(min)));
        target.append(scale);
    }

    function renderAlarmSummary(payload, available = true) {
        const target = byId('reportsAlarmSummary');
        if (!target) return;
        target.replaceChildren();
        if (!available) {
            target.append(node('p', 'reports-empty bad-text', 'Alarm summary unavailable for this refresh.'));
            return;
        }
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

    function renderEvents(payload, available = true) {
        const target = byId('reportsEventList');
        if (!target) return;
        target.replaceChildren();
        const badge = byId('reportsEventBadge');
        if (!available) {
            if (badge) badge.textContent = 'Unavailable';
            target.append(node('p', 'reports-empty bad-text', 'Event chronology unavailable for this refresh.'));
            return;
        }
        const events = Array.isArray(payload?.events) ? payload.events : [];
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

    function renderSamples(samples, available = true) {
        const body = byId('reportsSampleRows');
        const badge = byId('reportsSampleBadge');
        if (badge) badge.textContent = available ? `${samples.length} sample${samples.length === 1 ? '' : 's'}` : 'Unavailable';
        if (!body) return;
        body.replaceChildren();
        if (!available || !samples.length) {
            const row = document.createElement('tr');
            const cell = document.createElement('td');
            cell.colSpan = 8;
            cell.textContent = available ? 'No controller-resident samples are available for this range.' : 'History endpoint unavailable for this refresh.';
            row.append(cell); body.append(row); return;
        }
        samples.slice(-20).reverse().forEach((sample) => {
            const row = document.createElement('tr');
            const values = [
                estimatedTimestamp(sample.age_ms),
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

    function rangeLabel(value = state.range) {
        return value === '15m' ? '15 minutes' : value === '24h' ? '24 hours' : '1 hour';
    }

    function qualitySummary() {
        const failed = Object.entries(state.quality).filter(([, value]) => !value.available);
        if (!failed.length) return { tone: 'good', message: 'Report ready · all controller data sources responded.' };
        if (failed.length === 3) return { tone: 'bad', message: `Report unavailable · ${failed.map(([key]) => key).join(', ')} data sources failed.` };
        return { tone: 'warning', message: `Partial report · unavailable: ${failed.map(([key]) => key).join(', ')}. Available sections remain usable.` };
    }

    function renderMetadata() {
        byId('reportsMetaGenerated').textContent = state.refreshedAt ? state.refreshedAt.toLocaleString() : '--';
        byId('reportsMetaWindow').textContent = rangeLabel();
        byId('reportsMetaController').textContent = statusText('statusController', 'Unknown');
        byId('reportsMetaFreshness').textContent = statusText('statusUpdated', 'Unknown');
        byId('reportsFreshness').textContent = state.refreshedAt ? `Updated ${state.refreshedAt.toLocaleTimeString()}` : 'Not loaded';
    }

    function render() {
        const historyAvailable = state.quality.history.available;
        const eventsAvailable = state.quality.events.available;
        const alarmsAvailable = state.quality.alarms.available;
        const history = historyAvailable ? (state.history || {}) : {};
        const samples = Array.isArray(history.samples) ? history.samples : [];
        const summary = history.summary || {};

        byId('reportsSamples').textContent = historyAvailable ? formatCount(samples.length) : '--';
        byId('reportsCoverage').textContent = historyAvailable && samples.length
            ? `${history.range || state.range} · ${Number.isFinite(Number(history.sample_interval_ms)) ? `${(Number(history.sample_interval_ms) / 1000).toFixed(0)} s sample interval` : 'controller interval'}`
            : historyAvailable ? 'No valid samples' : 'History unavailable';
        byId('reportsGridAverage').textContent = historyAvailable ? formatPower(summary.grid_average_kw) : '--';
        byId('reportsGridRange').textContent = historyAvailable ? `Min ${formatPower(summary.grid_min_kw)} · Max ${formatPower(summary.grid_max_kw)}` : 'History unavailable';
        byId('reportsSolarAverage').textContent = historyAvailable ? formatPower(summary.solar_average_kw) : '--';
        byId('reportsSolarRange').textContent = historyAvailable ? `Min ${formatPower(summary.solar_min_kw)} · Max ${formatPower(summary.solar_max_kw)}` : 'History unavailable';

        const eventSummary = eventsAvailable ? (state.events?.summary || {}) : {};
        const attention = Number(eventSummary.active_critical || 0) + Number(eventSummary.active_warning || 0);
        byId('reportsAttention').textContent = eventsAvailable ? formatCount(attention) : '--';
        byId('reportsEventsCount').textContent = eventsAvailable ? `${formatCount(eventSummary.stored_events)} stored events` : 'Events unavailable';

        renderMetadata();
        renderChart(samples, historyAvailable);
        renderAlarmSummary(state.alarms, alarmsAvailable);
        renderEvents(state.events, eventsAvailable);
        renderSamples(samples, historyAvailable);
        setButtonState();
        const quality = qualitySummary();
        setState(quality.message, quality.tone);
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

    function applySettled(key, result) {
        if (result.status === 'fulfilled') {
            state[key] = result.value;
            state.quality[key] = { available: true, error: null };
            return;
        }
        state[key] = null;
        const reason = result.reason?.name === 'AbortError' ? 'Request cancelled' : (result.reason?.message || 'Unavailable');
        state.quality[key] = { available: false, error: reason };
    }

    async function refresh() {
        if (route() !== 'reports' || document.hidden || state.loading) return;
        stop();
        state.loading = true;
        const refreshButton = byId('reportsRefresh');
        if (refreshButton) refreshButton.disabled = true;
        setState('Loading report data sources…', 'loading');
        const controller = new AbortController();
        state.controller = controller;
        try {
            const results = await Promise.allSettled([
                request(ENDPOINTS.history(state.range), controller.signal),
                request(ENDPOINTS.events(), controller.signal),
                request(ENDPOINTS.alarms(), controller.signal)
            ]);
            if (controller.signal.aborted) return;
            applySettled('history', results[0]);
            applySettled('events', results[1]);
            applySettled('alarms', results[2]);
            state.refreshedAt = new Date();
            render();
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
        if (!state.quality.history.available || !samples.length) return;
        const generatedAt = state.refreshedAt?.getTime() || Date.now();
        const rows = [['timestamp_estimate', 'age_ms', 'grid_kw', 'solar_kw', 'meter_online', 'inverter_online', 'inverter_enabled', 'control_enabled', 'alarm_flags']];
        samples.forEach((sample) => {
            const age = Math.max(0, Number(sample.age_ms) || 0);
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

    function reportBundle() {
        return {
            schema: 2,
            kind: 'automatrix_controller_resident_operational_report',
            generated_at: (state.refreshedAt || new Date()).toISOString(),
            requested_range: state.range,
            controller_state: statusText('statusController', 'Unknown'),
            controller_data_freshness: statusText('statusUpdated', 'Unknown'),
            data_quality: structuredClone(state.quality),
            limitations: {
                controller_resident_window: true,
                billing_grade: false,
                long_term_historian: false,
                sample_timestamps_are_estimates: true,
                engineering_configuration_included: false,
                credentials_included: false,
                physical_qualification_claimed: false
            },
            history: state.quality.history.available ? state.history : null,
            events: state.quality.events.available ? state.events : null,
            alarms: state.quality.alarms.available ? state.alarms : null
        };
    }

    function exportJson() {
        if (!['history', 'events', 'alarms'].some((key) => state.quality[key].available)) return;
        const bundle = reportBundle();
        download(`automatrix-report-${state.range}-${new Date().toISOString().replaceAll(':', '-')}.json`, 'application/json;charset=utf-8', JSON.stringify(bundle, null, 2));
    }

    function exportHtml() {
        if (!['history', 'events', 'alarms'].some((key) => state.quality[key].available)) return;
        const bundle = reportBundle();
        const samples = Array.isArray(bundle.history?.samples) ? bundle.history.samples.slice(-40).reverse() : [];
        const events = Array.isArray(bundle.events?.events) ? bundle.events.events.slice(0, 20) : [];
        const historySummary = bundle.history?.summary || {};
        const qualityRows = Object.entries(bundle.data_quality).map(([name, value]) =>
            `<tr><td>${escapeHtml(name)}</td><td>${value.available ? 'Available' : 'Unavailable'}</td><td>${escapeHtml(value.error || '')}</td></tr>`).join('');
        const sampleRows = samples.map((sample) => `<tr><td>${escapeHtml(estimatedTimestamp(sample.age_ms))}</td><td>${escapeHtml(formatPower(sample.grid_kw))}</td><td>${escapeHtml(formatPower(sample.solar_kw))}</td><td>${sample.meter_online ? 'Online' : 'Unavailable'}</td><td>${sample.control_enabled ? 'Enabled' : 'Disabled'}</td></tr>`).join('') || '<tr><td colspan="5">No sample rows available.</td></tr>';
        const eventRows = events.map((event) => `<tr><td>${escapeHtml(formatAge(event.age_ms))}</td><td>${escapeHtml(event.severity || 'information')}</td><td>${escapeHtml(event.title || 'Controller event')}</td><td>${escapeHtml(event.detail || '')}</td></tr>`).join('') || '<tr><td colspan="4">No event rows available.</td></tr>';
        const html = `<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Automatrix operational report</title><style>body{font-family:Arial,sans-serif;margin:32px;color:#15202b}h1{margin-bottom:4px}p{line-height:1.45}.meta{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px;margin:20px 0}.meta div{border:1px solid #ccd4dc;padding:10px}.meta span{display:block;font-size:11px;color:#5a6875}.meta strong{display:block;margin-top:4px}table{width:100%;border-collapse:collapse;margin:12px 0 24px;font-size:12px}th,td{border:1px solid #ccd4dc;padding:7px;text-align:left}th{background:#f1f4f7}.notice{border-left:4px solid #376f9f;padding:10px;background:#f4f8fb}@media print{@page{size:A4 landscape;margin:12mm}body{margin:0}table{break-inside:auto}tr{break-inside:avoid}}</style></head><body><h1>Automatrix PV-DG operational report</h1><p>Controller-resident service and operations evidence.</p><div class="notice"><strong>Limitations:</strong> not billing-grade, not a long-term historian, estimated sample timestamps, no physical qualification claim.</div><div class="meta"><div><span>Generated</span><strong>${escapeHtml(bundle.generated_at)}</strong></div><div><span>Window</span><strong>${escapeHtml(rangeLabel())}</strong></div><div><span>Controller</span><strong>${escapeHtml(bundle.controller_state)}</strong></div><div><span>Freshness</span><strong>${escapeHtml(bundle.controller_data_freshness)}</strong></div></div><h2>Data quality</h2><table><thead><tr><th>Source</th><th>Status</th><th>Detail</th></tr></thead><tbody>${qualityRows}</tbody></table><h2>Power summary</h2><table><tbody><tr><th>Grid average</th><td>${escapeHtml(formatPower(historySummary.grid_average_kw))}</td><th>Solar average</th><td>${escapeHtml(formatPower(historySummary.solar_average_kw))}</td></tr><tr><th>Grid min / max</th><td>${escapeHtml(formatPower(historySummary.grid_min_kw))} / ${escapeHtml(formatPower(historySummary.grid_max_kw))}</td><th>Solar min / max</th><td>${escapeHtml(formatPower(historySummary.solar_min_kw))} / ${escapeHtml(formatPower(historySummary.solar_max_kw))}</td></tr></tbody></table><h2>Recent events</h2><table><thead><tr><th>Age</th><th>Severity</th><th>Event</th><th>Detail</th></tr></thead><tbody>${eventRows}</tbody></table><h2>Recent samples</h2><table><thead><tr><th>Estimated time</th><th>Grid</th><th>Solar</th><th>Meter</th><th>Control</th></tr></thead><tbody>${sampleRows}</tbody></table></body></html>`;
        download(`automatrix-report-${state.range}-${new Date().toISOString().replaceAll(':', '-')}.html`, 'text/html;charset=utf-8', html);
    }

    function activateReportsRoute() {
        if (route() !== 'reports') return false;
        /* The base router has a static route table. This narrow bridge only
           activates the dynamically supplied read-only Reports page; it does
           not own or mutate any other route's hierarchy. */
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
        /* Run after all DOMContentLoaded installers so the authoritative
           Industrial UI reorder pass has completed before Reports is placed. */
        requestAnimationFrame(placeNav);
        if (activateReportsRoute()) refresh();
        window.addEventListener('hashchange', () => {
            placeNav();
            if (activateReportsRoute()) refresh();
            else stop();
        });
        window.addEventListener('amx-access-change', () => requestAnimationFrame(placeNav));
        document.addEventListener('visibilitychange', () => {
            if (document.hidden) stop();
            else if (route() === 'reports') refresh();
        });
        window.addEventListener('beforeunload', stop);
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();