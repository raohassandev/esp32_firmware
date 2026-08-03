(() => {
    'use strict';

    const state = {
        history: null,
        historyAt: 0,
        historyError: null,
        /* One chart instance for the whole product. It is created once, moved
         * between the pages that show it, and reconfigured - never rebuilt per
         * page and never duplicated per series. */
        chart: null,
        chartPage: null,
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
        /* A9. Suppressed alarms are never hidden from this screen, so the filter
         * exists to let someone REVIEW them - "show me everything that is
         * currently quiet, and who decided" - which is the routine that keeps a
         * suppression list from turning into a graveyard. */
        filterSuppression: 'all',
        sort: 'severity',
        /* Chosen per row before the action is taken; the controller requires both
         * and will reject a request that omits either. */
        shelfDuration: 3600000,
        outOfServiceReason: 0
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

    /* ------------------------------------------------- ISA-18.2 suppression (A9)
     *
     * Three states, and the standard is explicit that they must not be drawn as
     * one "disabled" badge, because that is what destroys the audit trail. What
     * separates them is who decided and what ends them, so both are on the screen
     * for every suppressed alarm rather than in a tooltip:
     *
     *   shelved              an operator asked for quiet. Time-limited; it ends
     *                        by itself, and the screen shows how long is left.
     *   suppressed_by_design the controller decided, because another live fault
     *                        already explains this one. Nobody can lift it while
     *                        that fault stands - and the screen says so, so an
     *                        operator does not hunt for a button that must not
     *                        exist.
     *   out_of_service       a maintenance action, authorised, with a recorded
     *                        reason. This one does NOT expire, which is exactly
     *                        why it is drawn as the most prominent of the three
     *                        and why the reason is always shown next to it.
     */
    const SUPPRESSION_STATES = {
        none: {
            label: 'Not suppressed',
            tone: 'good',
            meaning: 'This condition is counted in the triage figures.'
        },
        shelved: {
            label: 'Shelved by an operator',
            tone: 'warning',
            meaning: 'An operator asked not to be pressed by this for a fixed time. It expires by itself and comes back into the triage counts.'
        },
        suppressed_by_design: {
            label: 'Suppressed by design',
            tone: 'warning',
            meaning: 'The controller suppressed this because another live fault already explains it. It is released automatically when that fault clears, and it cannot be lifted by hand while the cause stands.'
        },
        out_of_service: {
            label: 'Out of service',
            tone: 'bad',
            meaning: 'Taken out of service as a maintenance action, with a recorded reason. This does not expire: it stays quiet until somebody returns it to service.'
        }
    };

    /* One shift is the outer bound the controller enforces. Nothing shorter than a
     * minute is offered because a shelf that short is a mis-click, not a
     * decision. */
    const SHELF_DURATIONS = [
        [900000, '15 minutes'],
        [3600000, '1 hour'],
        [14400000, '4 hours'],
        [28800000, '8 hours (one shift)']
    ];

    function node(tag, className = '', text = null) {
        const item = document.createElement(tag);
        if (className) item.className = className;
        if (text != null) item.textContent = String(text);
        return item;
    }

    /* ------------------------------------------------- three presentation levels
     *
     * The same disclosure control web/operator-view.js uses, for the same reason.
     * The first screen of an alarm list has one job: triage. What is wrong, how
     * bad, how old, what do I do. The alarm standard's lifecycle, the EEMUA
     * performance metrics and the per-condition history are all real and all
     * kept - one level down, closed, labelled.
     *
     * It is presentation, not permission. Every action still passes the
     * controller's own gate: acknowledgement is open to an operator because
     * ISA-18.2 assigns it to them, and shelving is refused without an
     * engineering session whether or not this file draws a control for it. */
    function details(level, summaryText, ...children) {
        const wrap = node('details', `op-more level-${level}`);
        const head = node('summary', 'op-more-summary');
        head.append(node('span', 'op-more-level', level === 'service' ? 'Service' : 'Engineering'),
            node('span', '', summaryText));
        wrap.append(head, ...children.filter(Boolean));
        return wrap;
    }

    /* The same drawer without the level BADGE - said once for the section
     * instead of once per row.
     *
     * The badge is right and it stays; what was wrong was the arithmetic. One
     * badge per drawer, two drawers per row, four rows on the screen is eight
     * identical ENGINEERING pills in one list, and a label repeated eight times
     * is a label nobody reads by the third. The level survives in three places
     * that cost no height: the level- class the stylesheet colours the drawer
     * with, the accessible name, and the tooltip.
     *
     * This is still presentation and never permission. The controller refuses
     * shelving and out-of-service without an engineering session whether or not
     * a badge was drawn next to the control. */
    function levelledDetails(level, summaryText, ...children) {
        const word = level === 'service' ? 'Service' : 'Engineering';
        const wrap = node('details', `op-more op-more-quiet level-${level}`);
        const head = node('summary', 'op-more-summary');
        head.append(node('span', '', summaryText));
        head.title = `${word}: ${summaryText}`;
        head.setAttribute('aria-label', `${word}. ${summaryText}`);
        wrap.append(head, ...children.filter(Boolean));
        return wrap;
    }

    /* The one statement of the level for a whole section, carrying the badge the
     * rows no longer repeat. */
    function levelNote(level, text) {
        const word = level === 'service' ? 'Service' : 'Engineering';
        const note = node('div', `op-level-note level-${level}`);
        note.append(node('span', 'op-more-level', word), node('small', '', text));
        return note;
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

    /* Used only until the controller's own list arrives, so the picker is never
     * empty on first paint. The controller validates the value regardless. */
    const OUT_OF_SERVICE_FALLBACK = [
        { reason: 0, name: 'field_device_maintenance', text: 'Field device maintenance' },
        { reason: 1, name: 'field_device_replacement', text: 'Field device replacement' },
        { reason: 2, name: 'site_commissioning_work', text: 'Site commissioning work' },
        { reason: 3, name: 'awaiting_repair', text: 'Awaiting repair' },
        { reason: 4, name: 'plant_change_pending_rationalisation', text: 'Plant change pending rationalisation' }
    ];

    /* Below this many conditions, reading the list beats filtering it. */
    const FILTER_THRESHOLD = 6;

    /* ------------------------------------------- the plant overview attention band
     *
     * Rendered into the mount point web/operator-view.js puts at the TOP of the
     * plant overview. It used to be a card appended after the charts, at the
     * bottom of the longest page in the product.
     *
     * Three beats per exception, in the order an operator needs them: what is
     * true now, why that matters, what to do about it. The controller writes all
     * three - title, detail and recommended_action - so none of it is this
     * screen's opinion about a safety condition. */
    function renderAttention() {
        const host = byId('operatorAttentionHost');
        if (!host || !isOperator()) return;
        const events = (state.events?.events || [])
            .filter((event) => event.active && event.severity !== 'information');
        host.replaceChildren();
        if (!events.length) return;   /* Normal gets no paragraph explaining normal. */
        const card = node('article', 'op-card op-dashboard-events');
        card.append(node('div', 'op-card-headline', 'Needs attention'));
        const list = node('div', 'op-event-list compact');
        events.slice(0, 3).forEach((event) => list.append(eventRow(event)));
        card.append(list);
        host.append(card);
    }

    function summaryCard(label, value, detail, tone) {
        const card = node('article', `op-kpi ${tone || ''}`);
        card.append(node('span', 'op-kpi-label', label), node('strong', 'op-kpi-value', value), node('small', 'op-kpi-detail', detail));
        return card;
    }

    /* Current condition / why it matters / required action. A condition that has
     * already returned to normal gets the first beat and the age only: telling
     * an operator what to do about something that is no longer happening is the
     * noise that teaches them to skim the ones that are. */
    function eventRow(event) {
        const row = node('article', `op-event-row ${event.severity || 'information'} ${event.active ? 'active' : 'cleared'}`);
        const marker = node('span', 'op-event-marker', severityIcon(event.severity));
        const copy = node('div', 'op-event-copy');
        copy.append(node('strong', '', event.title || 'Controller event'));
        if (event.active) {
            if (event.detail) copy.append(node('p', '', event.detail));
            if (event.recommended_action) copy.append(node('small', 'op-event-action', event.recommended_action));
        }
        const meta = node('div', 'op-event-meta');
        /* Alarm family, not a fifth set of words: an event is Critical, Warning
         * or Normal, and whether it is present now is said separately. */
        const states = window.AutomatrixUi?.STATES?.alarm;
        const severityWord = event.severity === 'critical' ? (states?.critical || 'Critical')
            : event.severity === 'warning' ? (states?.warning || 'Warning')
            : (states?.normal || 'Normal');
        /*
         * AN EVENT IS A MOMENT, NOT A STATE.
         *
         * `active` on an entry in this ring means the entry RECORDS A RAISE. It
         * was rendered as "Present now", so a condition that raised four minutes
         * ago and cleared three minutes ago was still described as present --
         * while the console at the top of the same page correctly reported zero
         * active conditions. Two blocks on one screen, disagreeing about whether
         * anything is wrong right now, and the wrong one is the one written in
         * the present tense.
         *
         * Whether a condition is present NOW is the condition table's answer.
         * This list says what happened and when.
         */
        meta.append(
            node('span', `op-state-pill ${event.active ? event.severity === 'critical' ? 'bad' : event.severity === 'warning' ? 'warning' : 'good' : ''}`,
                event.active ? severityWord : (states?.normal || 'Normal')),
            node('small', '', event.active ? 'Raised' : 'Returned to normal'),
            node('small', '', formatAge(event.age_ms)));
        row.append(marker, copy, meta);
        return row;
    }

    /* ------------------------------------------------------- the one chart
     *
     * Two chart implementations used to draw this data: a browser-session
     * sparkline in operator-view.js and the controller-history sparkline that
     * used to live here. Both appeared on the dashboard at once - four charts,
     * two different windows over the same quantity, neither with a time axis.
     *
     * Both also compacted the samples down to the finite ones before
     * drawing, which deleted the missing readings instead of showing them, so a
     * minute in which the meter said nothing was drawn as a straight line
     * between the readings either side of it. On a reverse-power controller
     * that is a safety-relevant lie. web/pvdg-chart.js is now the only chart,
     * and it breaks the line at a gap.
     *
     * The controller offers exactly three ranges and silently substitutes 15m
     * for anything else, so only those three values are ever requested. */
    const CHART_PAGES = {
        dashboard: {
            title: 'Plant power trend',
            description: 'Metered supply and solar production stored by the controller',
            series: [
                { key: 'grid_kw', label: 'Metered supply', meaning: { positive: 'supplying the plant', negative: 'flowing back to the supply' } },
                { key: 'solar_kw', label: 'Solar production', meaning: { positive: 'producing', negative: 'consuming' } }
            ]
        },
        /*
         * NO CHART ON THE SUPPLY PAGES.
         *
         * Removed at the owner's request. It plotted grid_kw, which on this
         * product is whichever supply was live at each sample, so on a
         * single-meter plant one line crossed between the utility and the
         * generator with nothing on the axis saying where -- and the Grid and
         * Generator pages now exist precisely to keep those two apart. A trend
         * that mixes them contradicts the page it sits on.
         *
         * The same quantity over time is still on the plant overview, where the
         * line is not making a claim about one supply.
         *
         * The solar trend is gone too, at the owner's request. The plant
         * overview is now the ONLY page with a chart, and it is the one page
         * whose subject is the whole site rather than one device.
         */
    };

    function chart() {
        if (!state.chart && window.PvdgChart && typeof window.PvdgChart.create === 'function') {
            state.chart = window.PvdgChart.create({
                height: 500,
                range: state.range,
                series: CHART_PAGES.dashboard.series,
                onRangeChange: (value) => {
                    state.range = window.PvdgChart.normalizeRange(value);
                    refreshHistory();
                }
            });
        }
        return state.chart;
    }

    /* The measurement bars above the chart quote the same window the chart is
     * drawing, so they have to be told when that window changes - a range
     * button, a poll, or a history error that invalidates the figures. This
     * fires only on history arriving, never on a render, so it cannot loop with
     * the operator view's own rebuild. */
    function announceHistory() {
        window.dispatchEvent(new CustomEvent('amx-operator-history', {
            detail: { range: state.range, ok: !state.historyError }
        }));
    }

    function applyHistory() {
        const instance = state.chart;
        if (!instance) return;
        if (state.historyError) {
            instance.setState('error', state.historyError);
            return;
        }
        if (!state.history) {
            instance.setState('loading');
            return;
        }
        instance.setData({
            samples: state.history.samples,
            sample_interval_ms: state.history.sample_interval_ms,
            range: state.history.range,
            receivedAt: state.historyAt
        });
    }

    function mountChart(target, page) {
        const spec = CHART_PAGES[page];
        if (!target || !spec) return;
        /* The mount point belongs to operator-view.js, which rebuilds its page
         * every refresh but keeps this one node. Without it there is nowhere to
         * put the chart, and appending it to the page body would only have it
         * wiped by the next rebuild. */
        const host = target.querySelector('#operatorTrendHost');
        if (!host) return;
        const instance = chart();
        if (!instance) {
            if (!host.querySelector('.op-empty-state')) {
                host.append(node('div', 'op-empty-state', 'Trend charting is unavailable in this build.'));
            }
            return;
        }
        if (state.chartPage !== page) {
            state.chartPage = page;
            instance.setTitle(spec.title, spec.description);
            instance.setSeries(spec.series);
        }
        if (instance.element.parentElement !== host) host.append(instance.element);
        applyHistory();
    }

    function enhanceCurrentPage() {
        if (!isOperator()) return;
        const current = route();
        /* The plant overview is the only page with a chart. See CHART_PAGES. */
        const target = current === 'dashboard' ? byId('operatorDashboardView') : null;
        if (!target) return;
        mountChart(target, current);
        /* The band is re-rendered rather than appended once: operator-view.js
         * keeps the mount point across its rebuilds, so this is the only thing
         * that keeps its contents current. */
        if (current === 'dashboard') renderAttention();
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
        try {
            const payload = await api(`/api/operator/history?range=${encodeURIComponent(state.range)}`);
            state.history = payload;
            state.historyAt = Date.now();
            state.historyError = null;
        } catch (error) {
            state.historyError = error?.message || 'Controller history is unavailable.';
        }
        applyHistory();
        announceHistory();
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
            state.historyAt = Date.now();
            state.historyError = null;
            state.events = events;
            scheduleEnhance();
            announceHistory();
        } catch (error) {
            state.historyError = error?.message || 'Controller history is unavailable.';
            applyHistory();
            announceHistory();
            console.warn('Operator history/events unavailable:', error);
        } finally {
            state.busy = false;
        }
    }

    function start() {
        setRouteActive();
        refreshAll();
        window.addEventListener('hashchange', () => {
            setRouteActive();
            scheduleEnhance();
        });
        new MutationObserver(() => {
            setRouteActive();
            scheduleEnhance();
        }).observe(document.documentElement, { attributes: true, attributeFilter: ['data-access'] });
        window.addEventListener('amx-operator-view-rendered', scheduleEnhance);
        const main = byId('mainContent');
        if (main) {
            new MutationObserver((records) => {
                if (records.some((record) => record.target === main)) scheduleEnhance();
            }).observe(main, { childList: true });
        }
        state.timer = window.setInterval(refreshAll, 10000);
    }

    /* ------------------------------------------------- shared range statistics
     *
     * This module already holds the controller's history, and web/pvdg-chart.js
     * already knows how to turn it into honest statistics - measured samples
     * only, an unmeasured sample counted as missing rather than as zero. The
     * measurement bars on the grid-power and inverter pages need exactly those
     * numbers over exactly the window the chart below them is drawing, so they
     * read them from here instead of computing a second, quietly different set.
     *
     * Nothing is recomputed and no chart internals are reimplemented: this is a
     * read-only view over state.history using PvdgChart's own pure functions.
     * If the history has not arrived, or the range contains no measured sample,
     * `available` is false and the caller must draw nothing rather than zero. */
    function rangeStats(key) {
        const chart = window.PvdgChart;
        const info = chart && typeof chart.rangeInfo === 'function' ? chart.rangeInfo(state.range) : null;
        const label = info ? info.label : String(state.range);
        if (!chart || typeof chart.stats !== 'function' || !state.history) {
            return { available: false, rangeLabel: label, stats: null };
        }
        const points = chart.toPoints(state.history.samples, key, { now: state.historyAt || Date.now() });
        const figures = chart.stats(points);
        return {
            /* count is the number of MEASURED samples. Zero of them means this
             * window has nothing to say, which is not the same as a flat zero. */
            available: figures.count > 0,
            rangeLabel: label,
            stats: figures
        };
    }

    window.AutomatrixOperations = Object.freeze({ rangeStats });

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();