(() => {
    'use strict';

    const state = { history: null, events: null, range: '15m', busy: false, timer: null, enhanceQueued: false };
    const byId = (id) => document.getElementById(id);
    const isOperator = () => document.documentElement.dataset.access !== 'engineering';
    const route = () => location.hash.replace(/^#\/?/, '') || 'dashboard';

    function node(tag, className = '', text = null) {
        const item = document.createElement(tag);
        if (className) item.className = className;
        if (text != null) item.textContent = String(text);
        return item;
    }

    async function api(path) {
        const controller = new AbortController();
        const timer = window.setTimeout(() => controller.abort(), 6000);
        try {
            const response = await fetch(path, {
                cache: 'no-store',
                credentials: 'same-origin',
                signal: controller.signal
            });
            const text = await response.text();
            let payload = {};
            if (text) {
                try { payload = JSON.parse(text); }
                catch { throw new Error('Controller returned an incomplete response'); }
            }
            if (!response.ok) throw new Error(payload.error || `HTTP ${response.status}`);
            return payload;
        } catch (error) {
            if (error?.name === 'AbortError') throw new Error('Controller request timed out');
            throw error;
        } finally {
            window.clearTimeout(timer);
        }
    }

    function formatPower(value) {
        const number = Number(value);
        if (!Number.isFinite(number)) return '—';
        return `${number.toFixed(Math.abs(number) >= 100 ? 1 : 2)} kW`;
    }

    function formatAge(value) {
        const ms = Number(value);
        if (!Number.isFinite(ms) || ms < 0) return 'Unknown';
        if (ms < 60000) return `${Math.max(1, Math.round(ms / 1000))} sec ago`;
        if (ms < 3600000) return `${Math.round(ms / 60000)} min ago`;
        return `${(ms / 3600000).toFixed(1)} hr ago`;
    }

    function ensureAlarmPage() {
        const nav = document.querySelector('.nav-list');
        if (nav && !nav.querySelector('[data-route="alarms"]')) {
            const link = document.createElement('a');
            link.className = 'nav-link';
            link.href = '#/alarms';
            link.dataset.route = 'alarms';
            link.innerHTML = '<span aria-hidden="true">△</span><span>Alarms</span><b class="op-alarm-badge" id="operatorAlarmBadge" hidden>0</b>';
            const system = nav.querySelector('[data-route="system"]');
            nav.insertBefore(link, system || null);
        }
        const main = byId('mainContent');
        if (main && !main.querySelector('[data-page="alarms"]')) {
            const page = node('section', 'page');
            page.dataset.page = 'alarms';
            page.innerHTML = '<div class="operator-product-view" id="operatorAlarmView"></div>';
            main.append(page);
        }
    }

    function setRouteActive() {
        if (!isOperator()) return;
        document.querySelectorAll('.nav-link').forEach((link) => link.classList.toggle('active', link.dataset.route === route()));
        document.querySelectorAll('.page').forEach((page) => page.classList.toggle('active', page.dataset.page === route()));
        if (route() === 'alarms') {
            if (byId('pageTitle')) byId('pageTitle').textContent = 'Alarms';
            if (byId('breadcrumbCurrent')) byId('breadcrumbCurrent').textContent = 'Alarm center';
        }
    }

    function severityIcon(severity) {
        return severity === 'critical' ? '!' : severity === 'warning' ? '△' : 'i';
    }

    function renderAlarmPage() {
        const view = byId('operatorAlarmView');
        if (!view || route() !== 'alarms' || !isOperator()) return;
        const payload = state.events || {};
        const events = Array.isArray(payload.events) ? payload.events : [];
        const summary = payload.summary || {};
        view.replaceChildren();

        const head = node('div', 'op-section-head');
        const copy = node('div');
        copy.append(node('p', 'eyebrow', 'Plant attention'), node('h3', '', 'Alarm and event center'), node('p', '', 'Active conditions, recoveries, and recent controller events in operator language.'));
        const refresh = node('button', 'button secondary', 'Refresh');
        refresh.type = 'button';
        refresh.addEventListener('click', refreshAll);
        head.append(copy, refresh);
        view.append(head);

        const totals = node('div', 'op-three-column');
        totals.append(
            summaryCard('Critical', Number(summary.active_critical) || 0, 'Immediate plant attention', Number(summary.active_critical) ? 'bad' : 'good'),
            summaryCard('Warnings', Number(summary.active_warning) || 0, 'Review when safe', Number(summary.active_warning) ? 'warning' : 'good'),
            summaryCard('Event history', Number(summary.stored_events) || 0, 'Controller-resident events', '')
        );
        view.append(totals);

        const active = events.filter((event) => event.active && event.severity !== 'information');
        const activeCard = node('article', 'op-card');
        activeCard.append(node('div', 'op-card-headline', 'Active conditions'));
        const activeList = node('div', 'op-event-list');
        if (!active.length) activeList.append(node('div', 'op-empty-state good', 'No active critical or warning condition.'));
        active.forEach((event) => activeList.append(eventRow(event)));
        activeCard.append(activeList);
        view.append(activeCard);

        const historyCard = node('article', 'op-card');
        historyCard.append(node('div', 'op-card-headline', 'Recent events'));
        const historyList = node('div', 'op-event-list');
        events.slice(0, 40).forEach((event) => historyList.append(eventRow(event)));
        if (!events.length) historyList.append(node('div', 'op-empty-state', 'Events will appear as controller states change.'));
        historyCard.append(historyList);
        view.append(historyCard);
    }

    function summaryCard(label, value, detail, tone) {
        const card = node('article', `op-kpi ${tone || ''}`);
        card.append(node('span', 'op-kpi-label', label), node('strong', 'op-kpi-value', value), node('small', 'op-kpi-detail', detail));
        return card;
    }

    function eventRow(event) {
        const row = node('article', `op-event-row ${event.severity || 'information'} ${event.active ? 'active' : 'cleared'}`);
        const marker = node('span', 'op-event-marker', severityIcon(event.severity));
        const copy = node('div', 'op-event-copy');
        copy.append(node('strong', '', event.title || 'Controller event'), node('p', '', event.detail || ''), node('small', '', event.recommended_action || ''));
        const meta = node('div', 'op-event-meta');
        meta.append(node('span', `op-state-pill ${event.active ? event.severity === 'critical' ? 'bad' : event.severity === 'warning' ? 'warning' : 'good' : ''}`, event.active ? 'Active' : 'Cleared'), node('small', '', formatAge(event.age_ms)));
        row.append(marker, copy, meta);
        return row;
    }

    function values(key) {
        return (state.history?.samples || []).map((sample) => Number(sample[key])).filter(Number.isFinite);
    }

    function sparkline(series, label) {
        const wrap = node('div', 'op-sparkline op-history-chart');
        if (!series.length) {
            wrap.append(node('span', 'op-empty-inline', 'Controller history is collecting samples'));
            return wrap;
        }
        const width = 420, height = 92, pad = 8;
        const min = Math.min(...series), max = Math.max(...series);
        const span = Math.max(1, max - min);
        const points = series.map((value, index) => {
            const x = series.length === 1 ? width / 2 : pad + index * (width - pad * 2) / (series.length - 1);
            const y = height - pad - ((value - min) / span) * (height - pad * 2);
            return `${x.toFixed(1)},${y.toFixed(1)}`;
        }).join(' ');
        wrap.innerHTML = `<svg viewBox="0 0 ${width} ${height}" role="img" aria-label="${label}"><path class="op-spark-area" d="M ${points.replace(/ /g, ' L ')} L ${width - pad},${height - pad} L ${pad},${height - pad} Z"/><polyline class="op-spark-line" points="${points}"/></svg>`;
        return wrap;
    }

    function rangeSelector() {
        const group = node('div', 'op-range-selector');
        [['15m', '15 min'], ['1h', '1 hour'], ['24h', '24 hours']].forEach(([value, label]) => {
            const button = node('button', `op-range-button ${state.range === value ? 'active' : ''}`, label);
            button.type = 'button';
            button.addEventListener('click', async () => {
                state.range = value;
                await refreshHistory();
                scheduleEnhance();
            });
            group.append(button);
        });
        return group;
    }

    function historyPanel(kind) {
        const key = kind === 'solar' ? 'solar_kw' : 'grid_kw';
        const series = values(key);
        const summary = state.history?.summary || {};
        const prefix = kind === 'solar' ? 'solar' : 'grid';
        const card = node('article', 'op-card op-controller-history');
        const headline = node('div', 'op-history-head');
        headline.append(node('div', 'op-card-headline', `${kind === 'solar' ? 'Solar production' : 'Grid demand'} history`), rangeSelector());
        card.append(headline, sparkline(series, `${kind} history`));
        const stats = node('div', 'op-history-stats');
        stats.append(
            stat('Minimum', formatPower(summary[`${prefix}_min_kw`])),
            stat('Average', formatPower(summary[`${prefix}_average_kw`])),
            stat('Peak', formatPower(summary[`${prefix}_max_kw`]))
        );
        card.append(stats, node('small', 'op-chart-note', `Stored by the controller · ${state.history?.range || state.range} range`));
        return card;
    }

    function stat(label, value) {
        const item = node('div', 'op-history-stat');
        item.append(node('span', '', label), node('strong', '', value));
        return item;
    }

    function enhanceCurrentPage() {
        if (!isOperator()) return;
        const current = route();
        if (current === 'alarms') {
            renderAlarmPage();
            return;
        }
        const target = current === 'dashboard' ? byId('operatorDashboardView') : current === 'meters' ? byId('operatorMeterView') : current === 'inverters' ? byId('operatorInverterView') : null;
        if (!target || target.querySelector('.op-controller-history')) return;
        if (current === 'dashboard') {
            const grid = node('div', 'op-two-column op-history-section');
            grid.append(historyPanel('grid'), historyPanel('solar'));
            target.append(grid);
            const events = (state.events?.events || []).filter((event) => event.active && event.severity !== 'information');
            const attention = node('article', 'op-card op-dashboard-events');
            attention.append(node('div', 'op-card-headline', 'Current attention'));
            const list = node('div', 'op-event-list compact');
            if (!events.length) list.append(node('div', 'op-empty-state good', 'Plant monitoring is clear.'));
            events.slice(0, 3).forEach((event) => list.append(eventRow(event)));
            attention.append(list);
            target.append(attention);
        } else if (current === 'meters') {
            target.append(historyPanel('grid'));
        } else if (current === 'inverters') {
            target.append(historyPanel('solar'));
        }
    }

    function scheduleEnhance() {
        if (state.enhanceQueued) return;
        state.enhanceQueued = true;
        window.requestAnimationFrame(() => {
            state.enhanceQueued = false;
            enhanceCurrentPage();
        });
    }

    async function refreshHistory() {
        state.history = await api(`/api/operator/history?range=${encodeURIComponent(state.range)}`);
    }

    async function refreshAll() {
        if (!isOperator() || state.busy) return;
        state.busy = true;
        try {
            const [history, events] = await Promise.all([
                api(`/api/operator/history?range=${encodeURIComponent(state.range)}`),
                api('/api/operator/events')
            ]);
            state.history = history;
            state.events = events;
            const count = (Number(events.summary?.active_critical) || 0) + (Number(events.summary?.active_warning) || 0);
            const badge = byId('operatorAlarmBadge');
            if (badge) {
                badge.textContent = count;
                badge.hidden = count === 0;
            }
            renderAlarmPage();
            scheduleEnhance();
        } catch (error) {
            console.warn('Operator history/events unavailable:', error);
        } finally {
            state.busy = false;
        }
    }

    function start() {
        ensureAlarmPage();
        setRouteActive();
        refreshAll();
        window.addEventListener('hashchange', () => {
            ensureAlarmPage();
            setRouteActive();
            scheduleEnhance();
            renderAlarmPage();
        });
        new MutationObserver(() => {
            ensureAlarmPage();
            setRouteActive();
            scheduleEnhance();
        }).observe(document.documentElement, { attributes: true, attributeFilter: ['data-access'] });
        const main = byId('mainContent');
        if (main) {
            new MutationObserver((records) => {
                if (records.some((record) => record.target === main)) scheduleEnhance();
            }).observe(main, { childList: true });
        }
        state.timer = window.setInterval(refreshAll, 10000);
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();