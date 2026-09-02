(() => {
    'use strict';

    const REFRESH_MS = 10000;
    const REQUEST_TIMEOUT_MS = 6000;
    const OPERATOR_ROUTES = new Set(['dashboard', 'meters', 'inverters', 'alarms']);
    const state = {
        history: null,
        events: null,
        range: '15m',
        busy: false,
        timer: null,
        controllers: new Set(),
        enhanceQueued: false,
        /* Alarm condition table from /api/operator/alarms. The controller has
         * published this ISA-18.2 state model since the condition-table work
         * landed; nothing displayed it, which is why the external audit called
         * the alarm screen a release blocker (P0-5b). */
        alarms: null,
        alarmError: null,
        alarmMessage: null,
        alarmBusy: false,
        filterState: 'all',
        filterSeverity: 'all',
        sort: 'severity'
    };
    const byId = (id) => document.getElementById(id);
    const isOperator = () => document.documentElement.dataset.access !== 'engineering';
    const route = () => location.hash.replace(/^#\/?/, '') || 'dashboard';
    const historyActive = () => !document.hidden && isOperator() && OPERATOR_ROUTES.has(route());
    const alarmsActive = () => {
        if (document.hidden) return false;
        const current = route();
        return current === 'alarms' || (isOperator() && OPERATOR_ROUTES.has(current));
    };

    /* ------------------------------------------------------------ ISA-18.2
     *
     * These four states are the alarm system's semantics, not labels on a chart.
     * The one that decides whether this screen is worth having is
     * rtn_unacknowledged: a condition that appeared and cleared again while
     * nobody was watching. On an unattended PV-DG site that is the normal shape
     * of a real fault - a generator that stumbled at 03:00 and recovered - and
     * if it is drawn as "resolved" the operator arriving in the morning has no
     * way to learn it happened. It is therefore outstanding work, it is counted
     * as outstanding, and it is never styled as good news.
     *
     * See docs/ALARM_MANAGEMENT_RESEARCH.md gap A1. */
    const ALARM_STATES = {
        unacknowledged: {
            label: 'Unacknowledged',
            tone: 'bad',
            meaning: 'The condition is present now and nobody has accepted responsibility for it.'
        },
        acknowledged: {
            label: 'Acknowledged · still present',
            tone: 'warning',
            meaning: 'Someone has accepted this condition, but it is still present and still counts as active. Acknowledging did not clear it.'
        },
        rtn_unacknowledged: {
            label: 'Returned to normal · never acknowledged',
            tone: 'warning',
            meaning: 'The condition cleared itself before anyone accepted it. It is not resolved work: it stays on this list until someone acknowledges that it happened.'
        },
        normal: {
            label: 'Normal',
            tone: 'good',
            meaning: 'The condition is not present and has been acknowledged. Nothing is outstanding.'
        }
    };

    const SEVERITY_RANK = { critical: 3, warning: 2, information: 1 };

    function alarmState(alarm) {
        return ALARM_STATES[String(alarm && alarm.state)] || ALARM_STATES.unacknowledged;
    }

    /* Outstanding is "nobody has accepted this", not "something is wrong right
     * now". The two differ exactly on rtn_unacknowledged, which is the case the
     * standard exists to protect. */
    function isOutstanding(alarm) {
        return alarm && alarm.acknowledged !== true;
    }

    function isActive(alarm) {
        return Boolean(alarm && alarm.present);
    }

    function node(tag, className = '', text = null) {
        const item = document.createElement(tag);
        if (className) item.className = className;
        if (text != null) item.textContent = String(text);
        return item;
    }

    async function api(path, options = {}) {
        const controller = new AbortController();
        state.controllers.add(controller);
        const timer = window.setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS);
        try {
            const response = await fetch(path, {
                cache: 'no-store',
                credentials: 'same-origin',
                signal: controller.signal,
                ...options
            });
            const text = await response.text();
            let payload = {};
            if (text) {
                try { payload = JSON.parse(text); }
                catch { throw new Error('Controller returned an incomplete response'); }
            }
            if (!response.ok) {
                const error = new Error(payload.message || payload.error || `HTTP ${response.status}`);
                error.status = response.status;
                throw error;
            }
            return payload;
        } catch (error) {
            if (error?.name === 'AbortError') throw new Error('Controller request timed out');
            throw error;
        } finally {
            window.clearTimeout(timer);
            state.controllers.delete(controller);
        }
    }

    function cancelRequests() {
        state.controllers.forEach((controller) => controller.abort());
        state.controllers.clear();
    }

    function cancelTimer() {
        if (state.timer) window.clearTimeout(state.timer);
        state.timer = null;
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

    function formatDuration(value) {
        const ms = Number(value);
        if (!Number.isFinite(ms) || ms < 0) return 'Unknown';
        if (ms < 1000) return 'under a second';
        if (ms < 60000) return `${Math.round(ms / 1000)} sec`;
        if (ms < 3600000) return `${Math.round(ms / 60000)} min`;
        const hours = ms / 3600000;
        return hours < 24 ? `${hours.toFixed(1)} hr` : `${(hours / 24).toFixed(1)} days`;
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
            /* The condition table is deliberately NOT inside .operator-product-view:
             * that class is hidden for engineering access, and acknowledgement is
             * an engineering action. A screen whose only action is invisible to
             * the only role that can perform it is not a screen. */
            page.innerHTML = '<div class="alarm-console" id="alarmConsole"></div>'
                + '<div class="operator-product-view" id="operatorAlarmView"></div>';
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

    /* ----------------------------------------------------- alarm condition table
     *
     * P0-5b. Everything the audit found missing is here: severity, alarm
     * identifier, first and last occurrence, duration, occurrence count,
     * acknowledgement state, filtering, sorting and an acknowledge action.
     */
    function engineeringAuthorized() {
        const access = window.AutomatrixEngineeringAccess;
        return Boolean(access && access.isAuthenticated());
    }

    function filteredAlarms() {
        const list = Array.isArray(state.alarms?.alarms) ? state.alarms.alarms.slice() : [];
        const visible = list.filter((alarm) => {
            if (state.filterSeverity !== 'all' && String(alarm.severity) !== state.filterSeverity) return false;
            if (state.filterState === 'active') return isActive(alarm);
            /* "Outstanding" deliberately keeps returned-to-normal rows that were
             * never acknowledged. Filtering them out with the resolved ones is
             * exactly the mistake this screen exists to prevent. */
            if (state.filterState === 'outstanding') return isOutstanding(alarm);
            return true;
        });
        const rank = (alarm) => SEVERITY_RANK[String(alarm.severity)] || 0;
        const number = (value) => (Number.isFinite(Number(value)) ? Number(value) : Number.MAX_SAFE_INTEGER);
        visible.sort((a, b) => {
            if (state.sort === 'recent') return number(a.last_raised_age_ms) - number(b.last_raised_age_ms);
            if (state.sort === 'first') return number(b.first_raised_age_ms) - number(a.first_raised_age_ms);
            if (state.sort === 'duration') return number(b.duration_ms) - number(a.duration_ms);
            /* Severity order, then outstanding work above accepted work, then
             * most recent first - so the row that needs a decision is at the top. */
            if (rank(b) !== rank(a)) return rank(b) - rank(a);
            if (isOutstanding(a) !== isOutstanding(b)) return isOutstanding(a) ? -1 : 1;
            return number(a.last_raised_age_ms) - number(b.last_raised_age_ms);
        });
        return visible;
    }

    function selectControl(id, label, value, options, onChange) {
        const wrap = node('label', 'alarm-control');
        wrap.htmlFor = id;
        wrap.append(node('span', '', label));
        const select = node('select', 'alarm-select');
        select.id = id;
        options.forEach(([optionValue, optionLabel]) => {
            const option = node('option', '', optionLabel);
            option.value = optionValue;
            select.append(option);
        });
        select.value = value;
        select.addEventListener('change', () => onChange(select.value));
        wrap.append(select);
        return wrap;
    }

    function alarmSummaryTiles(alarms, summary) {
        const outstanding = alarms.filter(isOutstanding).length;
        const active = alarms.filter(isActive).length;
        const returned = alarms.filter((alarm) => String(alarm.state) === 'rtn_unacknowledged').length;
        const tiles = node('div', 'alarm-summary');
        tiles.append(
            summaryCard('Unacknowledged', Number(summary.unacknowledged ?? outstanding) || 0,
                        'Work nobody has accepted yet', outstanding ? 'bad' : 'good'),
            summaryCard('Active conditions', Number(summary.active ?? active) || 0,
                        'Present now, acknowledged or not', active ? 'warning' : 'good'),
            summaryCard('Returned, never acknowledged', returned,
                        'Cleared itself while nobody was watching', returned ? 'warning' : 'good'),
            summaryCard('State model', summary.state_model || 'ISA-18.2',
                        'Ten-state lifecycle; four states are implemented', '')
        );
        return tiles;
    }

    function alarmMetaRow(label, value) {
        const item = node('div', 'alarm-meta-item');
        item.append(node('span', '', label), node('strong', '', value));
        return item;
    }

    function acknowledgeControl(alarm) {
        if (alarm.acknowledged === true) {
            /* The controller has no operator identity model, so the record says
             * an authenticated engineering session did this - never a name.
             * Inventing "acknowledged by <someone>" would put a fact in the
             * incident record that the device never had. Gap A8 in
             * docs/ALARM_MANAGEMENT_RESEARCH.md. */
            return node('small', 'alarm-ack-note',
                `Acknowledged ${formatAge(alarm.acknowledged_age_ms)} by an authenticated engineering session. `
                + 'This controller records no operator identity, so it cannot say who.');
        }
        if (!engineeringAuthorized()) {
            /* No dead button. A control that can only fail is worse than no
             * control: it teaches the operator that the screen does not work. */
            const wrap = node('div', 'alarm-ack-locked');
            wrap.append(node('small', '', 'Acknowledging requires an engineering session.'));
            const link = node('a', 'button secondary alarm-ack-link', 'Sign in to acknowledge');
            link.href = '#/engineering';
            wrap.append(link);
            return wrap;
        }
        const button = node('button', 'button primary alarm-ack-button', 'Acknowledge');
        button.type = 'button';
        button.dataset.alarmCode = String(alarm.code);
        button.addEventListener('click', () => acknowledgeAlarm(alarm));
        return button;
    }

    function alarmRow(alarm) {
        const meta = alarmState(alarm);
        const severity = String(alarm.severity || 'information');
        const returned = String(alarm.state) === 'rtn_unacknowledged';
        const row = node('article',
            `alarm-row severity-${severity} state-${String(alarm.state)}`
            + `${isOutstanding(alarm) ? ' alarm-outstanding' : ''}`
            + `${returned ? ' alarm-returned' : ''}`);

        const marker = node('span', 'alarm-marker', severityIcon(severity));
        marker.setAttribute('aria-hidden', 'true');

        const copy = node('div', 'alarm-copy');
        const heading = node('div', 'alarm-heading');
        heading.append(
            node('code', 'alarm-id', alarm.id || 'GEN-000'),
            node('strong', '', alarm.title || 'Controller condition'),
            node('span', `alarm-severity-pill severity-${severity}`, severity)
        );
        const stateLine = node('div', 'alarm-state-line');
        stateLine.append(
            node('span', `alarm-state-pill tone-${meta.tone}`, meta.label),
            node('small', 'alarm-state-meaning', meta.meaning)
        );
        copy.append(heading, stateLine);
        if (alarm.detail) copy.append(node('p', '', alarm.detail));
        if (alarm.recommended_action) copy.append(node('small', 'alarm-action', `Recommended action: ${alarm.recommended_action}`));

        const metaGrid = node('div', 'alarm-meta');
        metaGrid.append(
            alarmMetaRow('First occurrence', formatAge(alarm.first_raised_age_ms)),
            alarmMetaRow('Last occurrence', formatAge(alarm.last_raised_age_ms)),
            alarmMetaRow('Duration', formatDuration(alarm.duration_ms)),
            alarmMetaRow('Occurrences', Number(alarm.occurrences) || 0),
            alarmMetaRow('Present now', alarm.present ? 'Yes' : 'No'),
            alarmMetaRow('Acknowledged', alarm.acknowledged ? formatAge(alarm.acknowledged_age_ms) : 'No')
        );
        copy.append(metaGrid);

        const actions = node('div', 'alarm-actions');
        actions.append(acknowledgeControl(alarm));
        row.append(marker, copy, actions);
        return row;
    }

    function renderAlarmConsole() {
        const view = byId('alarmConsole');
        if (!view || route() !== 'alarms') return;
        const payload = state.alarms || {};
        const alarms = Array.isArray(payload.alarms) ? payload.alarms : [];
        const summary = payload.summary || {};
        view.replaceChildren();

        const head = node('div', 'op-section-head');
        const copy = node('div');
        copy.append(
            node('p', 'eyebrow', 'Plant attention'),
            node('h3', '', 'Alarm conditions'),
            node('p', '', 'Every condition the controller tracks, with when it started, how long it has stood, how often it has recurred and whether anyone has accepted it.')
        );
        const refresh = node('button', 'button secondary', 'Refresh');
        refresh.type = 'button';
        refresh.addEventListener('click', refreshAlarms);
        head.append(copy, refresh);
        view.append(head);

        /* Stated before the list, not buried in a tooltip: acknowledgement is a
         * record that a human looked, not a repair. Operators who believe
         * otherwise stop investigating. */
        view.append(node('div', 'notice warning alarm-semantics',
            'Acknowledging records that someone has seen a condition. It never clears it — only the plant can do that. '
            + 'An acknowledged condition that is still present stays on this list and still counts as active.'));

        view.append(alarmSummaryTiles(alarms, summary));

        const controls = node('div', 'alarm-controls');
        controls.append(
            selectControl('alarmFilterState', 'Show', state.filterState, [
                ['all', 'All conditions'],
                ['active', 'Active only'],
                ['outstanding', 'Outstanding only (unacknowledged)']
            ], (value) => { state.filterState = value; renderAlarmConsole(); }),
            selectControl('alarmFilterSeverity', 'Severity', state.filterSeverity, [
                ['all', 'All severities'],
                ['critical', 'Critical'],
                ['warning', 'Warning'],
                ['information', 'Information']
            ], (value) => { state.filterSeverity = value; renderAlarmConsole(); }),
            selectControl('alarmSort', 'Sort by', state.sort, [
                ['severity', 'Severity, then outstanding'],
                ['recent', 'Most recent occurrence'],
                ['first', 'Oldest first occurrence'],
                ['duration', 'Longest duration']
            ], (value) => { state.sort = value; renderAlarmConsole(); })
        );
        view.append(controls);

        if (state.alarmMessage) view.append(node('div', 'alarm-message', state.alarmMessage));
        if (state.alarmError) view.append(node('div', 'notice danger alarm-message', state.alarmError));

        const list = node('div', 'alarm-list');
        const visible = filteredAlarms();
        if (!alarms.length) {
            list.append(node('div', 'op-empty-state good',
                'The controller has raised no alarm condition since it started.'));
        } else if (!visible.length) {
            list.append(node('div', 'op-empty-state',
                `${alarms.length} condition(s) are recorded but none match the current filter.`));
        }
        visible.forEach((alarm) => list.append(alarmRow(alarm)));
        view.append(list);
    }

    async function acknowledgeAlarm(alarm) {
        if (state.alarmBusy) return;
        state.alarmBusy = true;
        state.alarmError = null;
        state.alarmMessage = `Acknowledging ${alarm.id || alarm.code}…`;
        renderAlarmConsole();
        try {
            const result = await api('/api/operator/alarms/ack', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ code: Number(alarm.code) })
            });
            /* The controller's own wording is shown verbatim. It is the only
             * thing that knows whether the acknowledgement was recorded, and
             * paraphrasing a safety decision is how interfaces start lying. */
            state.alarmMessage = result.note
                || (result.acknowledged ? 'Condition acknowledged.' : 'The controller did not record an acknowledgement.');
        } catch (error) {
            if (error.status === 401) {
                state.alarmError = 'Acknowledging an alarm requires an authenticated engineering session. '
                    + 'Open Engineering and sign in, then acknowledge again. Nothing was changed.';
            } else {
                state.alarmError = `Acknowledgement failed: ${error.message}. The condition is unchanged.`;
            }
            state.alarmMessage = null;
        } finally {
            state.alarmBusy = false;
        }
        await refreshAlarms();
    }

    async function refreshAlarms() {
        if (!alarmsActive()) return;
        try {
            state.alarms = await api('/api/operator/alarms');
            state.alarmError = state.alarmError && state.alarmError.startsWith('Alarm conditions unavailable')
                ? null : state.alarmError;
        } catch (error) {
            state.alarms = null;
            state.alarmError = `Alarm conditions unavailable: ${error.message}`;
        }
        updateAlarmBadge();
        renderAlarmConsole();
    }

    /* The sidebar badge counts outstanding work, not live conditions: a fault
     * that came and went unnoticed still needs someone to see it. */
    function updateAlarmBadge() {
        const badge = byId('operatorAlarmBadge');
        if (!badge) return;
        const summary = state.alarms?.summary || null;
        const list = Array.isArray(state.alarms?.alarms) ? state.alarms.alarms : [];
        const count = summary && Number.isFinite(Number(summary.unacknowledged))
            ? Number(summary.unacknowledged)
            : list.filter(isOutstanding).length;
        badge.textContent = count;
        badge.hidden = count === 0;
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
        if (!historyActive()) return;
        state.history = await api(`/api/operator/history?range=${encodeURIComponent(state.range)}`);
    }

    async function refreshAll() {
        if (!historyActive() || state.busy) return;
        state.busy = true;
        try {
            const [history, events] = await Promise.all([
                api(`/api/operator/history?range=${encodeURIComponent(state.range)}`),
                api('/api/operator/events')
            ]);
            state.history = history;
            state.events = events;
            /* The navigation badge is driven by the alarm condition table, not
             * by the event ring: the condition table is the one that still
             * counts a fault that cleared itself unacknowledged. */
            renderAlarmPage();
            scheduleEnhance();
        } catch (error) {
            console.warn('Operator history/events unavailable:', error);
        } finally {
            state.busy = false;
        }
    }

    function schedulePoll(delay = REFRESH_MS) {
        cancelTimer();
        if (!historyActive() && !alarmsActive()) return;
        state.timer = window.setTimeout(async () => {
            state.timer = null;
            const tasks = [];
            if (historyActive()) tasks.push(refreshAll());
            if (alarmsActive()) tasks.push(refreshAlarms());
            await Promise.allSettled(tasks);
            schedulePoll();
        }, delay);
    }

    function reconcileLifecycle() {
        cancelTimer();
        cancelRequests();
        if (!historyActive() && !alarmsActive()) return;
        const tasks = [];
        if (historyActive()) tasks.push(refreshAll());
        if (alarmsActive()) tasks.push(refreshAlarms());
        Promise.allSettled(tasks).finally(() => schedulePoll());
    }

    function start() {
        ensureAlarmPage();
        setRouteActive();
        reconcileLifecycle();
        window.addEventListener('hashchange', () => {
            ensureAlarmPage();
            setRouteActive();
            scheduleEnhance();
            renderAlarmPage();
            renderAlarmConsole();
            reconcileLifecycle();
        });
        new MutationObserver(() => {
            ensureAlarmPage();
            setRouteActive();
            scheduleEnhance();
            /* Signing in or out changes whether an acknowledge control can do
             * anything, so the rows are rebuilt rather than left showing a
             * button that would only 401. */
            renderAlarmConsole();
            reconcileLifecycle();
        }).observe(document.documentElement, { attributes: true, attributeFilter: ['data-access'] });
        window.addEventListener('amx-access-change', () => {
            renderAlarmConsole();
            reconcileLifecycle();
        });
        const main = byId('mainContent');
        if (main) {
            new MutationObserver((records) => {
                if (records.some((record) => record.target === main)) scheduleEnhance();
            }).observe(main, { childList: true });
        }
        document.addEventListener('visibilitychange', reconcileLifecycle);
        window.addEventListener('beforeunload', () => {
            cancelTimer();
            cancelRequests();
        });
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();