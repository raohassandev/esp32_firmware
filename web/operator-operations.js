(() => {
    'use strict';

    const state = {
        history: null,
        events: null,
        range: '15m',
        busy: false,
        timer: null,
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
        const timer = window.setTimeout(() => controller.abort(), 6000);
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
            link.setAttribute('aria-label', 'Alarms and events');
            link.innerHTML = '<span aria-hidden="true">△</span><span>Alarms and events</span><b class="op-alarm-badge" id="operatorAlarmBadge" hidden>0</b>';
            const system = nav.querySelector('[data-route="system"]');
            nav.insertBefore(link, system || null);
            /* app.js decides which group this belongs to (Operate) and keeps it
             * there when other modules move it. */
            window.AutomatrixUi?.ensureNavigationHierarchy();
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
        document.querySelectorAll('.page').forEach((page) => page.classList.toggle('active', page.dataset.page === route()));
        /* This page called itself "Alarms" in the title and "Alarm center" in
         * the breadcrumb while the sidebar said something else again. The one
         * durable name now comes from the route table in web/app.js. */
        const ui = window.AutomatrixUi;
        if (ui) { ui.applyRouteChrome(route()); return; }
        document.querySelectorAll('.nav-link').forEach((link) => link.classList.toggle('active', link.dataset.route === route()));
        if (route() === 'alarms') {
            if (byId('pageTitle')) byId('pageTitle').textContent = 'Alarms and events';
            if (byId('breadcrumbCurrent')) byId('breadcrumbCurrent').textContent = 'Alarms and events';
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
        /* Alarm family, not a fifth set of words: an event is Critical, Warning
         * or Normal, and whether it is present now is said separately. */
        const states = window.AutomatrixUi?.STATES?.alarm;
        const severityWord = event.severity === 'critical' ? (states?.critical || 'Critical')
            : event.severity === 'warning' ? (states?.warning || 'Warning')
            : (states?.normal || 'Normal');
        meta.append(
            node('span', `op-state-pill ${event.active ? event.severity === 'critical' ? 'bad' : event.severity === 'warning' ? 'warning' : 'good' : ''}`,
                event.active ? severityWord : (states?.normal || 'Normal')),
            node('small', '', event.active ? 'Present now' : 'Returned to normal'),
            node('small', '', formatAge(event.age_ms)));
        row.append(marker, copy, meta);
        return row;
    }

    /* ============================================================== trends
     *
     * The previous visual was a bare polyline: no axes, no units, no range, no
     * scale. It could not answer "how much" or "when", which makes it decoration
     * rather than a diagnostic, and it joined a straight line straight across
     * missing samples. On a control system that last part is the serious one -
     * interpolating across a gap draws a measurement that was never taken, in
     * exactly the window where the plant was least observed.
     *
     * What is drawn here comes only from GET /api/operator/history. The
     * firmware supports 15m, 1h and 24h and silently substitutes 15m for
     * anything else (operational_api.c history_get), so only those three are
     * offered - a 6h button would quietly return 15 minutes of data under a
     * 6-hour label.
     *
     * A null grid_kw or solar_kw is the controller saying it had no valid
     * sample. It breaks the line and is marked in the quality strip. So does a
     * hole in time: if two consecutive samples are further apart than the
     * declared sample_interval_ms allows, the controller was not sampling, and
     * the plot says so instead of bridging it. */
    const RANGE_WINDOW_MS = { '15m': 900000, '1h': 3600000, '24h': 86400000 };
    const RANGE_LABELS = [['15m', '15 min'], ['1h', '1 hour'], ['24h', '24 hours']];
    const SVG_NS = 'http://www.w3.org/2000/svg';

    function svg(tag, attributes = {}) {
        const element = document.createElementNS(SVG_NS, tag);
        Object.entries(attributes).forEach(([name, value]) => element.setAttribute(name, String(value)));
        return element;
    }

    /* Samples in controller order (oldest first), each carrying its own age,
     * value and whether the source was communicating when it was taken. */
    function trendSeries(kind) {
        const key = kind === 'solar' ? 'solar_kw' : 'grid_kw';
        const samples = Array.isArray(state.history?.samples) ? state.history.samples : [];
        const interval = Number(state.history?.sample_interval_ms) || 0;
        const points = samples.map((sample) => {
            const raw = sample[key];
            const value = raw == null ? null : Number(raw);
            const source = kind === 'solar'
                ? Number(sample.inverter_online) > 0
                : sample.meter_online === true;
            return {
                ageMs: Number(sample.age_ms),
                value: Number.isFinite(value) ? value : null,
                communicating: Boolean(source)
            };
        }).filter((point) => Number.isFinite(point.ageMs));
        /* An interval-sized hole means samples the controller never took. */
        points.forEach((point, index) => {
            const previous = points[index - 1];
            point.gapBefore = Boolean(previous) && interval > 0
                && (previous.ageMs - point.ageMs) > interval * 1.8;
        });
        return { points, interval };
    }

    /* A readable Y scale that always includes zero, so import and export are
     * read against the same reference on every refresh. */
    function niceAxis(min, max) {
        if (!Number.isFinite(min) || !Number.isFinite(max)) return { lo: 0, hi: 1, ticks: [0, 1] };
        let lo = Math.min(0, min);
        let hi = Math.max(0, max);
        if (hi - lo < 0.5) hi = lo + 0.5;
        const rough = (hi - lo) / 4;
        const magnitude = Math.pow(10, Math.floor(Math.log10(rough)));
        const normalized = rough / magnitude;
        const step = magnitude * (normalized > 5 ? 10 : normalized > 2 ? 5 : normalized > 1 ? 2 : 1);
        lo = Math.floor(lo / step) * step;
        hi = Math.ceil(hi / step) * step;
        const ticks = [];
        for (let value = lo; value <= hi + step / 2; value += step) ticks.push(Number(value.toFixed(6)));
        return { lo, hi, ticks };
    }

    function timeTickLabel(agoMs) {
        if (agoMs <= 0) return 'now';
        if (agoMs < 3600000) return `-${Math.round(agoMs / 60000)} min`;
        return `-${Math.round(agoMs / 3600000)} h`;
    }

    /* The chart. Axes, units, an explicit visible range, real gaps and a
     * per-sample quality strip - the five things the audit found missing. */
    function trendChart(kind, unit, label) {
        const { points, interval } = trendSeries(kind);
        const wrap = node('div', 'op-history-chart');
        const finiteValues = points.filter((point) => point.value != null).map((point) => point.value);
        if (!finiteValues.length) {
            wrap.append(node('div', 'op-empty-state',
                'The controller has no valid sample in this window. History is held in RAM and starts empty after a restart.'));
            return { chart: wrap, values: finiteValues, points };
        }

        const windowMs = RANGE_WINDOW_MS[state.history?.range || state.range] || RANGE_WINDOW_MS['15m'];
        const axis = niceAxis(Math.min(...finiteValues), Math.max(...finiteValues));
        const W = 640, H = 220;
        const left = 58, right = W - 14, top = 14, bottom = 150;
        const qualityTop = 160, qualityH = 10;
        const plotW = right - left, plotH = bottom - top;

        const x = (ageMs) => right - Math.min(1, Math.max(0, ageMs / windowMs)) * plotW;
        const y = (value) => bottom - ((value - axis.lo) / (axis.hi - axis.lo)) * plotH;

        const figure = svg('svg', {
            viewBox: `0 0 ${W} ${H}`, class: 'op-trend-svg', role: 'img',
            'aria-label': `${label}. Vertical axis ${unit}, ${axis.lo} to ${axis.hi}. Horizontal axis time, last ${timeTickLabel(windowMs).replace('-', '')}.`
        });

        /* Y axis: gridlines, tick values and the unit named once. */
        axis.ticks.forEach((tick) => {
            const ty = y(tick);
            figure.append(svg('line', { class: 'op-trend-gridline', x1: left, x2: right, y1: ty, y2: ty }));
            const text = svg('text', { class: 'op-trend-tick', x: left - 8, y: ty + 4, 'text-anchor': 'end' });
            text.textContent = tick.toFixed(Math.abs(tick) >= 10 ? 0 : 1);
            figure.append(text);
        });
        const unitLabel = svg('text', { class: 'op-trend-axis-title', x: left - 8, y: top - 3, 'text-anchor': 'end' });
        unitLabel.textContent = unit;
        figure.append(unitLabel);
        figure.append(svg('line', { class: 'op-trend-axis', x1: left, x2: left, y1: top, y2: bottom }));
        figure.append(svg('line', { class: 'op-trend-axis', x1: left, x2: right, y1: bottom, y2: bottom }));

        /* X axis: real elapsed time, four ticks. */
        [1, 2 / 3, 1 / 3, 0].forEach((fraction) => {
            const ageMs = windowMs * fraction;
            const tx = x(ageMs);
            const text = svg('text', { class: 'op-trend-tick', x: tx, y: bottom + 18, 'text-anchor': fraction === 1 ? 'start' : fraction === 0 ? 'end' : 'middle' });
            text.textContent = timeTickLabel(ageMs);
            figure.append(text);
        });

        /* The line, broken wherever the controller had no measurement. Each
         * unbroken run is its own path; nothing is drawn across a gap. */
        let run = [];
        const flush = () => {
            if (run.length === 1) {
                figure.append(svg('circle', { class: 'op-trend-point', cx: run[0][0], cy: run[0][1], r: 2.2 }));
            } else if (run.length > 1) {
                figure.append(svg('path', {
                    class: 'op-trend-line',
                    d: run.map((p, i) => `${i ? 'L' : 'M'} ${p[0].toFixed(1)} ${p[1].toFixed(1)}`).join(' ')
                }));
            }
            run = [];
        };
        points.forEach((point) => {
            if (point.value == null || point.gapBefore) flush();
            if (point.value == null) return;
            run.push([x(point.ageMs), y(point.value)]);
        });
        flush();

        /* Data quality, one cell per sample: measured, no valid sample, or a
         * hole where the controller took no sample at all. */
        const cellW = Math.max(1.5, plotW / Math.max(points.length, 1));
        points.forEach((point) => {
            const quality = point.value == null ? 'unavailable' : point.communicating ? 'good' : 'invalid';
            figure.append(svg('rect', {
                class: `op-trend-quality op-trend-quality-${quality}`,
                x: Math.max(left, x(point.ageMs) - cellW / 2), y: qualityTop,
                width: cellW, height: qualityH
            }));
            if (point.gapBefore) {
                figure.append(svg('rect', {
                    class: 'op-trend-quality op-trend-quality-gap',
                    x: Math.max(left, x(point.ageMs) - cellW * 2), y: qualityTop,
                    width: cellW * 1.5, height: qualityH
                }));
            }
        });
        const stripLabel = svg('text', { class: 'op-trend-tick', x: left - 8, y: qualityTop + qualityH, 'text-anchor': 'end' });
        stripLabel.textContent = 'Data';
        figure.append(stripLabel);

        wrap.append(figure);
        return { chart: wrap, values: finiteValues, points, axis };
    }

    function trendLegend(points) {
        const gaps = points.filter((point) => point.value == null || point.gapBefore).length;
        const legend = node('div', 'op-trend-legend');
        [['good', 'Good'], ['invalid', 'Invalid'], ['unavailable', 'Unavailable'], ['gap', 'No sample taken']]
            .forEach(([key, text]) => {
                const item = node('span', 'op-trend-legend-item');
                item.append(node('i', `op-trend-swatch op-trend-quality-${key}`), node('small', '', text));
                legend.append(item);
            });
        legend.append(node('small', 'op-trend-gap-count', gaps
            ? `${gaps} sample interval(s) have no measurement. The line is broken there rather than drawn across.`
            : 'No missing samples in this window.'));
        return legend;
    }

    function rangeSelector() {
        const group = node('div', 'op-range-selector');
        group.setAttribute('role', 'group');
        group.setAttribute('aria-label', 'History range');
        RANGE_LABELS.forEach(([value, label]) => {
            const button = node('button', `op-range-button ${state.range === value ? 'active' : ''}`, label);
            button.type = 'button';
            button.setAttribute('aria-pressed', state.range === value ? 'true' : 'false');
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
        const summary = state.history?.summary || {};
        const prefix = kind === 'solar' ? 'solar' : 'grid';
        const title = kind === 'solar' ? 'Solar production' : 'Grid demand';
        const card = node('article', `op-card op-controller-history op-trend-${prefix}`);
        const headline = node('div', 'op-history-head');
        headline.append(node('div', 'op-card-headline', `${title} history`), rangeSelector());
        card.append(headline);

        const rendered = trendChart(kind, 'kW', `${title} over the selected range`);
        card.append(rendered.chart);

        /* The visible scale, stated. A chart whose axis silently rescales
         * between refreshes is worse than no chart. */
        if (rendered.axis) {
            card.append(node('small', 'op-chart-scale',
                `Visible range ${rendered.axis.lo.toFixed(1)} to ${rendered.axis.hi.toFixed(1)} kW · `
                + `${state.history?.range || state.range} window · sample every `
                + `${Math.round((Number(state.history?.sample_interval_ms) || 0) / 1000)} s`));
        }

        const current = rendered.values.length ? rendered.values[rendered.values.length - 1] : NaN;
        const stats = node('div', 'op-history-stats');
        stats.append(
            stat('Current', formatPower(current)),
            stat('Minimum', formatPower(summary[`${prefix}_min_kw`])),
            stat('Average', formatPower(summary[`${prefix}_average_kw`])),
            stat('Peak', formatPower(summary[`${prefix}_max_kw`]))
        );
        card.append(stats);
        card.append(trendLegend(rendered.points));
        /* Said where a user would otherwise assume otherwise: this is not a
         * historian. A restart loses every point on this chart. */
        card.append(node('small', 'op-chart-note',
            'Held in controller RAM only. Restarting the controller erases this history; it is not written to flash and is not archived.'));
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

    function start() {
        ensureAlarmPage();
        setRouteActive();
        refreshAll();
        refreshAlarms();
        window.addEventListener('hashchange', () => {
            ensureAlarmPage();
            setRouteActive();
            scheduleEnhance();
            renderAlarmPage();
            renderAlarmConsole();
        });
        new MutationObserver(() => {
            ensureAlarmPage();
            setRouteActive();
            scheduleEnhance();
            /* Signing in or out changes whether an acknowledge control can do
             * anything, so the rows are rebuilt rather than left showing a
             * button that would only 401. */
            renderAlarmConsole();
        }).observe(document.documentElement, { attributes: true, attributeFilter: ['data-access'] });
        window.addEventListener('amx-access-change', renderAlarmConsole);
        const main = byId('mainContent');
        if (main) {
            new MutationObserver((records) => {
                if (records.some((record) => record.target === main)) scheduleEnhance();
            }).observe(main, { childList: true });
        }
        state.timer = window.setInterval(() => {
            refreshAll();
            refreshAlarms();
        }, 10000);
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();