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
    /* SAFETY-RELEVANT. "Is this a measurement?" - not "does JavaScript have a
     * number for it".
     *
     * This used to be Number.isFinite(Number(value)), and Number(null) is 0.
     * So was Number(''), Number(false) and Number([]). Every one of those is the
     * controller saying "this quantity was not measured", and every one of them
     * was accepted as a reading and then printed as 0.00. It was visible on the
     * live device: /api/inverters returns measured_power_kw: null for an
     * inverter that is not enabled, and the fleet table printed "0.00 kW" and
     * "0%" for it - an inverter nobody was measuring, presented as an inverter
     * measured to be producing nothing.
     *
     * On a controller whose purpose is preventing reverse power, an unmeasured
     * quantity drawn as zero is the failure mode the whole product exists to
     * avoid, so the test is the same one web/pvdg-chart.js measured() applies to
     * every sample it plots: null, undefined, empty string and booleans are not
     * measurements, whatever they coerce to. */
    function finite(value) {
        if (value === null || value === undefined || value === '') return false;
        if (typeof value === 'boolean') return false;
        return Number.isFinite(Number(value));
    }
    function clamp(value, min, max) { return Math.min(max, Math.max(min, Number(value) || 0)); }
    function formatPower(value) {
        if (!finite(value)) return '—';
        const n = Number(value);
        const digits = Math.abs(n) >= 100 ? 1 : 2;
        return `${n.toFixed(digits)} kW`;
    }
    function formatPercent(value) { return finite(value) ? `${clamp(value, 0, 100).toFixed(0)}%` : '—'; }
    /* A bare number at the product's usual precision. Unmeasured is "—", never
     * 0 and never a blank that could be mistaken for a zero that failed to
     * render. */
    function formatValue(value) {
        if (!finite(value)) return '—';
        const n = Number(value);
        return n.toFixed(Math.abs(n) >= 100 ? 1 : 2);
    }
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

    /* ------------------------------------------------------- measurement bar
     *
     * This replaces the semicircular gauge, and the reason is specific to this
     * product rather than a matter of taste.
     *
     * The gauge spent a 220x140 box printing ONE number and one caption. On the
     * grid-power page the same screen already carried current, minimum, average
     * and peak under the trend chart, so the most valuable area on the page held
     * the least information on it. Worse, it drew the ABSOLUTE value against an
     * invented "1.2 x whatever is showing now" maximum. Two consequences follow
     * from that, and both matter on a reverse-power controller:
     *
     *   - the scale moved as the reading moved, so the needle position meant
     *     nothing between one refresh and the next; and
     *   - zero export - the single boundary this whole controller exists to
     *     defend - was not on the instrument at all, because taking |value|
     *     folds import and export onto the same side.
     *
     * A horizontal scale fixes both, in 96px rather than 140px, and has room to
     * carry what an operator actually needs: where the value is now, which way
     * the power is going, where it has been over a named window, and where the
     * limit is. Everything drawn is either a measurement or a stated limit;
     * nothing is inferred.
     *
     * The unmeasured rule is unchanged and non-negotiable. When the quantity is
     * not measured NO marker is drawn anywhere on the track, the track itself
     * goes dashed, and the reading is "—" with the word "Not measured". The
     * marker must never rest at the zero position, because a bar sitting at zero
     * and a bar sitting at "nobody is watching" are the two readings that must
     * never look alike. The same rule applies to the range statistics: a window
     * with no measured sample in it draws no span and no average.
     */

    /* Position of a value on the drawn track, 0..100. Callers only ever pass
     * finite values here; the unmeasured case is handled before this point. */
    function trackPercent(value, scale) {
        const span = scale.max - scale.min;
        if (!(span > 0)) return 0;
        return clamp(((Number(value) - scale.min) / span) * 100, 0, 100);
    }

    /* The bar and the chart under it must not disagree about the domain, so the
     * axis comes from the chart's own niceScale when it is available. */
    function measureScale(values, includeZero) {
        const chart = window.PvdgChart;
        const list = values.filter(finite).map(Number);
        const low = list.length ? Math.min(...list) : 0;
        const high = list.length ? Math.max(...list) : 1;
        if (chart && typeof chart.niceScale === 'function') {
            return chart.niceScale(low, high, { ticks: 4, includeZero: includeZero !== false });
        }
        const lo = includeZero === false ? low : Math.min(0, low);
        const hi = includeZero === false ? high : Math.max(0, high);
        return { min: lo, max: hi === lo ? lo + 1 : hi, ticks: [lo, hi] };
    }

    /* A marker label is centred on its tick, except near either end of the
     * track, where centring would push half of it outside the card and clip it.
     * "Zero export" sits at the extreme left whenever the site is importing
     * hard, which is most of the time, so this is the normal case rather than an
     * edge case. */
    function measureMark(kind, at, label, title) {
        const anchor = at < 12 ? ' anchor-start' : at > 88 ? ' anchor-end' : '';
        const mark = node('span', `op-measure-mark kind-${kind}${anchor}`);
        mark.style.left = `${at}%`;
        if (label) mark.append(node('small', '', label));
        if (title) mark.title = title;
        return mark;
    }

    function measureStat(label, value) {
        const cell = node('div', 'op-measure-stat');
        cell.append(node('span', '', label), node('strong', '', value));
        return cell;
    }

    /* spec:
     *   label      what is being measured
     *   value      the live reading, or a non-finite value for "not measured"
     *   unit       printed beside the reading when it is measured
     *   tone       good | warning | bad, applied to the current-value marker
     *   scale      { min, max } domain of the track
     *   signed     true when the track is bipolar and the negative half is
     *              export - drawn hatched, exactly as the trend chart draws it
     *   zeroLabel  what the zero reference is called on this page
     *   limits     [{ at, label, title }] stated operating limits
     *   range      { available, rangeLabel, stats } from the shared history
     *   direction  { flow, text } the arrow and its wording
     *   note       one line under the bar, only when there is something to say
     */
    function measureBar(spec) {
        const known = finite(spec.value);
        const scale = spec.scale;
        const range = spec.range && spec.range.available ? spec.range.stats : null;
        const wrap = node('div', `op-measure ${spec.tone || ''}${known ? '' : ' op-measure-unmeasured'}`);

        const head = node('div', 'op-measure-head');
        head.append(node('span', 'op-measure-label', spec.label));
        if (spec.direction) {
            const dir = node('span', 'op-measure-direction');
            dir.append(arrow(spec.direction.flow), node('span', '', spec.direction.text));
            wrap.append(head);
            head.append(dir);
        } else {
            wrap.append(head);
        }

        const reading = node('div', 'op-measure-reading');
        reading.append(node('strong', '', known ? formatValue(spec.value) : '—'));
        reading.append(node('span', 'op-measure-unit', known
            ? String(spec.unit || '')
            : stateWord('dataQuality', 'unavailable', 'Unavailable')));
        wrap.append(reading);

        const track = node('div', 'op-measure-track');
        /* The export half of a signed scale, hatched. Same meaning and same
         * visual language as the shaded export region on the trend chart, so
         * "below zero" reads identically on both instruments. */
        if (spec.signed && scale.min < 0) {
            const negative = node('span', 'op-measure-negative');
            negative.style.width = `${trackPercent(0, scale)}%`;
            track.append(negative);
        }
        /* Where the quantity has been over the window the chart below covers.
         * Drawn only when that window actually contains measured samples. */
        if (range) {
            const band = node('span', 'op-measure-span');
            const from = trackPercent(range.min, scale);
            const to = trackPercent(range.max, scale);
            band.style.left = `${from}%`;
            band.style.width = `${Math.max(0.6, to - from)}%`;
            band.title = `Measured range over the last ${spec.range.rangeLabel}`;
            track.append(band);
        }
        if (spec.signed) {
            track.append(measureMark('zero', trackPercent(0, scale), spec.zeroLabel || 'Zero',
                'The boundary the controller exists to defend'));
        }
        (spec.limits || []).forEach((limit) => {
            if (!finite(limit.at)) return;
            track.append(measureMark('limit', trackPercent(limit.at, scale), limit.label, limit.title));
        });
        if (range && finite(range.average)) {
            track.append(measureMark('average', trackPercent(range.average, scale), '',
                `Average over the last ${spec.range.rangeLabel}`));
        }
        /* No value marker at all when the quantity is not measured. */
        if (known) {
            const now = node('span', 'op-measure-now');
            now.style.left = `${trackPercent(spec.value, scale)}%`;
            track.append(now);
        }
        wrap.append(track);

        const axis = node('div', 'op-measure-axis');
        axis.append(node('span', '', formatValue(scale.min)), node('span', '', formatValue(scale.max)));
        wrap.append(axis);

        /* Current / minimum / average / peak over a NAMED window. An absent
         * figure prints "—", never 0. */
        const stats = node('div', 'op-measure-stats');
        stats.append(measureStat('Now', known ? `${formatValue(spec.value)} ${spec.unit || ''}`.trim() : '—'));
        stats.append(measureStat('Minimum', range ? `${formatValue(range.min)} ${spec.unit || ''}`.trim() : '—'));
        stats.append(measureStat('Average', range ? `${formatValue(range.average)} ${spec.unit || ''}`.trim() : '—'));
        stats.append(measureStat('Peak', range ? `${formatValue(range.max)} ${spec.unit || ''}`.trim() : '—'));
        wrap.append(stats);

        const window_ = spec.range && spec.range.rangeLabel;
        wrap.append(node('small', 'op-measure-window', range
            ? `Minimum, average and peak over the last ${window_}, from measured samples only.`
            : `No measured sample in the last ${window_ || 'selected window'}. Nothing is plotted on the scale — this is not zero.`));
        if (spec.note) wrap.append(node('small', 'op-measure-note', spec.note));
        return wrap;
    }

    function rangeFor(key) {
        const ops = window.AutomatrixOperations;
        return ops && typeof ops.rangeStats === 'function'
            ? ops.rangeStats(key)
            : { available: false, rangeLabel: null, stats: null };
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

    /* One line of page identity at most, and NOTHING when there is nothing to
     * say. The shell already prints the page name, the breadcrumb and the
     * document title from the route table, so a heading that repeats it is the
     * third copy of the same word.
     *
     * This used to be called with an empty title purely to carry a Refresh
     * button, which produced a full-width 48px flex row holding one control and
     * an empty div on every operator route. Measured at 1440x900 that was a
     * 94px band of nothing between the top bar and the first card with a lone
     * button floating in it - the largest single piece of dead space in the
     * product. The manual refresh has not been removed: it is bound to the
     * shell's existing top-bar refresh control instead, where a global action
     * belongs, and the screen still re-reads the controller every five seconds
     * on its own. A header with no title now returns null and is not appended. */
    function sectionHeader(title, subtitle) {
        if (!title && !subtitle) return null;
        const head = node('div', 'op-section-head');
        const copy = node('div');
        if (title) copy.append(node('h3', '', title));
        if (subtitle) copy.append(node('p', '', subtitle));
        head.append(copy);
        return head;
    }

    /* append() ignores null, so a page can pass an optional header straight
     * through without every caller testing for it. */
    function appendAll(view, ...children) {
        children.filter(Boolean).forEach((child) => view.append(child));
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
        /* The KIND of the value is on the element, so the stylesheet can set a
         * measured reading and a state word differently. "Not measured" and
         * "Monitoring only" were typeset at the same weight and size as
         * "25.23 kW", which puts a word where a reader is looking for a
         * quantity - the same confusion between a measurement and the absence
         * of one that the rest of this work exists to remove. */
        const card = node('div', `op-flow-source op-flow-${entry.id} kind-${String(entry.valueKind || 'not_measured')}`
            + `${entry.measured ? '' : ' op-flow-unmeasured'}`);
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
     * Current power, direction, and where the exchange has been over the window
     * the chart below is drawing - on one signed scale that has zero export on
     * it, because zero export is the boundary this controller exists to defend.
     *
     * The gauge that used to occupy this position showed |value| against a
     * maximum recomputed from the live reading on every refresh. It could not
     * show which side of zero the plant was on and its needle position was not
     * comparable between two refreshes. See measureBar() for the full note. */
    function renderMeter(payload) {
        const view = byId('operatorMeterView');
        if (!view) return;
        const status = payload.status || {};
        const meters = payload.meters?.meters || [];
        const primary = meters.find((item) => item.enabled) || meters[0];
        const runtime = primary?.runtime || {};
        const power = finite(status.grid_power_kw) ? Number(status.grid_power_kw) : runtime.active_power_kw;
        const online = runtime.online === true || (status.meter_online && !status.meter_stale);
        const direction = finite(power)
            ? (power > 0.01 ? 'Importing from the utility' : power < -0.01 ? 'Exporting to the utility' : 'Near-zero exchange')
            : stateWord('dataQuality', 'unavailable', 'Unavailable');
        const flow = finite(power) ? (power > 0.01 ? 'in' : power < -0.01 ? 'out' : 'balanced') : 'unknown';
        const range = rangeFor('grid_kw');
        const figures = range.available ? range.stats : null;
        /* The domain covers the live reading and everything the window has held,
         * and always contains zero so import and export stay on opposite,
         * labelled sides. Nothing about it moves with the reading alone. */
        const scale = measureScale([power, figures?.min, figures?.max], true);

        view.replaceChildren();
        const overview = node('div', 'op-meter-overview');
        const measureCard = node('article', 'op-card op-measure-card');
        measureCard.append(measureBar({
            label: 'Grid power at the point of common coupling',
            value: power,
            unit: 'kW',
            tone: online ? 'good' : 'bad',
            scale,
            signed: true,
            zeroLabel: 'Zero export',
            range,
            direction: { flow, text: direction },
            note: online ? '' : 'No current measurement. Automatic control cannot use this value.'
        }));
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
        overview.append(measureCard, stateCard);
        /* The table before the chart. The trend chart is 500px tall, so with the
         * table under it the dense, scannable answer to "which meter stopped"
         * was two screens down while a chart the operator had not asked for held
         * the fold. The chart is unchanged and still on the page; it is simply
         * no longer between the reader and the list. */
        view.append(overview, meterTable(meters), chartHost());
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
        const utilization = installed > 0 && finite(production) ? (production / installed) * 100 : NaN;
        const productionRange = rangeFor('solar_kw');

        view.replaceChildren();
        const top = node('div', 'op-inverter-overview');

        /* Production: a one-sided scale from zero to the installed capacity,
         * with the rated limit drawn as a stated limit rather than as the end of
         * an invented range. Unmeasured stays unmeasured - no marker anywhere on
         * the track and no percentage of capacity claimed. */
        const productionCard = node('article', 'op-card op-measure-card');
        productionCard.append(measureBar({
            label: 'Solar production, measured at the inverters',
            value: production,
            unit: 'kW',
            tone: finite(production) ? 'good' : 'warning',
            scale: measureScale([0, installed || 1, production, productionRange?.stats?.max], false),
            signed: false,
            range: productionRange,
            limits: installed > 0
                ? [{ at: installed, label: `${formatValue(installed)} installed`, title: 'Total rated capacity of every configured inverter' }]
                : [],
            note: finite(production) && installed > 0
                ? `${formatPercent(utilization)} of installed capacity.`
                : ''
        }));
        top.append(productionCard, fleetCard(summary, inverters));
        /* Same ordering as Grid power, for the same reason: "is anything down"
         * is answered by the table, and it was below a 500px chart. */
        view.append(top, inverterTable(inverters, telemetryMap), chartHost());
    }

    /* ------------------------------------------------------ fleet composition
     *
     * This replaces a semicircular "Fleet availability" gauge that read
     * "0.00 % / 0 of 0 online" on a site with one inverter configured and none
     * enabled. It was the largest object on the page and it was wrong twice.
     *
     * It was wrong as INFORMATION: two hundred pixels to say nothing is running.
     * It was wrong as a MEASUREMENT, which is the part that matters here. With
     * no inverter enabled there is no availability figure to state - the
     * denominator is zero - and "0.00 %" is a fabricated number that reads as
     * "the fleet is entirely unavailable" when the truth is "no fleet has been
     * commissioned yet". On a controller that curtails PV to protect a
     * generator, a manufactured zero in the place where a measurement belongs
     * is the same class of defect as an unmeasured power drawn as 0 kW, and it
     * is refused for the same reason.
     *
     * What is drawn instead is the thing the operator and the commissioning
     * engineer both actually need: how the configured inverters are accounted
     * for right now, and how much capacity the controller can actually command.
     * Commandable capacity is the number the control loop runs on - an inverter
     * that is online but does not accept external control is not a lever - and
     * it was nowhere on this screen before.
     *
     * A percentage is printed ONLY when there is a denominator for it. */
    function fleetCard(summary, inverters) {
        const configured = Array.isArray(inverters) ? inverters.length : 0;
        const enabled = Number(summary.enabled) || 0;
        const online = Number(summary.online) || 0;
        const offline = Math.max(0, enabled - online);
        const dormant = Math.max(0, configured - enabled);
        const availability = enabled > 0 ? (online / enabled) * 100 : NaN;

        const card = node('article', 'op-card op-fleet-card');
        const head = node('div', 'op-measure-head');
        head.append(node('span', 'op-measure-label', 'Inverter fleet'));
        head.append(node('span', `op-state-pill ${online > 0 ? 'good' : enabled > 0 ? 'bad' : ''}`,
            online > 0 ? stateWord('communication', 'online', 'Online')
                : enabled > 0 ? stateWord('communication', 'offline', 'Offline')
                : stateWord('commissioning', 'notConfigured', 'Not configured')));
        card.append(head);

        const reading = node('div', 'op-measure-reading');
        /* The headline is a COUNT, which is always known, rather than a
         * percentage, which is not. */
        reading.append(node('strong', '', `${online} of ${enabled}`));
        reading.append(node('span', 'op-measure-unit', 'answering'));
        card.append(reading);

        /* One stacked bar over the configured population. Every configured
         * inverter is in exactly one segment, so the bar is always full and its
         * proportions are counts rather than a derived ratio. */
        const bar = node('div', 'op-fleet-bar');
        [['online', online, stateWord('communication', 'online', 'Online')],
         ['offline', offline, stateWord('communication', 'offline', 'Offline')],
         ['dormant', dormant, stateWord('commissioning', 'notConfigured', 'Not configured')]
        ].forEach(([kind, count, word]) => {
            if (count <= 0) return;
            const part = node('span', `op-fleet-part kind-${kind}`);
            part.style.width = `${(count / Math.max(1, configured)) * 100}%`;
            part.title = `${count} ${word}`;
            bar.append(part);
        });
        if (!configured) bar.classList.add('op-fleet-empty');
        card.append(bar);

        const legend = node('div', 'op-fleet-legend');
        [['online', online, stateWord('communication', 'online', 'Online')],
         ['offline', offline, stateWord('communication', 'offline', 'Offline')],
         ['dormant', dormant, stateWord('commissioning', 'notConfigured', 'Not configured')]
        ].forEach(([kind, count, word]) => {
            const item = node('div', `op-fleet-key kind-${kind}`);
            item.append(node('strong', '', String(count)), node('span', '', word));
            legend.append(item);
        });
        card.append(legend);

        /* Capacity accounting. Rated capacity is a commissioned constant and is
         * always known; it is never confused with measured production. */
        const stats = node('div', 'op-measure-stats');
        stats.append(measureStat('Configured', `${formatValue(Number(summary.configured_rated_kw) || 0)} kW`));
        stats.append(measureStat('Enabled', `${formatValue(Number(summary.enabled_rated_kw) || 0)} kW`));
        stats.append(measureStat('Commandable', `${formatValue(Number(summary.commandable_rated_kw) || 0)} kW`));
        /* The one figure that genuinely has no value when nothing is enabled. */
        stats.append(measureStat('Answering', finite(availability) ? `${availability.toFixed(0)}%` : '—'));
        card.append(stats);

        card.append(node('small', 'op-measure-window', enabled > 0
            ? 'Answering is the share of ENABLED inverters replying now. Commandable capacity is what automatic control can actually move.'
            : 'No inverter is enabled, so there is no availability figure to state. Commandable capacity is what automatic control can actually move.'));
        return card;
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
                why: 'Export cannot be prevented without it.',
                action: 'Check the meter and its communication path.'
            });
        }
        if (commandable <= 0) {
            actions.push({
                condition: 'No inverter accepts external control.',
                why: 'There is nothing for the controller to command.',
                action: 'Ask Engineering to commission an inverter for control.'
            });
        }
        if (!authority.enabled) {
            actions.push({
                condition: 'Automatic control is switched off.',
                why: 'The controller is watching, not acting.',
                action: 'Ask Engineering to enable it when the plant is ready.'
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

    /* The shell's top-bar refresh control is the one manual refresh in the
     * product. It already existed and already refreshed the common controller
     * status; this makes it refresh the operator screens too, so removing the
     * per-page Refresh button removed a duplicate rather than a capability. The
     * screen also re-reads the controller every five seconds by itself. */
    function bindShellRefresh() {
        const button = byId('refreshButton');
        if (!button || button.dataset.operatorBound === 'true') return;
        button.dataset.operatorBound = 'true';
        button.addEventListener('click', () => { refreshAll(); });
    }

    async function refreshAll() {
        if (!isOperator() || state.busy) return;
        state.busy = true;
        const shellRefresh = byId('refreshButton');
        if (shellRefresh) shellRefresh.disabled = true;
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
            if (shellRefresh) shellRefresh.disabled = false;
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
        bindShellRefresh();
        onRoute();
        window.addEventListener('hashchange', onRoute);
        /* The measurement bars quote the same window the trend chart draws, and
         * that window is owned by operator-operations.js. Redraw when it lands
         * so the bar and the chart under it never disagree. The event is fired
         * only when history arrives from the controller, never by a render, so
         * this cannot re-enter. */
        window.addEventListener('amx-operator-history', () => { renderCurrent(); });
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
