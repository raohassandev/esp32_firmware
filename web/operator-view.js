(() => {
    'use strict';

    const byId = (id) => document.getElementById(id);
    const state = {
        busy: false,
        timer: null,
        lastPayload: null,
        /* The one chart lives in a node this module owns but never rebuilds.
         * renderX() re-appends the same element instead of recreating it, so
         * the chart instance operator-operations.js mounts into it survives the
         * five-second refresh with its focus, cursor and listeners intact. */
        chartHost: null,
        /* Same arrangement for the exceptions band. operator-operations.js holds
         * the alarm and event data, so it renders the band; this module decides
         * WHERE it goes, which is the top of the plant overview. An exception is
         * the first thing an operator needs and used to be the last thing on the
         * page, below two charts and eight tiles. */
        attentionHost: null,
        /* Site capability report (/api/telemetry): which quantities this
         * installation measures. devices.js already polls it on the dashboard
         * route, so it is consumed from that event rather than requested a
         * second time - the controller serves four client sockets (audit S1). */
        siteTelemetry: null
    };

    const ICONS = {
        grid: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 2 7 9h3l-3 13h10L14 9h3z"/></svg>',
        meter: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 4h16v16H4zM8 16a4 4 0 0 1 8 0M12 8v4l3-2"/></svg>',
        solar: '<svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="18" cy="6" r="3"/><path d="M3 11h12l2 9H5zM7 11l-1 9m5-9v9m4-5H4"/></svg>',
        control: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M5 7h14M5 17h14M9 4v6m6 4v6"/></svg>',
        alarm: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3 2 21h20zM12 9v5m0 3v1"/></svg>',
        wifi: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M3 9a15 15 0 0 1 18 0M6 13a10 10 0 0 1 12 0m-9 4a5 5 0 0 1 6 0m-3 3h.01"/></svg>',
        shield: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 2 4 5v6c0 5 3 9 8 11 5-2 8-6 8-11V5z"/><path d="m8 12 3 3 5-6"/></svg>',
        clock: '<svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/></svg>'
    };

    /* Direction of power, drawn rather than typed. The model hands back the
     * glyphs "→ ← ↔ ⋯ ⇢", which render at a different weight, baseline and
     * sometimes a different width in every browser and font fallback - and on a
     * screen whose whole job is "which way is the power going" the arrow is the
     * measurement, not decoration. These are the same five meanings as inline
     * SVG so they are identical everywhere and scale with the surrounding text.
     * No emoji: emoji are re-drawn by the platform and a coloured pictogram on a
     * control screen is at the mercy of whichever font the operator's tablet
     * happens to ship. */
    const FLOW_ARROWS = {
        in: '<svg viewBox="0 0 32 16" aria-hidden="true"><path d="M2 8h24m-6-6 6 6-6 6"/></svg>',
        out: '<svg viewBox="0 0 32 16" aria-hidden="true"><path d="M30 8H6m6-6-6 6 6 6"/></svg>',
        balanced: '<svg viewBox="0 0 32 16" aria-hidden="true"><path d="M4 5h24m-5-4 5 4-5 4M28 11H4m5 4-5-4 5-4"/></svg>',
        command: '<svg viewBox="0 0 32 16" aria-hidden="true"><path d="M2 8h4m4 0h4m4 0h4" stroke-dasharray="0"/><path d="m22 2 6 6-6 6"/></svg>',
        unknown: '<svg viewBox="0 0 32 16" aria-hidden="true"><circle cx="8" cy="8" r="1.6"/><circle cx="16" cy="8" r="1.6"/><circle cx="24" cy="8" r="1.6"/></svg>'
    };

    /* ------------------------------------------------------------ state taxonomy
     *
     * This screen used to carry Healthy, Ready, Available, Attention, Armed,
     * Safe, Safely disabled, Monitoring only, Not commissioned, Not monitored,
     * Clear, Normal and Review - thirteen words for five questions. They are
     * now drawn from the closed families published by web/app.js, so the same
     * condition reads the same way here, on the alarm centre and on the
     * engineering dashboard.
     *
     * Two of them are deliberately NOT ours to choose. The controller publishes
     * control_authority.mode_label and control_authority.inhibit_reason, and it
     * publishes grid_measurement.quality. Those are the firmware's statements
     * about a safety decision, and they are rendered exactly as received. */
    const VOCAB = () => window.AutomatrixUi?.STATES || null;
    function stateWord(family, key, fallback) {
        const states = VOCAB();
        return (states && states[family] && states[family][key]) || fallback;
    }
    function firmwareWord(value, absent) {
        const ui = window.AutomatrixUi;
        return ui ? ui.verbatim(value, absent) : (String(value || '').trim() || absent);
    }

    /* The firmware's own quality token for the grid measurement, presented as
     * written rather than re-scored here. "degraded" is the controller saying
     * something about the instrument that this screen has no basis to soften. */
    function measurementQuality(status) {
        const quality = status?.grid_measurement?.quality;
        if (typeof quality === 'string' && quality) return quality.charAt(0).toUpperCase() + quality.slice(1);
        if (status?.meter_online && !status?.meter_stale) return stateWord('dataQuality', 'good', 'Good');
        if (status?.meter_stale && status?.meter_has_data) return stateWord('dataQuality', 'stale', 'Stale');
        return stateWord('dataQuality', 'unavailable', 'Unavailable');
    }

    /* Control authority in the controller's words. mode_label is one of
     * "Monitoring only", "Commanding" or "Inhibited"; when it is inhibited the
     * reason is the firmware's sentence, not a summary of it. */
    function controlAuthority(status) {
        const authority = status?.control_authority || null;
        const label = firmwareWord(authority?.mode_label, null);
        if (label) {
            return {
                label,
                reason: firmwareWord(authority?.inhibit_reason, ''),
                commanding: authority?.command_authority === true,
                enabled: authority?.control_enabled === true
            };
        }
        const enabled = Boolean(status?.control_enabled);
        return {
            label: enabled ? stateWord('control', 'active', 'Active') : stateWord('control', 'standby', 'Standby'),
            reason: '',
            commanding: enabled,
            enabled
        };
    }

    function isOperator() { return document.documentElement.dataset.access !== 'engineering'; }
    function route() { return location.hash.replace(/^#\/?/, '') || 'dashboard'; }
    function node(tag, className = '', text = null) {
        const item = document.createElement(tag);
        if (className) item.className = className;
        if (text != null) item.textContent = String(text);
        return item;
    }
    function icon(name) {
        const item = node('span', 'op-icon');
        item.innerHTML = ICONS[name] || ICONS.shield;
        return item;
    }
    function arrow(direction) {
        const item = node('span', `op-flow-glyph dir-${direction}`);
        item.innerHTML = FLOW_ARROWS[direction] || FLOW_ARROWS.unknown;
        return item;
    }
    function finite(value) { return Number.isFinite(Number(value)); }
    function clamp(value, min, max) { return Math.min(max, Math.max(min, Number(value) || 0)); }
    function formatPower(value) {
        if (!finite(value)) return '—';
        const n = Number(value);
        const digits = Math.abs(n) >= 100 ? 1 : 2;
        return `${n.toFixed(digits)} kW`;
    }
    function formatPercent(value) { return finite(value) ? `${clamp(value, 0, 100).toFixed(0)}%` : '—'; }
    function formatAge(value) {
        const n = Number(value);
        if (!Number.isFinite(n) || n < 0) return 'No sample';
        if (n < 1000) return 'Just now';
        if (n < 60000) return `${Math.round(n / 1000)} s ago`;
        return `${(n / 60000).toFixed(1)} min ago`;
    }
    async function api(path) {
        const response = await fetch(path, { cache: 'no-store', credentials: 'same-origin' });
        const payload = await response.json().catch(() => ({}));
        if (!response.ok) throw new Error(payload.error || `HTTP ${response.status}`);
        return payload;
    }

    /* ------------------------------------------------- three presentation levels
     *
     * Operator / Engineering / Advanced Service. Nothing is deleted when it drops
     * a level: it moves inside one of these, closed by default, labelled with the
     * level it belongs to. That matters twice over. An operator screen that keeps
     * setpoint provenance, register origins and commissioning rationale in the
     * body text is unreadable at 3 a.m.; a product that DELETES them cannot be
     * commissioned or serviced at all.
     *
     * This is a disclosure control, never an authorisation control. Everything in
     * here is already readable by whoever loaded the page, and every action that
     * needs authority is still refused by the controller's default-deny gateway
     * whether or not the browser drew a button for it. */
    function details(level, summaryText, ...children) {
        const wrap = node('details', `op-more level-${level}`);
        const head = node('summary', 'op-more-summary');
        head.append(node('span', 'op-more-level', level === 'service' ? 'Service' : 'Engineering'),
            node('span', '', summaryText));
        wrap.append(head, ...children.filter(Boolean));
        return wrap;
    }

    function detailLine(label, value) {
        const row = node('div', 'op-more-row');
        row.append(node('span', '', label), node('strong', '', value));
        return row;
    }

    /* The browser-session sparkline this module used to draw is gone. It plotted
     * array position against a floating min/max window from at most 36 values
     * that only existed while the tab stayed open, and because both refreshAll()
     * and renderDashboard() appended to that window every cycle it recorded each
     * reading twice. The controller already stores a timestamped history; the
     * shared chart draws that instead, from a single mount point per page. */
    function chartHost() {
        if (!state.chartHost) {
            state.chartHost = node('div', 'op-chart-host');
            state.chartHost.id = 'operatorTrendHost';
        }
        return state.chartHost;
    }

    function attentionHost() {
        if (!state.attentionHost) {
            state.attentionHost = node('div', 'op-attention-host');
            state.attentionHost.id = 'operatorAttentionHost';
        }
        return state.attentionHost;
    }

    function gauge(value, max, title, unit, tone = 'good') {
        const safeMax = Math.max(1, Number(max) || 1);
        const ratio = clamp((Math.abs(Number(value) || 0) / safeMax) * 100, 0, 100);
        const circumference = 251.2;
        const offset = circumference - (circumference * ratio / 100);
        const wrap = node('div', `op-gauge ${tone}`);
        wrap.innerHTML = `<svg viewBox="0 0 120 72" role="img" aria-label="${title}"><path class="op-gauge-track" d="M18 62a42 42 0 0 1 84 0"/><path class="op-gauge-value" style="stroke-dashoffset:${offset}" d="M18 62a42 42 0 0 1 84 0"/></svg><div class="op-gauge-copy"><span>${title}</span><strong>${finite(value) ? Math.abs(Number(value)).toFixed(Math.abs(Number(value)) >= 100 ? 1 : 2) : '—'}</strong><small>${unit}</small></div>`;
        return wrap;
    }

    /* Label, value, and at most one status line - and the status line is omitted
     * entirely when there is nothing exceptional to say. Printing "Normal - the
     * plant is operating normally" under every healthy tile is how a screen
     * teaches its reader to stop reading it. */
    function kpi(iconName, label, value, detail, tone = '') {
        const card = node('article', `op-kpi ${tone}`);
        const head = node('div', 'op-kpi-head');
        head.append(icon(iconName), node('span', '', label));
        card.append(head, node('strong', 'op-kpi-value', value));
        if (detail) card.append(node('small', 'op-kpi-detail', detail));
        return card;
    }

    function statusLine(iconName, label, value, detail, tone) {
        const row = node('div', `op-status-row ${tone || ''}`);
        const lead = node('div', 'op-status-lead');
        lead.append(icon(iconName), node('span', '', label));
        const copy = node('div', 'op-status-copy');
        copy.append(node('strong', '', value));
        if (detail) copy.append(node('small', '', detail));
        row.append(lead, copy, node('span', 'op-status-dot'));
        return row;
    }

    /* One line of page identity at most. The shell already prints the page name,
     * the breadcrumb and the document title from the route table, so a heading
     * that repeats it is the third copy of the same word. Where this screen has
     * nothing to add, it passes an empty title and gets only the refresh action. */
    function sectionHeader(title, subtitle, actionLabel = '') {
        const head = node('div', 'op-section-head');
        const copy = node('div');
        if (title) copy.append(node('h3', '', title));
        if (subtitle) copy.append(node('p', '', subtitle));
        head.append(copy);
        if (actionLabel) {
            const button = node('button', 'button secondary op-refresh', actionLabel);
            button.type = 'button';
            button.addEventListener('click', refreshAll);
            head.append(button);
        }
        return head;
    }

    function ensureView(pageName, id, afterSelector = '.page-intro') {
        const page = document.querySelector(`[data-page="${pageName}"]`);
        if (!page) return null;
        let view = byId(id);
        if (view) return view;
        view = node('section', 'operator-product-view op-product-page');
        view.id = id;
        const anchor = page.querySelector(afterSelector);
        if (anchor) anchor.after(view); else page.prepend(view);
        return view;
    }

    function ensureViews() {
        ensureView('dashboard', 'operatorDashboardView');
        ensureView('meters', 'operatorMeterView');
        ensureView('inverters', 'operatorInverterView');
        ensureView('control', 'operatorControlView');
        ensureView('system', 'operatorSystemView');
    }

    /* ------------------------------------------------------------- power flow
     *
     * P0-7. This card used to show solar, controller and utility grid. For a
     * PV-DG product that leaves out the generator - the asset the whole control
     * strategy exists to protect - and the facility load both sources serve.
     *
     * The model is shared with the engineering dashboard (devices-utils.js) so
     * the two screens cannot disagree about the plant. Its two rules are what
     * make this card trustworthy on an unattended site: a quantity that is not
     * measured says "Not measured" instead of 0 kW, and no arrow is drawn out of
     * a node whose magnitude nobody is measuring.
     *
     * What changed here is only what a node SAYS by default. It used to print a
     * measured/commanded/not-measured pill, a sentence of detail and a
     * provenance string ("Automatrix EM500 · unit 1 · 194 ms · Good") on every
     * one of five nodes - fifteen lines of instrument bookkeeping, including a
     * Modbus unit id, over a diagram whose job is to answer "where is the power
     * coming from". The node now carries the four things the diagram is for:
     * source, kilowatts, direction and whether it is talking. The provenance,
     * the commissioning notes and the reasoning behind an unmeasured node are
     * intact, one level down.
     *
     * The unmeasured case is stated ONCE, in the coverage line above the
     * diagram, and the node itself reads "Not measured" as its value. It is
     * never 0 kW: on a controller whose entire purpose is preventing reverse
     * power, a zero where nothing is watching is the most dangerous number the
     * product could print. */
    function flowNodeCard(entry) {
        const iconName = entry.id === 'grid' ? 'grid'
            : entry.id === 'solar' ? 'solar'
            : entry.id === 'generator' ? 'meter'
            : entry.id === 'load' ? 'shield'
            : 'control';
        const card = node('div', `op-flow-source op-flow-${entry.id}${entry.measured ? '' : ' op-flow-unmeasured'}`);
        card.append(icon(iconName), node('span', '', entry.label), node('strong', '', entry.value));
        const link = entry.state === 'measured' ? stateWord('communication', 'online', 'Online')
            : entry.state === 'stale' ? stateWord('dataQuality', 'stale', 'Stale')
            : entry.state === 'unavailable' ? stateWord('communication', 'offline', 'Offline')
            : '';
        if (link) card.append(node('small', 'op-flow-link', link));
        return card;
    }

    function flowArrow(entry) {
        const wrap = node('div', `op-flow-arrow${entry.direction === 'unknown' ? ' op-flow-arrow-unknown' : ''}`);
        wrap.append(arrow(entry.direction), node('small', '', entry.directionLabel));
        return wrap;
    }

    /* Everything the node used to print in the operator's face. Kept verbatim -
     * these sentences are the record of what this installation does and does not
     * measure, and an engineer arriving after a reverse-power event needs them. */
    function flowProvenance(model) {
        const body = node('div', 'op-more-body');
        model.nodes.forEach((entry) => {
            const block = node('div', 'op-more-block');
            block.append(node('h4', '', entry.label));
            block.append(detailLine('Reading', `${entry.value} · ${entry.valueKind === 'measured' ? 'measured'
                : entry.valueKind === 'command' ? 'commanded, not measured' : 'not measured'}`));
            block.append(detailLine('Source', entry.provenance));
            if (entry.detail) block.append(node('p', '', entry.detail));
            (entry.notes || []).forEach((text) => block.append(node('p', 'op-more-note', text)));
            body.append(block);
        });
        return details('service', 'Measurement sources for each node', body);
    }

    function flowCard(payload) {
        const utils = window.PvdgDeviceUtils;
        const card = node('article', 'op-card op-flow-card');
        const head = node('div', 'op-card-head');
        head.append(node('h3', '', 'Power flow'));
        if (!utils) {
            card.append(head, node('div', 'op-empty-state', 'Power-flow model unavailable in this build.'));
            return card;
        }
        const model = utils.buildPowerFlowModel({
            status: payload.status,
            telemetry: payload.telemetry,
            inverterTelemetry: payload.inverterTelemetry
        });
        head.append(node('span', `op-state-pill ${model.summary.tone === 'neutral' ? '' : model.summary.tone}`, model.summary.label));
        card.append(head);
        /* The one compact statement of what this site does not measure. It
         * replaces a pill on every node saying the same thing five times. */
        if (model.summary.unmeasured.length) {
            card.append(node('p', 'op-flow-coverage', model.summary.coverage));
        }

        const body = node('div', 'op-flow-body');
        const supply = node('div', 'op-flow-column');
        const hub = node('div', 'op-flow-column op-flow-column-hub');
        const demand = node('div', 'op-flow-column');
        model.nodes.forEach((entry) => {
            const slot = node('div', 'op-flow-slot');
            slot.append(flowNodeCard(entry), flowArrow(entry));
            if (entry.role === 'supply') supply.append(slot);
            else if (entry.role === 'demand') demand.append(slot);
            else hub.append(slot);
        });
        body.append(supply, hub, demand);
        card.append(body, flowProvenance(model));
        return card;
    }

    /* ---------------------------------------------------------- plant overview
     *
     * Exceptions first, then the plant, then the trend. Every live kilowatt
     * figure on this screen is rendered exactly once, by the power-flow card:
     * the four KPI tiles that used to sit under it repeated grid exchange, solar
     * production and control authority a second time, and the readiness list
     * repeated control authority a third. When the same number is in three
     * places, two of them are eventually wrong. */
    function renderDashboard(payload) {
        const view = byId('operatorDashboardView');
        if (!view) return;
        const { status, inverters } = payload;
        const inverterSummary = inverters?.summary || {};
        const gridOnline = Boolean(status?.meter_online) && !Boolean(status?.meter_stale);
        const fleetState = Number(inverterSummary.online) > 0
            ? stateWord('communication', 'online', 'Online')
            : Number(inverterSummary.enabled) > 0
                ? stateWord('communication', 'offline', 'Offline')
                : stateWord('commissioning', 'notConfigured', 'Not configured');

        view.replaceChildren();
        view.append(sectionHeader('', '', 'Refresh'));
        /* operator-operations.js fills this: it holds the alarm and event data. */
        view.append(attentionHost());
        view.append(flowCard(payload));

        const equipment = node('article', 'op-card op-health-card');
        equipment.append(node('div', 'op-card-title', 'Equipment availability'));
        const readiness = node('div', 'op-readiness');
        readiness.append(
            statusLine('wifi', 'Controller network',
                status?.network_online ? stateWord('communication', 'online', 'Online') : stateWord('communication', 'offline', 'Offline'),
                status?.network_online ? '' : 'Check site network and router power',
                status?.network_online ? 'good' : 'bad'),
            statusLine('meter', 'Grid measurement', measurementQuality(status),
                gridOnline ? '' : 'Check the meter and its communication path',
                gridOnline ? 'good' : 'bad'),
            statusLine('solar', 'Solar fleet', fleetState,
                `${Number(inverterSummary.online) || 0} of ${Number(inverterSummary.enabled) || 0} online`,
                Number(inverterSummary.online) > 0 ? 'good' : 'warning')
        );
        equipment.append(readiness);
        view.append(equipment, chartHost());
    }

    /* --------------------------------------------------------------- grid power
     *
     * Current power and direction at the top; the range statistics (average and
     * peak demand) come from the one shared chart below, which is where they are
     * measured over a stated window rather than guessed from the live sample. */
    function renderMeter(payload) {
        const view = byId('operatorMeterView');
        if (!view) return;
        const status = payload.status || {};
        const meters = payload.meters?.meters || [];
        const primary = meters.find((item) => item.enabled) || meters[0];
        const runtime = primary?.runtime || {};
        const power = finite(status.grid_power_kw) ? Number(status.grid_power_kw) : runtime.active_power_kw;
        const online = runtime.online === true || (status.meter_online && !status.meter_stale);
        const trendMax = Math.max(100, Math.abs(Number(power) || 0)) * 1.2;
        const direction = finite(power)
            ? (power > 0.01 ? 'Importing from the utility' : power < -0.01 ? 'Exporting to the utility' : 'Near-zero exchange')
            : stateWord('dataQuality', 'unavailable', 'Unavailable');
        const flow = finite(power) ? (power > 0.01 ? 'in' : power < -0.01 ? 'out' : 'balanced') : 'unknown';

        view.replaceChildren();
        view.append(sectionHeader('Electrical supply status', '', 'Refresh'));
        const overview = node('div', 'op-meter-overview');
        const gaugeCard = node('article', 'op-card op-gauge-card');
        const heading = node('div', 'op-direction');
        heading.append(arrow(flow), node('span', '', direction));
        gaugeCard.append(gauge(power, trendMax, 'Grid power', 'kW', online ? 'good' : 'bad'), heading);
        if (!online) gaugeCard.append(node('small', '', 'No current measurement. Automatic control cannot use this value.'));
        const stateCard = node('article', 'op-card op-readiness-card');
        /* Merge note: the vocabulary helpers (stateWord, measurementQuality) come from
         * the terminology work; the structural change -- no local trend card, mount the
         * shared chart instead -- comes from the chart consolidation. Both are kept. */
        stateCard.append(node('div', 'op-card-headline', 'Measurement health'),
            statusLine('meter', 'Communication',
                online ? stateWord('communication', 'online', 'Online') : stateWord('communication', 'offline', 'Offline'),
                online ? '' : 'Field communication needs checking', online ? 'good' : 'bad'),
            statusLine('clock', 'Data quality', measurementQuality(status),
                `Last sample ${formatAge(runtime.data_age_ms ?? status.meter_age_ms)}`,
                online ? 'good' : 'bad'));
        overview.append(gaugeCard, stateCard);
        view.append(overview, chartHost());

        view.append(meterTable(meters));
    }

    /* Compact availability table: what is metering this site, is it talking, what
     * does it read, how old is the reading. Anything not working sorts first. */
    function meterTable(meters) {
        const card = node('article', 'op-card');
        card.append(node('div', 'op-card-headline', 'Meter availability'));
        if (!meters.length) {
            card.append(node('div', 'op-empty-state', 'No grid meter has been commissioned.'));
            return card;
        }
        const rank = (meter) => (meter.runtime?.online === true ? 2 : meter.enabled ? 0 : 1);
        const rows = meters.map((meter, index) => ({ meter, index })).sort((a, b) => rank(a.meter) - rank(b.meter));
        const table = node('table', 'op-table');
        const headRow = node('tr');
        ['Meter', 'State', 'Power', 'Last update'].forEach((label) => headRow.append(node('th', '', label)));
        const thead = node('thead');
        thead.append(headRow);
        table.append(thead);
        const tbody = node('tbody');
        rows.forEach(({ meter, index }) => {
            const data = meter.runtime || {};
            const healthy = data.online === true;
            const row = node('tr', healthy ? '' : meter.enabled ? 'op-row-bad' : 'op-row-muted');
            row.append(node('td', '', meter.name || `Grid meter ${index + 1}`));
            const stateCell = node('td');
            stateCell.append(node('span', `op-state-pill ${healthy ? 'good' : meter.enabled ? 'bad' : ''}`,
                healthy ? stateWord('communication', 'online', 'Online')
                    : meter.enabled ? stateWord('communication', 'offline', 'Offline')
                    : stateWord('commissioning', 'notConfigured', 'Not configured')));
            row.append(stateCell);
            row.append(node('td', 'op-num', healthy ? formatPower(data.active_power_kw) : '—'));
            row.append(node('td', '', healthy ? formatAge(data.data_age_ms) : '—'));
            tbody.append(row);
        });
        table.append(tbody);
        card.append(table);
        return card;
    }

    /* ----------------------------------------------------------- solar fleet
     *
     * Fleet summary, one production chart, one dense table. The page used to
     * draw a full-width card per inverter with its own progress bar; at the
     * sixteen inverters this product supports that is sixteen screens of
     * scrolling to answer "is anything down". */
    function renderInverters(payload) {
        const view = byId('operatorInverterView');
        if (!view) return;
        const config = payload.inverters || {};
        const telemetry = payload.inverterTelemetry || {};
        const inverters = config.inverters || [];
        const summary = config.summary || {};
        const telemetryMap = new Map((telemetry.inverters || []).map((item) => [Number(item.index), item]));
        const production = Number(telemetry.summary?.telemetry_valid) > 0 ? Number(telemetry.summary.measured_total_kw) : NaN;
        const installed = Number(summary.configured_rated_kw) || 0;
        const availability = Number(summary.enabled) > 0 ? (Number(summary.online) / Number(summary.enabled)) * 100 : 0;
        const utilization = installed > 0 && finite(production) ? (production / installed) * 100 : NaN;

        view.replaceChildren();
        view.append(sectionHeader('Inverter fleet status', '', 'Refresh'));
        const top = node('div', 'op-inverter-overview');
        const productionCard = node('article', 'op-card op-gauge-card');
        productionCard.append(gauge(production, installed || 1, 'Solar production', 'kW', finite(production) ? 'good' : 'warning'),
            node('strong', 'op-direction', finite(production) ? `${formatPercent(utilization)} of installed capacity` : stateWord('dataQuality', 'unavailable', 'Unavailable')));
        const availabilityCard = node('article', 'op-card op-gauge-card');
        availabilityCard.append(gauge(availability, 100, 'Fleet availability', '%', availability >= 99 ? 'good' : availability > 0 ? 'warning' : 'bad'),
            node('strong', 'op-direction', `${Number(summary.online) || 0} of ${Number(summary.enabled) || 0} online`));
        top.append(productionCard, availabilityCard);
        view.append(top, chartHost());
        view.append(inverterTable(inverters, telemetryMap));
    }

    function inverterTable(inverters, telemetryMap) {
        const card = node('article', 'op-card');
        card.append(node('div', 'op-card-headline', 'Inverters'));
        if (!inverters.length) {
            card.append(node('div', 'op-empty-state', 'No solar inverter has been commissioned.'));
            return card;
        }
        const rows = inverters.map((inverter, index) => {
            const live = telemetryMap.get(Number(inverter.index ?? index)) || {};
            const online = inverter.runtime?.online === true || live.online === true;
            const measured = live.telemetry_valid ? Number(live.measured_power_kw)
                : finite(inverter.measured_power_kw) ? Number(inverter.measured_power_kw) : NaN;
            const rated = Number(inverter.rated_kw) || 0;
            return {
                name: inverter.name || `Solar inverter ${index + 1}`,
                enabled: inverter.enabled === true,
                online, measured, rated,
                age: live.data_age_ms ?? inverter.runtime?.data_age_ms,
                /* Failed and offline first: this table is read to find the unit
                 * that stopped, not to admire the ones that did not. */
                rank: online ? 2 : inverter.enabled ? 0 : 1
            };
        }).sort((a, b) => a.rank - b.rank);

        const table = node('table', 'op-table');
        const headRow = node('tr');
        ['Inverter', 'State', 'Now', 'Rated', 'Use', 'Last update'].forEach((label) => headRow.append(node('th', '', label)));
        const thead = node('thead');
        thead.append(headRow);
        table.append(thead);
        const tbody = node('tbody');
        rows.forEach((entry) => {
            const use = entry.rated > 0 && finite(entry.measured) ? clamp((entry.measured / entry.rated) * 100, 0, 100) : NaN;
            const row = node('tr', entry.online ? '' : entry.enabled ? 'op-row-bad' : 'op-row-muted');
            row.append(node('td', '', entry.name));
            const stateCell = node('td');
            stateCell.append(node('span', `op-state-pill ${entry.online ? 'good' : entry.enabled ? 'bad' : ''}`,
                entry.online ? stateWord('communication', 'online', 'Online')
                    : entry.enabled ? stateWord('communication', 'offline', 'Offline')
                    : stateWord('commissioning', 'notConfigured', 'Not configured')));
            row.append(stateCell);
            /* Never 0 kW for an inverter nobody is measuring. */
            row.append(node('td', 'op-num', finite(entry.measured) ? formatPower(entry.measured) : '—'));
            row.append(node('td', 'op-num', entry.rated ? formatPower(entry.rated) : '—'));
            row.append(node('td', 'op-num', finite(use) ? formatPercent(use) : '—'));
            row.append(node('td', '', finite(entry.age) ? formatAge(entry.age) : '—'));
            tbody.append(row);
        });
        table.append(tbody);
        card.append(table);
        return card;
    }

    /* --------------------------------------------------------- PV-DG control
     *
     * One authoritative statement of control state, in the controller's own
     * words. A reason ONLY when there is something blocking it - a screen that
     * explains at length why a working system is working is a screen nobody
     * reads when it stops working. At most three required actions. */
    function controlActions(status, payload) {
        const authority = controlAuthority(status);
        const meterReady = Boolean(status.meter_online) && !Boolean(status.meter_stale);
        const commandable = Number(payload.inverters?.summary?.commandable_rated_kw) || 0;
        const actions = [];
        if (!meterReady) {
            actions.push({
                condition: 'The grid measurement is not usable.',
                why: 'Automatic control cannot verify export protection without it.',
                action: 'Check the meter and its communication path, then ask Engineering to confirm it.'
            });
        }
        if (commandable <= 0) {
            actions.push({
                condition: 'No inverter is approved to accept control.',
                why: 'Automatic control has nothing it is allowed to command.',
                action: 'Ask Engineering to commission at least one inverter for external control.'
            });
        }
        if (!authority.enabled) {
            actions.push({
                condition: 'Automatic control is switched off.',
                why: 'The controller is watching the plant but not acting on it.',
                action: 'Ask Engineering to enable automatic control when the plant is ready.'
            });
        }
        return actions.slice(0, 3);
    }

    function renderControl(payload) {
        const view = byId('operatorControlView');
        if (!view) return;
        const status = payload.status || {};
        const authority = controlAuthority(status);
        const actions = controlActions(status, payload);
        const ready = authority.commanding && !actions.length;

        view.replaceChildren();
        view.append(sectionHeader('', '', 'Refresh'));

        /* The heading is the controller's own mode_label and the sentence under
         * it is its own inhibit_reason. Rewriting either would be this screen
         * inventing a safety statement the firmware did not make. Nothing is
         * printed under a mode that is not blocked. */
        const hero = node('article', `op-card op-control-hero ${ready ? 'good' : authority.enabled ? 'warning' : ''}`);
        hero.append(icon('control'), node('div', ''));
        hero.lastChild.append(node('h3', '', authority.label));
        if (authority.reason) hero.lastChild.append(node('p', '', authority.reason));
        view.append(hero);

        if (actions.length) {
            const card = node('article', 'op-card op-decision-card');
            card.append(node('div', 'op-card-headline', 'Required action'));
            const list = node('div', 'op-action-list');
            actions.forEach((entry) => {
                const row = node('div', 'op-action-row');
                row.append(node('strong', '', entry.condition),
                    node('small', 'op-action-why', entry.why),
                    node('small', 'op-action-do', entry.action));
                list.append(row);
            });
            card.append(list);
            view.append(card);
        }

        /* Engineering level: the individual prerequisites behind the verdict
         * above. An operator does not act on these; a commissioning engineer
         * standing next to the operator does. */
        const evidence = node('div', 'op-more-body');
        evidence.append(
            detailLine('Grid measurement', measurementQuality(status)),
            detailLine('Commandable solar', formatPower(Number(payload.inverters?.summary?.commandable_rated_kw) || 0)),
            detailLine('Command path', authority.commanding ? stateWord('control', 'active', 'Active')
                : authority.enabled ? stateWord('control', 'inhibited', 'Inhibited')
                : stateWord('control', 'standby', 'Standby'))
        );
        view.append(details('engineering', 'Control prerequisites', evidence));
    }

    /* ------------------------------------------------------------- controller
     *
     * Which controller is this, is it reachable, and is anything wrong with it.
     * Everything else that used to be here - what Engineering contains, how the
     * page refreshes, the product tagline - answered a question nobody asked. */
    function renderSystem(payload) {
        const view = byId('operatorSystemView');
        if (!view) return;
        const status = payload.status || {};
        const alarms = Array.isArray(status.alarm_names) ? status.alarm_names : [];
        view.replaceChildren();
        view.append(sectionHeader('', '', 'Refresh'));
        const card = node('article', 'op-card');
        card.append(node('div', 'op-card-headline', 'Controller'),
            statusLine('shield', 'Product', 'Automatrix PV-DG Controller', '', 'good'),
            statusLine('wifi', 'Connection',
                status.network_online ? stateWord('communication', 'online', 'Online') : stateWord('communication', 'offline', 'Offline'),
                status.network_online ? `${status.ssid || 'Wi-Fi'} · ${status.ip || ''}`.trim() : 'Network connection unavailable',
                status.network_online ? 'good' : 'bad'),
            statusLine('alarm', 'Alarms',
                alarms.length ? `${alarms.length} ${stateWord('alarm', 'critical', 'Critical')}` : stateWord('alarm', 'normal', 'Normal'),
                alarms.length ? alarms.join(', ') : '', alarms.length ? 'bad' : 'good'));
        view.append(card);

        const support = node('article', 'op-card op-support-card');
        support.append(icon('shield'), node('div', '', ''));
        support.lastChild.append(node('p', '', 'Setup, commissioning and diagnostics are in the protected Engineering area.'));
        view.append(support);
    }

    function hideLegacyOperatorContent() {
        if (!isOperator()) return;
        ['dashboard', 'meters', 'inverters', 'control', 'system'].forEach((name) => {
            const page = document.querySelector(`[data-page="${name}"]`);
            if (!page) return;
            Array.from(page.children).forEach((child) => {
                if (!child.classList.contains('operator-product-view') && !child.classList.contains('page-intro')) child.classList.add('operator-legacy-hidden');
            });
        });
    }

    /* One durable name per page, whatever the access level. This function used
     * to rename four sidebar entries depending on whether engineering was
     * unlocked, so "Meters" and "Grid Power" were the same page and an
     * instruction given over the phone did not match what was on screen. The
     * names now live in one table in web/app.js and are applied there. */
    function updateLanguage() {
        window.AutomatrixUi?.ensureNavigationHierarchy();
        const wifiLink = document.querySelector('[data-route="wifi"]');
        if (wifiLink) wifiLink.dataset.engineeringNav = 'true';
    }

    async function refreshAll() {
        if (!isOperator() || state.busy) return;
        state.busy = true;
        document.querySelectorAll('.op-refresh').forEach((button) => { button.disabled = true; button.textContent = 'Refreshing…'; });
        try {
            const [status, meters, inverters, inverterTelemetry] = await Promise.all([
                api('/api/status'), api('/api/meters'), api('/api/inverters'), api('/api/inverter-telemetry')
            ]);
            state.lastPayload = { status, meters, inverters, inverterTelemetry, telemetry: state.siteTelemetry };
            renderCurrent();
        } catch (error) {
            const view = document.querySelector('.page.active .operator-product-view');
            if (view) {
                view.replaceChildren(sectionHeader('The controller did not answer', ''));
                const message = node('article', 'op-card op-error-state');
                message.append(icon('alarm'), node('div', ''));
                message.lastChild.append(node('p', '', String(error.message || '')),
                    node('p', '', 'Values on this screen are not current.'));
                view.append(message);
            }
        } finally {
            state.busy = false;
            document.querySelectorAll('.op-refresh').forEach((button) => { button.disabled = false; button.textContent = 'Refresh'; });
        }
    }

    function renderCurrent() {
        if (!state.lastPayload || !isOperator()) return;
        const current = route();
        if (current === 'dashboard') renderDashboard(state.lastPayload);
        else if (current === 'meters') renderMeter(state.lastPayload);
        else if (current === 'inverters') renderInverters(state.lastPayload);
        else if (current === 'control') renderControl(state.lastPayload);
        else if (current === 'system') renderSystem(state.lastPayload);
        /* This view is rebuilt every refresh. The chart and the exceptions band
         * are not: they live in nodes this module keeps and re-appends, so they
         * survive the rebuild. The event tells the module that owns them that a
         * mount point is available now rather than making it wait for its own
         * next poll. */
        window.dispatchEvent(new CustomEvent('amx-operator-view-rendered', { detail: { route: current } }));
    }

    /* The title, the breadcrumb and the selected navigation item come from the
     * single route table. This function previously kept a second table whose
     * title and breadcrumb differed from each other and from the sidebar
     * ("Grid Power" / "Grid power" / "Meters" for one page). */
    function updateRouteTitles() {
        window.AutomatrixUi?.applyRouteChrome(route());
    }

    function onRoute() {
        updateLanguage();
        updateRouteTitles();
        hideLegacyOperatorContent();
        renderCurrent();
        if (isOperator() && !state.lastPayload) refreshAll();
    }

    function start() {
        ensureViews();
        updateLanguage();
        hideLegacyOperatorContent();
        onRoute();
        window.addEventListener('hashchange', onRoute);
        window.addEventListener('amx-site-telemetry', (event) => {
            state.siteTelemetry = event.detail || null;
            if (state.lastPayload) {
                state.lastPayload.telemetry = state.siteTelemetry;
                renderCurrent();
            }
        });
        state.timer = window.setInterval(refreshAll, 5000);
        new MutationObserver(() => {
            updateLanguage();
            hideLegacyOperatorContent();
            onRoute();
        }).observe(document.documentElement, { attributes: true, attributeFilter: ['data-access'] });
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();
