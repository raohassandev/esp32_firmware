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

    function suppressionState(alarm) {
        const key = String(alarm && alarm.suppression);
        return SUPPRESSION_STATES[key] || SUPPRESSION_STATES.none;
    }

    function isSuppressed(alarm) {
        return Boolean(alarm) && String(alarm.suppression || 'none') !== 'none';
    }

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
            /* A9: a review filter, not a hiding filter. 'all' shows suppressed
             * rows exactly as it shows every other row - suppression removes an
             * alarm from the triage counts and never from this list. */
            if (state.filterSuppression === 'suppressed' && !isSuppressed(alarm)) return false;
            if (state.filterSuppression === 'unsuppressed' && isSuppressed(alarm)) return false;
            if (state.filterSuppression !== 'all' && state.filterSuppression !== 'suppressed'
                && state.filterSuppression !== 'unsuppressed'
                && String(alarm.suppression) !== state.filterSuppression) return false;
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

    /* Counts first, because triage starts with "how much is there". The fourth
     * tile used to announce the state model and its ten-state lifecycle, which
     * teaches the alarm standard to someone who came here to find out what is
     * wrong with the plant. It is in the lifecycle drawer below. */
    function alarmSummaryTiles(alarms, summary) {
        const outstanding = alarms.filter(isOutstanding).length;
        const active = alarms.filter(isActive).length;
        const returned = alarms.filter((alarm) => String(alarm.state) === 'rtn_unacknowledged').length;
        const tiles = node('div', 'alarm-summary');
        tiles.append(
            summaryCard('Unacknowledged', Number(summary.unacknowledged ?? outstanding) || 0,
                        'Nobody has accepted these', outstanding ? 'bad' : 'good'),
            summaryCard('Active conditions', Number(summary.active ?? active) || 0,
                        'Present now', active ? 'warning' : 'good'),
            /* Never collapsed into the two tiles above and never styled as good
             * news: on an unattended site this is the ordinary shape of a real
             * fault, and it is the reason this screen exists. */
            summaryCard('Returned, never acknowledged', returned,
                        'Cleared while nobody was watching', returned ? 'warning' : 'good')
        );
        return tiles;
    }

    /* ------------------------------------------------- A10: EEMUA rate metrics
     *
     * EEMUA 191 gives two numbers a control room can be held to - fewer than one
     * alarm per operator per ten minutes in steady state, and no more than ten in
     * the first ten minutes of an upset - and until they are on a screen there is
     * no evidence this alarm system is usable, only a claim.
     *
     * Two honesty rules are enforced here rather than in the styling. A verdict is
     * only shown once the window it is measured over has actually elapsed; before
     * that the tile says so instead of extrapolating from four minutes of uptime.
     * And the peak is never reported as a pass, only as "no breach observed",
     * because a flood that has not happened yet cannot be disproved by waiting.
     */
    function formatRatePerWindow(milli) {
        const value = Number(milli);
        if (!Number.isFinite(value)) return '—';
        return `${(value / 1000).toFixed(2)} / 10 min`;
    }

    function alarmRateSection(rate) {
        const section = node('section', 'alarm-metrics');
        const head = node('div', 'alarm-metrics-head');
        head.append(
            node('p', 'eyebrow', 'EEMUA 191 · measured'),
            node('h4', '', 'Alarm rate')
        );
        section.append(head);
        if (!rate || typeof rate !== 'object') {
            section.append(node('p', 'alarm-metrics-note',
                'The controller did not report alarm-rate metrics.'));
            return section;
        }
        const steadyObserved = rate.steady_window_observed === true;
        const meetsSteady = rate.meets_steady_target === true;
        const peakBreached = rate.peak_target_breached === true;
        const tiles = node('div', 'alarm-metrics-grid');
        tiles.append(
            metricTile('Last 10 minutes', `${Number(rate.last_10_min) || 0} alarm(s)`,
                       'EEMUA measures both of its targets over ten minutes.', ''),
            metricTile('Steady-state rate',
                       steadyObserved ? formatRatePerWindow(rate.per_10_min_from_60_min_milli)
                                      : 'Not yet measured',
                       steadyObserved
                           ? `Target: under ${formatRatePerWindow(rate.steady_limit_milli)}. ${meetsSteady ? 'Met.' : 'Not met.'}`
                           : 'The controller has not been running for a full hour, so this would be an extrapolation rather than a measurement.',
                       steadyObserved ? (meetsSteady ? 'good' : 'bad') : ''),
            metricTile('Worst 10 minutes seen',
                       `${Number(rate.peak_per_10_min) || 0} alarm(s)`,
                       `Ceiling: ${Number(rate.peak_limit) || 10} in the first ten minutes of an upset. `
                       + (peakBreached ? 'Exceeded.' : 'No breach observed.'),
                       peakBreached ? 'bad' : ''),
            metricTile('Last 24 hours', `${Number(rate.last_24_h) || 0} alarm(s)`,
                       rate.last_24_h_truncated === true
                           ? 'At least this many: the controller ran out of room to record them all, so this is a lower bound.'
                           : 'Every raise in the window is counted.',
                       rate.last_24_h_truncated === true ? 'warning' : '')
        );
        section.append(tiles);
        /* The controller has no real-time clock. Saying so is the difference
         * between evidence and a fabricated timeline, and it is the same
         * convention the alarm journal already declares. */
        section.append(node('p', 'alarm-metrics-note',
            'Measured against controller uptime, not calendar time: this controller has no real-time clock, '
            + 'and a restart resets these counters. '
            + String(rate.note || '')));
        return section;
    }

    /* ------------------------------------ A6: the priority distribution, as found
     *
     * Reported rather than tidied. EEMUA's 5/15/80 is not met on this controller
     * and cannot be, because with a handful of conditions the smallest non-zero
     * share is already far above 5%. The screen says the target is missed AND says
     * it is arithmetically out of reach, in that order, so nobody reads the miss as
     * an invitation to demote an alarm that genuinely stops the plant being
     * controlled safely. */
    function distributionBar(census) {
        const bar = node('div', 'alarm-dist-bar');
        [['high', census.high_percent], ['medium', census.medium_percent],
         ['low', census.low_percent]].forEach(([band, percent]) => {
            const value = Math.max(0, Number(percent) || 0);
            if (!value) return;
            const part = node('span', `alarm-dist-part band-${band}`);
            part.style.flexGrow = String(value);
            part.title = `${band}: ${value}%`;
            bar.append(part);
        });
        bar.setAttribute('role', 'img');
        bar.setAttribute('aria-label',
            `High ${Number(census.high_percent) || 0}%, medium ${Number(census.medium_percent) || 0}%, `
            + `low ${Number(census.low_percent) || 0}%`);
        return bar;
    }

    function distributionBlock(title, census, target, explanation) {
        const block = node('div', 'alarm-dist-block');
        block.append(node('h5', '', title));
        block.append(distributionBar(census));
        const figures = node('div', 'alarm-dist-figures');
        figures.append(
            distributionFigure('High', census.high, census.high_percent, target?.high_percent),
            distributionFigure('Medium', census.medium, census.medium_percent, target?.medium_percent),
            distributionFigure('Low', census.low, census.low_percent, target?.low_percent)
        );
        block.append(figures);
        const verdict = node('p', `alarm-dist-verdict ${census.meets_target === true ? 'tone-good' : 'tone-warning'}`,
            census.meets_target === true
                ? `Meets the EEMUA target across ${Number(census.total) || 0} condition(s).`
                : `Does not meet the EEMUA target across ${Number(census.total) || 0} condition(s).`);
        block.append(verdict);
        if (census.target_representable === false) {
            block.append(node('p', 'alarm-dist-reason',
                `Not reachable either: the smallest share ${Number(census.total) || 0} condition(s) can express is `
                + `${Number(census.smallest_representable_percent) || 0}%, so a ${Number(target?.high_percent) || 5}% high band does not exist here. `
                + 'Demoting a condition to move this number would make the alarm system worse, not better.'));
        }
        if (explanation) block.append(node('p', 'alarm-dist-reason', explanation));
        return block;
    }

    function distributionFigure(label, count, percent, targetPercent) {
        const item = node('div', 'alarm-dist-figure');
        item.append(
            node('span', '', label),
            node('strong', '', `${Number(percent) || 0}%`),
            node('small', '', `${Number(count) || 0} condition(s) · target ${Number(targetPercent) || 0}%`)
        );
        return item;
    }

    function alarmRationalisationSection(rationalisation) {
        const section = node('section', 'alarm-metrics');
        const head = node('div', 'alarm-metrics-head');
        head.append(
            node('p', 'eyebrow', 'EEMUA 191 · rationalised'),
            node('h4', '', 'Priority distribution')
        );
        section.append(head);
        if (!rationalisation || typeof rationalisation !== 'object') {
            section.append(node('p', 'alarm-metrics-note',
                'The controller did not report a priority distribution.'));
            return section;
        }
        const blocks = node('div', 'alarm-dist-grid');
        if (rationalisation.alarms) {
            blocks.append(distributionBlock('Alarms an operator must acknowledge',
                rationalisation.alarms, rationalisation.target,
                'This is the population EEMUA’s distribution is about.'));
        }
        if (rationalisation.conditions) {
            blocks.append(distributionBlock('All tracked conditions',
                rationalisation.conditions, rationalisation.target,
                'Adds the records rationalised out of the alarm table and into the event log, '
                + 'which is where this controller’s low-priority band lives.'));
        }
        section.append(blocks);
        if (rationalisation.note) {
            section.append(node('p', 'alarm-metrics-note', String(rationalisation.note)));
        }
        return section;
    }

    function metricTile(label, value, meaning, tone) {
        const tile = node('div', `alarm-metric${tone ? ` tone-${tone}` : ''}`);
        tile.append(node('span', '', label), node('strong', '', value));
        if (meaning) tile.append(node('small', '', meaning));
        return tile;
    }

    function alarmMetaRow(label, value) {
        const item = node('div', 'alarm-meta-item');
        item.append(node('span', '', label), node('strong', '', value));
        return item;
    }

    function acknowledgeControl(alarm) {
        if (alarm.acknowledged === true) {
            /* The controller has no operator identity model, so the record names a
             * CLASS of actor and never a person. Inventing "acknowledged by
             * <someone>" would put a fact in the incident record that the device
             * never had. Gap A8 in docs/ALARM_MANAGEMENT_RESEARCH.md.
             *
             * The class is read from the journal rather than assumed, because
             * acknowledgement is open to operators: assuming "engineering session"
             * would misattribute every operator acknowledgement. Falls back to the
             * neutral wording when the field is absent, which is what records
             * written before the field existed will look like. */
            const by = String(alarm.acknowledged_by || '');
            const who = by === 'operator' ? 'an operator at the plant'
                : by === 'engineering_session' ? 'an authenticated engineering session'
                : 'someone with access to this controller';
            return node('small', 'alarm-ack-note',
                `Acknowledged ${formatAge(alarm.acknowledged_age_ms)} by ${who}. `
                + 'This controller records no operator identity, so it cannot say who.');
        }
        /* No sign-in gate here, deliberately. Acknowledgement is an operator action
         * under ISA-18.2, and requiring an engineering session made the
         * returned-to-normal state impossible for the only person normally on site
         * to discharge -- so nothing was ever acknowledged and the outstanding list
         * stopped meaning anything. The suppression controls below DO still gate,
         * because those hide a live condition. See alarms_ack_post. */
        const button = node('button', 'button primary alarm-ack-button', 'Acknowledge');
        button.type = 'button';
        button.dataset.alarmCode = String(alarm.code);
        button.addEventListener('click', () => acknowledgeAlarm(alarm));
        return button;
    }

    /* ------------------------------------------- A9: suppression, on every row
     *
     * All three flags are drawn, not just the effective state, because an alarm can
     * be in more than one at once and collapsing them is the specific mistake
     * ISA-18.2 warns against. What each line answers is who decided and what ends
     * it. */
    function suppressionBlock(alarm) {
        const block = node('div', 'alarm-suppression');
        const meta = suppressionState(alarm);
        const key = String(alarm.suppression || 'none');
        /* The pill itself stays on the first screen whenever an alarm is
         * suppressed at all: it changes what the triage counts mean, so hiding
         * it would make the counts above misleading. Only the reasoning behind
         * it - who decided, what ends it, the recorded reason - moves down a
         * level. "Not suppressed" is the normal state and prints nothing. */
        if (key === 'none') return block;
        block.append(node('span', `alarm-suppression-pill tone-${meta.tone} suppression-${key}`, meta.label));

        const drawer = node('div', 'op-more-body');
        drawer.append(node('small', 'alarm-suppression-meaning', meta.meaning));
        const facts = node('div', 'alarm-suppression-facts');
        facts.append(alarmMetaRow('Decided by',
            String(alarm.suppression_authority || 'unknown')));
        facts.append(alarmMetaRow('Ends by itself',
            alarm.suppression_expires === true ? 'Yes, on expiry' : 'No — someone must end it'));
        if (Number(alarm.suppression_count) > 1) {
            /* Two suppressions at once is exactly the case a single flag hides. */
            facts.append(alarmMetaRow('Suppressions in force',
                `${Number(alarm.suppression_count)} at once`));
        }
        if (alarm.shelved === true) {
            facts.append(alarmMetaRow('Shelf remaining', formatDuration(alarm.shelf_remaining_ms)));
        }
        if (alarm.suppressed_by_design === true) {
            facts.append(alarmMetaRow('Explained by',
                String(alarm.design_suppressed_by || 'another condition')));
        }
        if (alarm.out_of_service === true) {
            facts.append(alarmMetaRow('Out of service since', formatAge(alarm.out_of_service_age_ms)));
        }
        drawer.append(facts);
        if (alarm.out_of_service === true && alarm.out_of_service_reason_text) {
            drawer.append(node('small', 'alarm-suppression-reason',
                `Recorded reason: ${alarm.out_of_service_reason_text}`));
        }
        block.append(details('engineering', 'Why this is suppressed', drawer));
        return block;
    }

    /* The asymmetry, made visible by where the controls sit. Acknowledge is a
     * plain button on the row: it is the operator's action under ISA-18.2 and it
     * needs no session. Shelving and out-of-service hide a live condition, still
     * require an engineering session, and therefore live one level down behind a
     * drawer that says so. The drawer is a signpost, not a lock - the controller
     * refuses these without a session regardless of what the browser drew. */
    function suppressionControls(alarm) {
        return details('engineering', 'Shelve or take out of service', suppressionActions(alarm));
    }

    function suppressionActions(alarm) {
        const wrap = node('div', 'alarm-suppress-actions');
        if (!engineeringAuthorized()) {
            /* No dead buttons: suppression is an engineering action and a control
             * that can only return 401 teaches an operator that the page is
             * broken. */
            wrap.append(node('small', '',
                'Shelving and out-of-service are engineering actions and need a session.'));
            return wrap;
        }
        if (alarm.shelved === true) {
            wrap.append(actionButton('Unshelve', 'secondary', () => unshelveAlarm(alarm)));
        } else {
            const picker = selectControl(`alarmShelfDuration-${alarm.code}`, 'Shelve for',
                String(state.shelfDuration), SHELF_DURATIONS.map(([ms, label]) => [String(ms), label]),
                (value) => { state.shelfDuration = Number(value); });
            wrap.append(picker);
            wrap.append(actionButton('Shelve', 'secondary', () => shelveAlarm(alarm)));
        }
        if (alarm.out_of_service === true) {
            wrap.append(actionButton('Return to service', 'secondary',
                () => setOutOfService(alarm, false)));
        } else {
            const reasons = Array.isArray(state.alarms?.out_of_service_reasons)
                ? state.alarms.out_of_service_reasons
                : OUT_OF_SERVICE_FALLBACK;
            const picker = selectControl(`alarmOosReason-${alarm.code}`, 'Out of service because',
                String(state.outOfServiceReason),
                reasons.map((entry) => [String(entry.reason), String(entry.text || entry.name)]),
                (value) => { state.outOfServiceReason = Number(value); });
            wrap.append(picker);
            wrap.append(actionButton('Take out of service', 'secondary',
                () => setOutOfService(alarm, true)));
        }
        if (alarm.suppressed_by_design === true) {
            /* Stated rather than implied by an absent button: the controller owns
             * this decision and releases it when the plant recovers. */
            wrap.append(node('small', '',
                'The suppressed-by-design state cannot be lifted here. It clears when the fault that explains this one clears.'));
        }
        return wrap;
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

    function actionButton(label, variant, onClick) {
        const button = node('button', `button ${variant} alarm-ack-button`, label);
        button.type = 'button';
        button.addEventListener('click', onClick);
        return button;
    }

    function alarmRow(alarm) {
        const meta = alarmState(alarm);
        const severity = String(alarm.severity || 'information');
        const returned = String(alarm.state) === 'rtn_unacknowledged';
        const row = node('article',
            `alarm-row severity-${severity} state-${String(alarm.state)}`
            + `${isOutstanding(alarm) ? ' alarm-outstanding' : ''}`
            + `${returned ? ' alarm-returned' : ''}`
            /* A suppressed row is marked, never faded: it is still a real
             * condition and still has to be readable. */
            + `${isSuppressed(alarm) ? ` alarm-suppressed suppression-${String(alarm.suppression)}` : ''}`);

        const marker = node('span', 'alarm-marker', severityIcon(severity));
        marker.setAttribute('aria-hidden', 'true');

        /* The triage row, and only the triage row: what it is, how bad, what
         * state it is in, how old, and what to do. The state MEANING (a sentence
         * of alarm-standard lifecycle per row, repeated down the whole list), the
         * priority rationale, the detail paragraph and the six metadata fields
         * were the bulk of this screen and are all one level down. The state
         * PILL stays - a returned-to-normal-unacknowledged condition must be
         * legible without anyone opening anything. */
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
            node('small', 'alarm-age', formatAge(alarm.last_raised_age_ms))
        );
        copy.append(heading, stateLine, suppressionBlock(alarm));
        /* The one sentence an operator acts on. The controller wrote it. */
        if (alarm.recommended_action) copy.append(node('small', 'alarm-action', alarm.recommended_action));

        const history = node('div', 'op-more-body');
        history.append(node('p', 'alarm-state-meaning', meta.meaning));
        if (alarm.detail) history.append(node('p', '', alarm.detail));
        /* A6: the priority and the reason it was assigned still travel with the
         * alarm. The rationalisation is only reviewable if the reasoning is on
         * the condition rather than in a spreadsheet nobody opens. */
        if (alarm.priority) {
            const priorityLine = node('div', 'alarm-priority-line');
            priorityLine.append(
                node('span', `alarm-priority-pill priority-${String(alarm.priority)}`,
                     `${String(alarm.priority)} priority`),
                node('small', 'alarm-priority-rationale', String(alarm.priority_rationale || ''))
            );
            history.append(priorityLine);
        }
        const metaGrid = node('div', 'alarm-meta');
        metaGrid.append(
            alarmMetaRow('First occurrence', formatAge(alarm.first_raised_age_ms)),
            alarmMetaRow('Last occurrence', formatAge(alarm.last_raised_age_ms)),
            alarmMetaRow('Duration', formatDuration(alarm.duration_ms)),
            alarmMetaRow('Occurrences', Number(alarm.occurrences) || 0),
            alarmMetaRow('Present now', alarm.present ? 'Yes' : 'No'),
            alarmMetaRow('Acknowledged', alarm.acknowledged ? formatAge(alarm.acknowledged_age_ms) : 'No')
        );
        history.append(metaGrid);
        copy.append(details('engineering', 'Condition history', history));

        const actions = node('div', 'alarm-actions');
        actions.append(acknowledgeControl(alarm), suppressionControls(alarm));
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

        /* The page name, the breadcrumb and the document title already say
         * "Alarms and events" - the heading and the paragraph that used to
         * restate it, and then list the columns the reader can see, are gone.
         * Counts first, then the list. */
        const head = node('div', 'op-section-head');
        head.append(node('div'));
        const refresh = node('button', 'button secondary', 'Refresh');
        refresh.type = 'button';
        refresh.addEventListener('click', refreshAlarms);
        head.append(refresh);
        view.append(head);

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
            selectControl('alarmFilterSuppression', 'Suppression', state.filterSuppression, [
                ['all', 'All conditions'],
                ['suppressed', 'Suppressed (any state)'],
                ['unsuppressed', 'Not suppressed'],
                ['shelved', 'Shelved by an operator'],
                ['suppressed_by_design', 'Suppressed by design'],
                ['out_of_service', 'Out of service']
            ], (value) => { state.filterSuppression = value; renderAlarmConsole(); }),
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

        /* Below the list, closed. Acknowledgement semantics are safety-relevant
         * and are kept word for word - an operator who thinks acknowledging
         * repairs something stops investigating - but they are a lesson, and a
         * lesson does not belong above the thing the reader came for. */
        const lifecycle = node('div', 'op-more-body');
        lifecycle.append(node('p', '',
            'Acknowledging records that someone has seen a condition. It never clears it — only the plant can do that. '
            + 'An acknowledged condition that is still present stays on this list and still counts as active.'));
        lifecycle.append(node('p', '', `State model: ${summary.state_model || 'ISA-18.2'}.`));
        Object.keys(ALARM_STATES).forEach((key) => {
            const entry = ALARM_STATES[key];
            const item = node('div', 'op-more-row');
            item.append(node('span', '', entry.label), node('small', '', entry.meaning));
            lifecycle.append(item);
        });
        view.append(details('engineering', 'How alarm states work', lifecycle));

        /* A10 then A6: what the alarm system is doing to the operator, and how
         * its priorities are distributed. Both are properties of the whole
         * system, both are evidence a service engineer needs, and neither is
         * something a plant operator acts on during a shift. */
        view.append(details('service', 'Alarm system performance',
            alarmRateSection(payload.rate),
            alarmRationalisationSection(payload.rationalisation)));
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

    /* One path for every suppression mutation, because they share every failure
     * mode: an engineering session is required, the controller's own wording is
     * the only trustworthy description of what happened, and a failure must say
     * plainly that nothing changed. Paraphrasing a suppression decision is how an
     * interface starts lying about what is being watched. */
    async function suppressionRequest(alarm, path, body, pending) {
        if (state.alarmBusy) return;
        state.alarmBusy = true;
        state.alarmError = null;
        state.alarmMessage = `${pending} ${alarm.id || alarm.code}…`;
        renderAlarmConsole();
        try {
            const result = await api(path, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(body)
            });
            state.alarmMessage = result.note || 'The controller recorded the change.';
        } catch (error) {
            if (error.status === 401) {
                state.alarmError = 'Changing alarm suppression requires an authenticated engineering session. '
                    + 'Open Engineering and sign in, then try again. Nothing was changed.';
            } else {
                state.alarmError = `The controller refused the change: ${error.message}. Nothing was changed.`;
            }
            state.alarmMessage = null;
        } finally {
            state.alarmBusy = false;
        }
        await refreshAlarms();
    }

    function shelveAlarm(alarm) {
        return suppressionRequest(alarm, '/api/operator/alarms/shelve',
            { code: Number(alarm.code), duration_ms: Number(state.shelfDuration) }, 'Shelving');
    }

    function unshelveAlarm(alarm) {
        return suppressionRequest(alarm, '/api/operator/alarms/unshelve',
            { code: Number(alarm.code) }, 'Unshelving');
    }

    function setOutOfService(alarm, wanted) {
        const body = { code: Number(alarm.code), out_of_service: Boolean(wanted) };
        /* The reason is required on the way out and meaningless on the way back,
         * and the controller enforces exactly that. */
        if (wanted) body.reason = Number(state.outOfServiceReason);
        return suppressionRequest(alarm, '/api/operator/alarms/out-of-service', body,
            wanted ? 'Taking out of service' : 'Returning to service');
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
        view.replaceChildren();

        /* What is left of this section after the duplicates were removed. The
         * three totals repeated the condition counts already tiled at the top of
         * the same page, and the "Active conditions" card listed the same
         * conditions as the alarm table directly above it - a second, subtly
         * different rendering of the row an operator is meant to act on. The
         * event ring answers a question the condition table does not: what has
         * been happening here recently. That is all it renders now. */
        const historyCard = node('article', 'op-card');
        historyCard.append(node('div', 'op-card-headline', 'Recent events'));
        const historyList = node('div', 'op-event-list');
        events.slice(0, 12).forEach((event) => historyList.append(eventRow(event)));
        if (!events.length) historyList.append(node('div', 'op-empty-state', 'Events will appear as controller states change.'));
        historyCard.append(historyList);
        view.append(historyCard);
    }

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
        meta.append(
            node('span', `op-state-pill ${event.active ? event.severity === 'critical' ? 'bad' : event.severity === 'warning' ? 'warning' : 'good' : ''}`,
                event.active ? severityWord : (states?.normal || 'Normal')),
            node('small', '', event.active ? 'Present now' : 'Returned to normal'),
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
            description: 'Grid exchange and solar production stored by the controller',
            series: [
                { key: 'grid_kw', label: 'Grid power', meaning: { positive: 'importing from the utility', negative: 'exporting to the utility' } },
                { key: 'solar_kw', label: 'Solar production', meaning: { positive: 'producing', negative: 'consuming' } }
            ]
        },
        meters: {
            title: 'Grid power trend',
            description: 'Grid exchange stored by the controller',
            series: [
                { key: 'grid_kw', label: 'Grid power', meaning: { positive: 'importing from the utility', negative: 'exporting to the utility' } }
            ]
        },
        inverters: {
            title: 'Solar production trend',
            description: 'Measured inverter production stored by the controller',
            series: [
                { key: 'solar_kw', label: 'Solar production', meaning: { positive: 'producing', negative: 'consuming' } }
            ]
        }
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
        if (current === 'alarms') {
            renderAlarmPage();
            return;
        }
        const target = current === 'dashboard' ? byId('operatorDashboardView') : current === 'meters' ? byId('operatorMeterView') : current === 'inverters' ? byId('operatorInverterView') : null;
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
            /* The navigation badge is driven by the alarm condition table, not
             * by the event ring: the condition table is the one that still
             * counts a fault that cleared itself unacknowledged. */
            renderAlarmPage();
            scheduleEnhance();
        } catch (error) {
            state.historyError = error?.message || 'Controller history is unavailable.';
            applyHistory();
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
        window.addEventListener('amx-operator-view-rendered', scheduleEnhance);
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