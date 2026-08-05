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
    /* Options are passed through so this can POST. It could not, and a caller
     * that handed it a method got a silent GET -- a request that looks like it
     * succeeded and changed nothing, which on an arming control would report a
     * plant armed that was never asked to arm. */
    async function api(path, options) {
        /* Through the shared reader: several modules poll these same paths on
         * their own timers, and the controller has very few client sockets. A
         * GET already in flight or answered a moment ago is reused instead of
         * asked again. See web/shared-fetch.js. */
        const method = (options && options.method) || 'GET';
        if (method === 'GET' && window.AutomatrixFetch) {
            return window.AutomatrixFetch.get(path);
        }
        const response = await fetch(path, {
            cache: 'no-store', credentials: 'same-origin', ...(options || {})
        });
        const payload = await response.json().catch(() => ({}));
        if (!response.ok) {
            throw new Error(payload.message || payload.error || `HTTP ${response.status}`);
        }
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
    /* ------------------------------------------------------------- energy flow
     *
     * The picture every solar platform opens on -- FusionSolar, SolarEdge,
     * iSolarCloud, Fronius, SMA all draw the same thing -- and the one this
     * product did not have. It is the diagram a customer already knows how to
     * read, and its absence is a large part of why this interface did not look
     * like a solar product.
     *
     * WHAT IT IS ALLOWED TO SAY. Quantity and direction, nothing else. No status,
     * no verdict, no alarm. Those live in the control strip above, which is
     * neutral until something is wrong. Keeping them apart is the whole point:
     * this block uses colour to tell one flow from another, and it may only do
     * that because nothing in it is a safety statement competing for the same
     * attention.
     *
     * WHAT IS DERIVED AND WHAT IS MEASURED. Grid and solar are measured. The
     * generator is measured when a generator-role meter exists. LOAD IS NOT
     * MEASURED -- it is grid + generator + solar, the site's own definition --
     * and it is labelled as derived on screen, because a number a reader assumes
     * came from an instrument is a number they will trust further than it
     * deserves.
     */
    /* ------------------------------------------------------------- energy flow
     *
     * The picture every solar platform opens on -- FusionSolar, SolarEdge,
     * iSolarCloud, Fronius and SMA all draw the same thing -- and the one this
     * product did not have.
     *
     * THE GEOMETRY IS THE PRODUCT OWNER'S, supplied as a working mockup: sources
     * on the outside, a junction in the middle, load to the right, animated
     * dashes along SVG paths, and a genuinely different arrangement on a phone.
     * That is kept exactly. What is NOT kept is the mockup's data model, and the
     * difference is the whole point of this comment.
     *
     * THE MOCKUP DERIVED THE GRID. It computed `grid = load - solar - generator`
     * from a load it invented with a sine wave. This product is the other way
     * round and must stay that way: GRID IS MEASURED, by the instrument the
     * entire control loop regulates against, and LOAD IS DERIVED from it. Adopt
     * the mockup's direction and the diagram would show a grid figure this file
     * calculated, on the same screen as a controller acting on a different one --
     * and the screen would be the more convincing of the two.
     *
     * WHAT IT IS ALLOWED TO SAY. Quantity and direction, nothing else. No status,
     * no verdict, no alarm; those live in the control strip above, which stays
     * neutral until something is wrong. Keeping them apart is what lets this
     * block use colour to tell one flow from another.
     *
     * WHAT IS MEASURED AND WHAT IS NOT. Grid and solar are measured. The
     * generator is measured when a generator-role meter exists. LOAD IS NOT --
     * it is grid + generator + solar, the site's own definition -- and it is
     * labelled as derived on screen, because a number a reader assumes came from
     * an instrument is one they will trust further than it deserves.
     */
    const FLOW_GEOMETRY = {
        desktop: [
            { flow: 'solar', d: 'M50 24 V50' },
            { flow: 'grid', d: 'M42 50 H50' },
            { flow: 'load', d: 'M50 50 H58' },
            { flow: 'generator', d: 'M50 76 V50' }
        ],
        /* Curved, because the stacked phone layout puts the two sources side by
         * side above the junction: straight connectors there would cross the
         * cards rather than run between them. */
        mobile: [
            { flow: 'solar', d: 'M25 25 C25 43 38 48 50 62' },
            { flow: 'grid', d: 'M75 25 C75 43 62 48 50 62' },
            { flow: 'generator', d: 'M50 53 V62' },
            { flow: 'load', d: 'M50 68 V76' }
        ]
    };

    const SVG_NS = 'http://www.w3.org/2000/svg';

    function svgNode(tag, attributes) {
        const element = document.createElementNS(SVG_NS, tag);
        Object.entries(attributes || {}).forEach(([name, value]) => {
            element.setAttribute(name, String(value));
        });
        return element;
    }

    /*
     * One path set. `active` decides which flows are drawn over their base
     * lines, and `reversed` runs the dashes the other way.
     *
     * DIRECTION IS NOT DECORATION on the grid link: import and export are the
     * same magnitude with opposite meaning, and a number alone does not say
     * which. The node's own note says it in words as well, because a moving line
     * is a convention and the reader may not share it.
     */
    function flowLines(variant, active, reversed) {
        const svg = svgNode('svg', {
            class: `amx-flow-lines amx-flow-lines-${variant}`,
            viewBox: '0 0 100 100',
            preserveAspectRatio: 'none',
            'aria-hidden': 'true'
        });

        FLOW_GEOMETRY[variant].forEach(({ d }) => {
            svg.append(svgNode('path', { class: 'amx-flow-base', d }));
        });

        FLOW_GEOMETRY[variant].forEach(({ flow, d }) => {
            const path = svgNode('path', {
                class: `amx-flow-path is-${flow}${active[flow] ? '' : ' is-inactive'}`,
                d
            });
            /* SMIL rather than a CSS animation: the dash offset has to travel
             * along the path itself, and only the path knows its own length.
             * The animation is created regardless and simply invisible on an
             * inactive path, so a flow that starts moving does not have to wait
             * for an element to be built. */
            path.append(svgNode('animate', {
                attributeName: 'stroke-dashoffset',
                from: '0',
                to: reversed[flow] ? '28' : '-28',
                dur: '0.75s',
                repeatCount: 'indefinite'
            }));
            svg.append(path);
        });
        return svg;
    }

    function flowNode(kind, area, iconName, name, kw, note) {
        const cards = window.AutomatrixCards;
        const value = cards.measured(kw);
        const absent = value === null;
        const element = node('article',
            `amx-flow-node is-${kind} amx-flow-${area}${absent ? ' is-absent' : ''}`);
        element.append(cards.icon(iconName));
        element.append(node('span', 'amx-flow-name', name));

        const figure = node('span', 'amx-flow-value');
        /* An em dash, never a zero. "0.0 kW" says the instrument measured
         * nothing; "—" says nothing was measured, and on a power screen those
         * are different plants. Magnitude only -- the sign is carried by the
         * arrow direction and by the note, where it can be read. */
        figure.append(document.createTextNode(absent ? '—' : Math.abs(value).toFixed(1)));
        if (!absent) figure.append(node('span', 'amx-flow-unit', 'kW'));
        element.append(figure);

        if (note) element.append(node('span', 'amx-flow-note', note));
        return element;
    }

    function flowChip(kind, label) {
        const chip = node('span', 'amx-flow-chip');
        const dot = node('span', `amx-flow-dot is-${kind}`);
        dot.style.background = `var(--${kind === 'solar' ? 'yellow'
            : kind === 'grid' ? 'blue' : 'orange'})`;
        chip.append(dot, document.createTextNode(label));
        return chip;
    }

    function energyFlow(payload) {
        const cards = window.AutomatrixCards;
        const status = payload.status || {};
        const telemetry = payload.inverterTelemetry || {};

        const card = node('article', 'amx-card amx-wide');
        card.append(node('span', 'amx-card-label', 'Power flow'));

        /* MEASURED. Only used when the meter is online and not stale: a retained
         * value drawn as a live flow is how a diagram shows a plant importing
         * from a meter that stopped answering. */
        const sourceFresh = Boolean(status.meter_online) && !Boolean(status.meter_stale);
        const measuredKw = sourceFresh && finite(status.grid_power_kw)
            ? Number(status.grid_power_kw) : null;
        const solarKw = cards.measured(telemetry?.summary?.measured_total_kw);

        /*
         * WHOSE POWER IS IT.
         *
         * This diagram drew the measured 347.3 kW under GRID while the
         * controller had resolved GENERATOR from the tariff input, with the
         * generator node dimmed and captioned "not running". The measurement was
         * never wrong -- its NAME was.
         *
         * On a single-meter tariff plant one meter measures whichever source is
         * live. The firmware settles which, and publishes it; this asks rather
         * than assuming. See web/source-attribution.js.
         */
        const attribution = window.AutomatrixSource?.attribution(status)
            || { node: 'grid', label: 'Grid', known: true, reason: '', direction: (kw) => (kw > 0.01 ? 'importing' : kw < -0.01 ? 'exporting' : 'balanced') };

        /* The measurement lands on whichever node the controller says it
         * belongs to. The other source is not "zero" -- it is not measured, and
         * an em dash says that where a 0.0 would claim the meter looked. */
        const onGenerator = attribution.node === 'generator';
        const gridKw = onGenerator ? null : measuredKw;
        const generatorKw = onGenerator ? measuredKw
            : cards.measured(status.generator_power_kw);

        /* Import-positive: above zero the source supplies the site, below it the
         * site pushes power back. On a generator that second case is not export,
         * it is reverse power -- a fault -- so the wording comes from the
         * attribution rather than being fixed here. */
        const importing = measuredKw !== null && measuredKw > 0.01;
        const exporting = measuredKw !== null && measuredKw < -0.01;

        /* DERIVED, and said so on screen. */
        const parts = [gridKw, generatorKw, solarKw].filter((v) => v !== null);
        const loadKw = parts.length ? parts.reduce((sum, v) => sum + v, 0) : null;

        /* A flow is drawn only when it is both measured and actually carrying
         * something. 0.05 kW rather than zero: a meter's own noise floor should
         * not animate a line on an idle plant. */
        const moving = (value) => value !== null && Math.abs(value) > 0.05;
        const active = {
            solar: moving(solarKw),
            grid: moving(gridKw),
            generator: moving(generatorKw),
            load: moving(loadKw)
        };
        const reversed = { solar: false, grid: exporting, generator: false, load: false };

        const stage = node('div', 'amx-flow-stage');
        stage.append(flowLines('desktop', active, reversed));
        stage.append(flowLines('mobile', active, reversed));

        stage.append(flowNode('solar', 'solar', 'solar', 'Solar', solarKw,
            solarKw === null ? 'not measured' : active.solar ? 'generating' : 'idle'));

        stage.append(flowNode('grid', 'grid', 'grid',
            attribution.node === 'grid' ? attribution.label : 'Grid',
            gridKw,
            gridKw !== null ? attribution.direction(gridKw)
                : onGenerator ? 'not carrying the plant'
                : !attribution.known ? 'source not established'
                : 'no measurement'));

        const hub = node('div', 'amx-flow-hub');
        /* The junction carries no number on purpose: the moment it shows one,
         * the reader has to work out whether it is a fifth measurement or the
         * sum of the other four. */
        const junction = node('div', 'amx-flow-junction');
        junction.setAttribute('aria-label', 'Power junction');
        hub.append(junction);
        stage.append(hub);

        stage.append(flowNode('load', 'load', 'home', 'Site load', loadKw,
            'derived, not metered'));

        stage.append(flowNode('generator', 'generator', 'generator', 'Generator', generatorKw,
            generatorKw !== null
                ? (onGenerator ? attribution.direction(generatorKw)
                    : active.generator ? 'supplying' : 'running, no load')
                /* "not running" was asserted whenever no generator measurement
                 * existed, which on a tariff plant is also what an unestablished
                 * source looks like. Only say it when the controller actually
                 * knows the plant is on the grid. */
                : attribution.node === 'grid' ? 'not running'
                : 'not measured'));

        card.append(stage);

        /*
         * SAID IN WORDS WHEN THE SOURCE IS NOT ESTABLISHED.
         *
         * A diagram that quietly draws the measurement under one node is making
         * a claim. When the controller cannot say which supply is live, the
         * honest rendering is to say so -- not to pick the more likely one.
         */
        if (!attribution.known && measuredKw !== null) {
            card.append(node('p', 'amx-flow-unattributed', attribution.reason));
        }

        const legend = node('div', 'amx-flow-legend');
        legend.setAttribute('aria-label', 'Power source legend');
        legend.append(flowChip('grid', 'Grid flow'),
                      flowChip('solar', 'Solar flow'),
                      flowChip('generator', 'Generator flow'));
        card.append(legend);
        return card;
    }

    /*
     * THE PLANT OVERVIEW.
     *
     * One owner, three sections, five card shapes. It replaces a layout that had
     * grown a flow diagram, an availability list and a chart in three different
     * visual idioms, with a second module appending its own section underneath --
     * which is how a page ends up looking unfinished even though every part of it
     * works.
     *
     * The order answers the questions in the order a person actually asks them:
     *
     *   1. Is this thing working?     the controller's own sentence, first and widest
     *   2. What is the plant doing?   the two numbers the site is about
     *   3. Is everything talking?     one row per device, and how long since it spoke
     *   4. What has it been doing?    the trend
     *
     * A visitor who reads only the first card has the answer they came for. That
     * is the test this page is built to pass, because the complaint it exists to
     * fix is that a working controller and a crashed one looked identical.
     */
    function renderDashboard(payload) {
        const view = byId('operatorDashboardView');
        const cards = window.AutomatrixCards;
        if (!view || !cards) return;

        const status = payload.status || {};
        const telemetry = payload.inverterTelemetry || {};
        const meters = payload.meters?.meters || [];
        const summary = payload.inverters?.summary || {};

        view.replaceChildren();
        /* Alarms stay at the very top and belong to operator-operations.js. An
         * exception is the one thing that outranks "is it working". */
        view.append(attentionHost());

        /* ---- 1. is it working -------------------------------------------- */
        const authority = status.control_authority || {};
        const mode = firmwareWord(authority.mode_label, '');
        const inhibit = firmwareWord(authority.inhibit_reason, '');
        const now = cards.section('Right now', 'What this controller is doing, and whether it can.');

        if (!mode) {
            now.grid.append(cards.status({
                label: 'Controller', mode: 'State not reported', state: 'warn',
                reason: 'The controller did not say what it is doing. That is not the same as a fault, but it is not proof that it is working either.'
            }));
        } else if (authority.command_authority === true) {
            now.grid.append(cards.status({
                label: 'Controller', mode, state: 'ok',
                reason: 'The controller is adjusting the solar inverters as the plant needs.'
            }));
        } else {
            now.grid.append(cards.status({
                label: 'Controller', mode, state: 'warn',
                reason: inhibit || 'The controller is watching the plant but is not adjusting the inverters.'
            }));
        }

        /*
         * HAS THE BOX ON THE WALL BEEN ALL RIGHT?
         *
         * A small factory cannot read a heap fragmentation ratio and should not
         * be asked to. What it needs is the thing an engineer would say out
         * loud: it has been running for eleven days and it did not fall over in
         * the night. That is the difference between trusting the controller and
         * quietly working around it.
         *
         * Shown only when something is worth saying. A permanent green
         * "everything is fine" card trains the reader to skip that corner of the
         * screen, which is precisely where the one abnormal day would appear.
         */
        const health = payload.status?.controller;
        if (health && (health.last_reboot_unexpected === true || health.state !== 'healthy')) {
            const days = finite(health.uptime_ms) ? health.uptime_ms / 86400000 : null;
            const running = days === null ? 'for an unknown time'
                : days >= 1 ? `for ${Math.floor(days)} day${Math.floor(days) === 1 ? '' : 's'}`
                : `for ${Math.max(1, Math.round(health.uptime_ms / 3600000))} hour(s)`;
            now.grid.append(cards.status({
                label: 'This controller',
                mode: health.last_reboot_unexpected ? 'Restarted unexpectedly'
                    : health.state === 'critical' ? 'Needs attention' : 'Worth a look',
                state: health.state === 'critical' || health.last_reboot_unexpected ? 'bad' : 'warn',
                reason: health.last_reboot_unexpected
                    ? `It restarted by itself and has been running ${running} since. `
                      + 'A controller that restarts on its own did so for a reason, '
                      + 'and nobody was watching when it happened. Show this to your engineer.'
                    : `It has been running ${running} and its memory is tighter than it `
                      + 'should be. The plant is unaffected right now; an engineer can see '
                      + 'the detail on the service page.'
            }));
        }

        /*
     * WHAT THE CONTROLLER IS TELLING THE INVERTERS.
     *
     * A different fact from "Solar now": one is the instruction, the other is
     * what the machines are producing. Reading the plant without it means an
     * operator can see production fall and have no way to know whether the
     * controller asked for that or something broke.
     *
     * Summarised across the fleet ONLY when the machines agree. When they carry
     * different limits a single percentage would be a number that is true of no
     * inverter, so the range is shown instead and the fleet table has the rest.
     */
    function fleetCommand(list) {
        const previews = (Array.isArray(list) ? list : [])
            .map((inverter) => inverter.command_preview)
            .filter((preview) => preview && preview.available === true
                && finite(Number(preview.percent)));
        if (!previews.length) return null;
        const values = previews.map((preview) => Number(preview.percent));
        const low = Math.min(...values);
        const high = Math.max(...values);
        /* In force only if EVERY one of them would actually be written. A fleet
         * where half the commands are blocked is not a commanded fleet. */
        const inForce = previews.every((preview) => preview.would_write === true);
        const blocked = previews.find((preview) => preview.would_write !== true);
        return {
            /* Rounded to whole percent: the register is written in whole
             * percent on every profile the product carries. */
            text: Math.round(low) === Math.round(high)
                ? `${Math.round(low)}` : `${Math.round(low)}–${Math.round(high)}`,
            inForce,
            reason: blocked ? (blocked.blocked_by || 'the write gate refuses it') : ''
        };
    }

    /* One inverter's command, for a tile that has room for a few words. Says
     * "would be" when it is not going to be written, because an operator
     * reading "commanded 30%" on a machine nobody is commanding would be
     * reading a decision as an action. */
    function commandWords(preview) {
        if (!preview || preview.available !== true || !finite(Number(preview.percent))) return '';
        const percent = Math.round(Number(preview.percent));
        return preview.would_write === true
            ? ` · commanded ${percent}%` : ` · would command ${percent}%`;
    }

    /* ---- 2. what the plant is doing ----------------------------------- */
        const gridKw = finite(status.grid_power_kw) ? Number(status.grid_power_kw) : null;
        const gridFresh = Boolean(status.meter_online) && !Boolean(status.meter_stale);
        /* Named by the controller, not assumed. See web/source-attribution.js. */
        const supply = window.AutomatrixSource?.attribution(status)
            || { node: 'grid', label: 'Grid', known: true, reason: '', direction: (kw) => (kw > 0.01 ? 'importing' : kw < -0.01 ? 'exporting' : 'balanced') };
        now.grid.append(cards.metric({
            label: supply.known ? `${supply.label} power` : 'Live source power',
            value: gridFresh ? gridKw : null,
            unit: 'kW',
            /* Direction in words, in the vocabulary of whichever source it is:
             * negative is ordinary export on a grid and reverse power -- a
             * fault -- on a generator. The sign alone is a convention nobody
             * told the reader about. */
            foot: !gridFresh ? 'No current measurement'
                : gridKw === null ? 'Not measured'
                : !supply.known ? supply.reason
                : supply.direction(gridKw)
        }));

        const solarKw = cards.measured(telemetry?.summary?.measured_total_kw);
        const commandable = cards.measured(summary.commandable_rated_kw);
        now.grid.append(cards.metric({
            label: 'Solar now',
            value: solarKw,
            unit: 'kW',
            /* Measured against commandable. The gap between them is the one
             * number that explains why a limit did not do what somebody
             * expected, and it appears nowhere else in the product. */
            foot: commandable === null
                ? 'Measured at the inverters'
                : 'This controller can adjust ' + commandable.toFixed(1) + ' kW of it'
        }));
        /*
         * Beside "Solar now" on purpose: instruction and measurement read
         * together, never merged. When the command is computed but not being
         * sent the card says so in the foot rather than presenting a decision
         * that is not in force as though it were.
         */
        const command = fleetCommand(payload.inverters?.inverters || []);
        if (command) {
            /* Built here rather than through cards.metric, which formats to one
             * decimal and takes a single number. Every profile the product
             * carries writes this register in WHOLE percent, and a disagreeing
             * fleet has a range and not a number -- "30.0" would invent a
             * precision the register does not have, and a range could not be
             * shown at all. Same classes, so it reads as one of the row. */
            const card = node('article', 'amx-card');
            card.append(node('span', 'amx-card-label', 'Commanded to the inverters'));
            const line = node('div', 'amx-metric-value');
            line.append(document.createTextNode(command.text));
            line.append(node('span', 'amx-metric-unit', '%'));
            /* Both states are marked, not just the bad one. Green says the value
             * is reaching the inverters, amber says it is only being computed --
             * and the number is shown either way, because what the controller
             * WOULD send is exactly what an engineer checks before switching
             * automatic control on. */
            line.classList.add(command.inForce ? 'is-in-force' : 'is-not-in-force');
            card.append(line);
            card.append(node('span', 'amx-metric-foot', command.inForce
                ? 'The limit the controller is holding the inverters to'
                : `Computed, not being sent: ${command.reason}`));
            now.grid.append(card);
        }
        /*
         * WHAT THE OLD OVERVIEW SHOWED AN ENGINEER.
         *
         * Unlocking Engineering used to reveal a second dashboard below this
         * one. It is gone -- one page for everybody -- and the facts that lived
         * only on it are here, folded away so an operator is not made to read
         * them. An engineer needs MORE detail than an operator, not a different
         * page.
         */
        const overviewDetail = node('div', 'op-more-body');

        /*
         * THE CONTROLLER'S OWN TWO NUMBERS.
         *
         * What it asked for, and what it actually applied after ramping and
         * clamping. They differ during a ramp and whenever a limit bit, and the
         * gap is the first thing to look at when solar did not move the way
         * somebody expected. Neither is measured production -- that is "Solar
         * now" above, and conflating the three is the mistake this page exists
         * to prevent.
         */
        overviewDetail.append(
            detailLine('Requested PV', finite(status.requested_pv_kw)
                ? formatPower(Number(status.requested_pv_kw)) : 'Not available'),
            detailLine('Applied PV', finite(status.applied_pv_kw)
                ? formatPower(Number(status.applied_pv_kw)) : 'Not available')
        );

        /*
         * THE SOURCE EVIDENCE, from the cache app.js already holds.
         *
         * Read rather than fetched: this endpoint is engineering-gated and
         * Modbus-backed, and asking for it twice per refresh would cost a client
         * socket and a transaction for a value already on the machine.
         *
         * The tariff number is here because it is the EVIDENCE, not the verdict.
         * The owner found a plant reading tariff 2 while every screen said grid,
         * and with only the verdict on screen there was nothing to compare it
         * against.
         */
        const utils = window.PvdgSourceDetectionUtils;
        const detection = window.AutomatrixSourceDetectionCache;
        if (utils && detection) {
            const seen = utils.describeSourceDetection(detection);
            overviewDetail.append(
                detailLine('Source evidence', seen.qualityLabel),
                detailLine('Meter tariff', seen.tariffLabel),
                detailLine('PV curtailment', seen.controlConsequence)
            );
        }

        view.append(details('engineering', 'Controller detail', overviewDetail));

        view.append(now);

        /* ---- 2b. where the power is going ---------------------------------
         * Between the control strip and the device list on purpose: it is the
         * shape a customer recognises, and it explains the two numbers above
         * it without repeating them. */
        const power = cards.section('Power flow', 'Where the power is coming from and going, right now.');
        power.grid.append(energyFlow(payload));
        view.append(power);

        /* ---- 3. is everything talking ------------------------------------- */
        const devices = cards.section('Equipment', 'Every device, and how long since it last answered.');
        for (const meter of meters) {
            const runtime = meter.runtime || {};
            const online = runtime.online === true;
            const kw = cards.measured(runtime.active_power_kw);
            devices.grid.append(cards.tile({
                iconName: online ? 'meter' : 'offline',
                name: meter.name || 'Meter',
                detail: (online ? 'Answering' : 'Not answering') + ' · ' + cards.ageWords(runtime.data_age_ms),
                value: kw === null ? '—' : kw.toFixed(1) + ' kW',
                state: online ? 'ok' : 'bad'
            }));
        }
        /* The command belongs to the CONFIGURED inverter, the measurement to the
         * telemetry record; joined by index so a tile can carry both. */
        const previewByIndex = new Map((payload.inverters?.inverters || [])
            .map((item, position) => [Number(item.index ?? position), item.command_preview]));
        for (const inverter of (telemetry.inverters || [])) {
            /* telemetry_valid, not a bare online flag: an inverter that answers
             * and returns nothing usable is not working, and calling that
             * "answering" sends an electrician to the wrong cable. */
            const valid = inverter.telemetry_valid === true;
            const kw = cards.measured(inverter.measured_power_kw);
            devices.grid.append(cards.tile({
                iconName: valid ? 'inverter' : 'offline',
                name: 'Inverter ' + (Number(inverter.index) + 1),
                detail: (valid ? 'Answering' : 'Not answering') + ' · '
                    + cards.ageWords(inverter.telemetry_age_ms) + commandWords(previewByIndex.get(Number(inverter.index))),
                value: kw === null ? '—' : kw.toFixed(1) + ' kW',
                state: valid ? 'ok' : 'bad'
            }));
        }
        devices.grid.append(cards.tile({
            iconName: status.network_online ? 'wifi' : 'offline',
            name: 'Controller network',
            detail: status.network_online
                ? 'Connected to ' + (status.ssid || 'Wi-Fi')
                : 'Check the site network and router power',
            value: status.network_online ? 'Online' : 'Offline',
            state: status.network_online ? 'ok' : 'bad'
        }));
        if (!meters.length && !(telemetry.inverters || []).length) {
            devices.grid.append(cards.tile({
                iconName: 'clipboard', name: 'Nothing commissioned yet',
                detail: 'Add the meter and inverters in Commissioning.',
                value: '—', state: 'idle'
            }));
        }
        view.append(devices);

        /* ---- 4. what it has been doing ------------------------------------ */
        const trend = cards.section('Recent history', 'Grid exchange and solar output over the recent window.');
        const holder = cards.node('article', 'amx-card amx-wide amx-trend');
        holder.append(chartHost());
        trend.grid.append(holder);
        view.append(trend);
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
    /*
     * ONE SUPPLY PER PAGE.
     *
     * Grid power draws the meter measuring the grid; Generator power draws the
     * meter measuring the generator. On a single-meter tariff plant the same
     * instrument moves between the two pages as the tariff register changes,
     * because a generator's 280 kW under a "Grid power" heading is not a layout
     * mistake -- it is the screen stating the plant is importing from the
     * utility while it burns diesel.
     *
     * See web/meter-source-routing.js for the attribution rule.
     */
    function renderMeter(payload, page) {
        const which = page || 'grid';
        const view = byId(which === 'generator' ? 'operatorGeneratorView' : 'operatorMeterView');
        if (!view) return;
        const status = payload.status || {};
        const meters = payload.meters?.meters || [];
        const routing = window.AutomatrixMeterRouting;
        const attributed = routing ? routing.metersFor(which, meters, status) : [];

        /* Nothing to draw is a STATEMENT, not an empty page. On a tariff plant
         * "the meter is on the other page right now" is the normal state for
         * half of every day, and it is completely different from "no meter is
         * commissioned" -- so the page says which. */
        if (routing && !attributed.length) {
            view.replaceChildren();
            const empty = node('article', 'op-card op-empty-state');
            empty.append(node('strong', '', which === 'generator'
                ? 'No generator measurement on this page'
                : 'No grid measurement on this page'));
            empty.append(node('p', '', routing.absence(which, meters, status)));
            view.append(empty);
            return;
        }

        const primary = attributed.length ? attributed[0].meter
            : (meters.find((item) => item.enabled) || meters[0]);
        const runtime = primary?.runtime || {};
        const power = finite(status.grid_power_kw) ? Number(status.grid_power_kw) : runtime.active_power_kw;
        const online = runtime.online === true || (status.meter_online && !status.meter_stale);
        /* Named by the controller. On a tariff plant this meter measures the
         * generator whenever the generator is carrying the site. */
        const supply = window.AutomatrixSource?.attribution(status)
            || { node: 'grid', label: 'Grid', known: true, reason: '', direction: (kw) => (kw > 0.01 ? 'importing' : kw < -0.01 ? 'exporting' : 'balanced') };
        const direction = finite(power)
            ? (supply.known
                ? `${supply.direction(power)}`.replace(/^./, (c) => c.toUpperCase())
                : supply.reason)
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
            /* Named by the page, which is now only reached by a meter the
             * controller attributed to that supply. The heading and the number
             * can no longer disagree. */
            label: which === 'generator' ? 'Generator power carrying the plant'
                : 'Grid power at the point of common coupling',
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
        view.append(overview, meterTable(meters));
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
        const table = inverterTable(inverters, telemetryMap, payload.status);
        /* Said in words under the table, every time a commanded figure is on it.
         * "Now" and "Commanded" are one column apart and look alike, and only
         * one of them is evidence about the machine. */
        table.append(node('p', 'op-table-note',
            'Requested is what the controller wants; Commanded is what the ramp '
            + 'rate allows it to send now. Each is '
            /* Kept on one line: the phrase is asserted verbatim by
             * tests/telemetry_source_contract.py, and a line break inside it
             * hides it from the check while the page still reads correctly. */
            + 'an instruction this controller sent, not a reading'
            + '. Now is what the machine reported.'));
        view.append(armCard(payload), top, table,
                    inverterEngineeringDetail(inverters));
    }

    /*
     * WHAT THE OLD ENGINEERING PAGE SHOWED, FOLDED INTO THIS ONE.
     *
     * Unlocking Engineering used to replace this page with an older one. It is
     * one page now, as the plant overview already was, and these are the facts
     * that lived only on the old one: how the controller reaches each machine,
     * which register it would write, and what the last reads did. An engineer
     * needs MORE detail than an operator, not a different page.
     *
     * Folded away, because an operator is not made to read a PDU address to
     * find out whether the plant is producing.
     */
    function inverterEngineeringDetail(inverters) {
        if (!Array.isArray(inverters) || !inverters.length) return null;
        const body = node('div', 'op-more-body');
        inverters.forEach((inverter, index) => {
            const live = (window.AutomatrixInverterTelemetryCache || {})[Number(inverter.index ?? index)] || null;
            const command = inverter.command || {};
            const endpoint = inverter.endpoint || {};
            body.append(node('div', 'op-more-heading',
                inverter.name || `Inverter ${index + 1}`));
            [['Endpoint', endpoint.host
                ? `${endpoint.host}:${endpoint.port ?? '--'} · unit ${endpoint.unit_id ?? '--'}`
                : 'Not available'],
             ['Limit register', command.limit_pdu_address != null
                ? `FC${command.function ?? '--'} · PDU ${command.limit_pdu_address}`
                : 'Not available'],
             ['Allowed range', command.minimum_percent != null
                ? `${command.minimum_percent}–${command.maximum_percent}%` : 'Not available'],
             /* From the telemetry read, which is the only place these exist. */
             ['Identity', !live ? 'Not read yet'
                : live.identity_supported === true
                    ? (live.identity_verified === true ? 'Verified' : 'Mismatch / unavailable')
                    : 'Not supported'],
             ['Readback', !live ? 'Not read yet'
                : live.has_readback === true
                    ? `${formatPercent(live.readback_percent)} · ${formatAge(live.readback_age_ms)}`
                    : 'Unavailable'],
             ['Reads', !live ? 'Not read yet'
                : `${live.read_successes ?? 0} ok · ${live.read_errors ?? 0} err`
                  + ` · ${live.consecutive_read_failures ?? 0} consec`],
             ['Last error', live?.last_error_name || live?.last_error || 'None']
            ].forEach(([label, value]) => body.append(detailLine(label, value)));
        });
        return details('engineering', 'How the controller reaches these machines', body);
    }

    /*
     * THE SWITCH, ON THE PAGE THAT SHOWS WHAT IT WILL DO.
     *
     * Automatic control had no control anywhere in the interface. The engine was
     * complete -- step, ramp, readback confirmation, safe zero -- and unreachable,
     * so the loop had never run end to end. The owner asked for the switch here,
     * beside the fleet it commands and the percentage it would send.
     *
     * THE CONTROLLER IS THE AUTHORITY, NOT THIS CARD. The engine re-evaluates
     * the commissioning gate every cycle and withholds command authority itself,
     * so this never reports success on its own: it sends the request and then
     * re-reads the controller, and what is shown is what the controller says. A
     * card that echoed the request would tell an engineer the plant was armed
     * when it had been refused -- the same defect as drawing a commanded
     * setpoint as though it were a measurement.
     *
     * DISARMING IS NEVER DISABLED. Being unable to command is recoverable;
     * being unable to stop commanding is not. The button is only ever disabled
     * in the arming direction.
     */
    function armCard(payload) {
        const status = payload.status || {};
        const enabled = status.control_enabled === true;
        const summary = payload.inverters?.summary || {};
        const commandable = Number(summary.commandable_rated_kw) || 0;

        const card = node('article', 'op-card');
        const head = node('div', 'op-measure-head');
        head.append(node('span', 'op-measure-label', 'Automatic control'));
        /* The product's own control vocabulary, not two words invented here.
         * "Armed" and "Off" read fine and are a fifth and sixth name for states
         * the rest of the product already calls Active and Standby -- and a
         * reader who learns a word on one page must find it on the next. */
        head.append(node('span', `op-state-pill ${enabled ? 'good' : ''}`,
            enabled ? stateWord('control', 'active', 'Active')
                : stateWord('control', 'standby', 'Standby')));
        card.append(head);

        card.append(node('p', 'op-card-note', enabled
            ? 'The controller is adjusting the inverters from its own measurements. '
              + 'Turning it off stops that immediately; the last limit stays in force on '
              + 'each machine until its own fail-safe expires.'
            : commandable > 0
                ? `The controller is watching only. It can move ${formatPower(commandable)} `
                  + 'of inverter capacity when it is on.'
                : 'No inverter is eligible to be commanded, so starting it would '
                  + 'change nothing. The table below says why for each machine.'));

        /*
         * SAY THE PREREQUISITE BEFORE THE PRESS, NOT AFTER IT.
         *
         * Arming is an engineering write. Without a session the controller
         * answers "Engineering authentication is required", which the operator
         * only discovered by pressing the button and reading a refusal -- and a
         * refusal after the act reads as a fault rather than as a step missed.
         */
        const unlocked = window.AutomatrixEngineeringAccess?.isAuthenticated() === true;
        if (!unlocked) {
            card.append(node('p', 'op-card-note',
                'Starting and stopping it is an engineering action. Unlock '
                + 'Engineering access first — the link is at the foot of the menu.'));
        }

        const actions = node('div', 'op-card-actions');
        const button = node('button', enabled ? 'button secondary' : 'button primary',
            enabled ? 'Stop automatic control' : 'Start automatic control');
        button.type = 'button';
        /* Never disabled while armed: being unable to stop is the one failure
         * that is not recoverable, so a locked session must not take the stop
         * away either. */
        button.disabled = enabled ? false : (commandable <= 0 || !unlocked);
        const message = node('span', 'action-message');
        message.setAttribute('role', 'status');

        button.addEventListener('click', async () => {
            if (!enabled && !window.confirm(
                'Start automatic control?' + String.fromCharCode(10, 10)
                + 'The controller will begin commanding the solar inverters from its own '
                + 'measurements. It will write a power limit to real equipment.')) return;
            button.disabled = true;
            message.className = 'action-message';
            message.textContent = enabled ? 'Stopping…' : 'Starting…';
            try {
                window.AutomatrixFetch?.invalidate();
                await api('/api/control/enable', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ enabled: !enabled })
                });
                /* Re-read, and from the controller rather than from anything
                 * held: it may have refused, and it is the only honest source
                 * for what actually happened. */
                window.AutomatrixFetch?.invalidate();
                await refreshAll();
            } catch (error) {
                message.textContent = `Refused: ${error.message}`;
                message.className = 'action-message bad';
                button.disabled = false;
            }
        });

        actions.append(button, message);
        card.append(actions);
        return card;
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

    /*
     * TWO COMMANDS, BECAUSE THE CONTROLLER MAKES TWO.
     *
     * "Requested" is the fleet target the control loop computed from the grid
     * or generator reading. "Commanded" is what is actually sent after the ramp
     * rate has limited how fast that target may be approached.
     *
     * They are equal at rest and differ during a ramp, and the difference is
     * the ramp doing its job -- which was visible nowhere. An engineer watching
     * only the commanded figure sees the plant move slowly and cannot tell
     * whether the controller asked for slow or the ramp made it slow.
     */
    function inverterTable(inverters, telemetryMap, status) {
        /* Shared out across the machines that carry a rating, the same basis
         * the controller uses. */
        const fleetRatedKw = (Array.isArray(inverters) ? inverters : [])
            .reduce((sum, item) => sum + (Number(item.rated_kw) || 0), 0);
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
                /* What the controller has DECIDED for this machine, which is a
                 * different fact from what the machine is producing and was
                 * visible nowhere. It is known even when the inverter is
                 * silent -- which is exactly when somebody asks. */
                preview: inverter.command_preview || null,
                /* Failed and offline first: this table is read to find the unit
                 * that stopped, not to admire the ones that did not. */
                rank: online ? 2 : inverter.enabled ? 0 : 1
            };
        }).sort((a, b) => a.rank - b.rank);

        const table = node('table', 'op-table');
        const headRow = node('tr');
        /* "Commanded" is the controller's own decision for this machine; "Now"
         * is what the machine reports. Adjacent on purpose, and never merged:
         * one is an instruction and the other is a measurement. */
        ['Inverter', 'State', 'Now', 'Requested', 'Commanded', 'Rated', 'Use', 'Last update']
            .forEach((label) => headRow.append(node('th', '', label)));
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
            /* The percentage the controller would send, and whether it will
             * actually go. An em dash when the profile describes no command, so
             * "not commandable" and "commanded to zero" stay distinguishable. */
            /* The fleet's requested target, shared out the same way the command
             * is: proportional to this machine's rating. Before the ramp. */
            const requestedFleetKw = finite(status?.requested_pv_kw)
                ? Number(status.requested_pv_kw) : null;
            const requested = node('td', 'op-num');
            if (requestedFleetKw !== null && fleetRatedKw > 0 && entry.rated > 0) {
                const shareKw = requestedFleetKw * entry.rated / fleetRatedKw;
                requested.textContent = `${Math.round(100 * shareKw / entry.rated)} %`;
                requested.title = `${shareKw.toFixed(2)} kW of this machine's share, `
                    + 'before the ramp rate limits how fast it may be approached';
            } else {
                requested.textContent = '—';
            }
            row.append(requested);

            const commanded = node('td', 'op-num');
            const preview = entry.preview;
            if (preview && preview.available && finite(Number(preview.percent))) {
                commanded.textContent = `${Number(preview.percent).toFixed(0)} %`;
                if (!preview.would_write) {
                    commanded.classList.add('op-num-pending');
                    commanded.title = `Not written: ${preview.blocked_by || 'the write gate refuses it'}`;
                }
            } else {
                commanded.textContent = '—';
                if (preview && preview.blocked_by) commanded.title = preview.blocked_by;
            }
            row.append(commanded);
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
        /*
         * THE PLANT OVERVIEW IS ONE PAGE FOR EVERYBODY.
         *
         * Unlocking Engineering used to reveal a second, older dashboard below
         * this one -- its own grid power card, its own source banner, its own
         * health list -- so the screen a site is run from changed shape at the
         * moment an engineer signed in, and the two halves stated the same
         * quantities in different words. An engineer needs MORE detail than an
         * operator, not a different page.
         *
         * The three facts that lived only on the old dashboard (requested PV,
         * applied PV, and the source evidence with its tariff) are now in the
         * engineering block above, which is where the rest of the engineering
         * detail already was.
         *
         * The generator page joins them for a different reason: it has no legacy
         * content at all, so the swap left an engineer with a title and an empty
         * page while the overview beside it read GENERATOR 91.0 kW. Nothing here
         * to hide -- it is listed so that anything added later is hidden too,
         * rather than appearing beside the product view as a second opinion.
         *
         * The remaining pages keep the old behaviour; they have not been given
         * an engineering block to move their detail into, and hiding them first
         * would delete it.
         */
        ['dashboard', 'inverters', 'generator'].forEach((name) => {
            const page = document.querySelector(`[data-page="${name}"]`);
            if (!page) return;
            Array.from(page.children).forEach((child) => {
                if (!child.classList.contains('operator-product-view')
                    && !child.classList.contains('page-intro')) {
                    child.classList.add('operator-legacy-hidden');
                }
            });
        });
        if (!isOperator()) return;
        /* 'control' is gone with its page; naming a page that no longer exists
         * reads as coverage this function does not have. */
        ['meters', 'system'].forEach((name) => {
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

    /* THE PRODUCT VIEW IS NOT AN OPERATOR PRIVILEGE.
     *
     * refreshAll() and renderCurrent() used to return early during an
     * engineering session, so signing in REMOVED the live status: the Solar
     * inverters page asks "how much solar is available", and an engineer
     * reading it saw setup forms and no production figure anywhere on the
     * screen. The one number the page is named after was visible only to the
     * reader who could not act on it.
     *
     * Every endpoint here is read-only and operator-scoped, so there is nothing
     * to withhold. What stays gated is hideLegacyOperatorContent(), which
     * SUPPRESSES the engineering panels -- that is a real access distinction
     * and is untouched. An engineer now sees the product view and the
     * engineering panels; an operator sees the product view alone. */
    /* The routes this module actually draws. The five-second timer used to be
     * cancelled in practice by the isOperator() guard above; removing that
     * guard would otherwise have added four requests every five seconds on
     * commissioning, network setup and the engineering workspace - pages this
     * module renders nothing on, competing for the four client sockets the
     * controller has. It polls where it draws, and nowhere else. */
    const PRODUCT_ROUTES = new Set(['dashboard', 'meters', 'generator', 'inverters', 'control', 'system']);

    async function refreshAll() {
        if (state.busy || !PRODUCT_ROUTES.has(route())) return;
        state.busy = true;
        const shellRefresh = byId('refreshButton');
        if (shellRefresh) shellRefresh.disabled = true;
        try {
            /*
             * THREE REQUESTS, NOT FOUR.
             *
             * web/inverter-telemetry.js already reads /api/inverter-telemetry
             * every two seconds and publishes what it got. Asking for it again
             * here every five seconds was a second request for a fact already
             * in the browser -- and on a controller with very few client
             * sockets, a duplicate does not waste its own socket, it occupies
             * one another module is waiting for.
             *
             * Falls back to fetching it only if that module has not answered
             * yet, so nothing depends on load order.
             */
            const cached = window.AutomatrixInverterTelemetryCache;
            /* app.js reads /api/status every two seconds. Anything it took
             * within the last three is newer than this view's own five-second
             * cadence, so asking again would only cost a socket. Older than
             * that and it is fetched, so a stalled app.js cannot freeze this
             * screen on an old reading. */
            const heldStatus = window.AutomatrixStatusCache;
            const statusIsFresh = heldStatus && Date.now() - heldStatus.at <= 3000;
            const [status, meters, inverters, inverterTelemetry] = await Promise.all([
                statusIsFresh ? Promise.resolve(heldStatus.payload) : api('/api/status'),
                api('/api/meters'), api('/api/inverters'),
                cached ? Promise.resolve({ inverters: Object.values(cached),
                                           summary: window.AutomatrixInverterTelemetrySummary || {} })
                       : api('/api/inverter-telemetry')
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
        if (!state.lastPayload) return;
        const current = route();
        if (current === 'dashboard') renderDashboard(state.lastPayload);
        else if (current === 'meters') renderMeter(state.lastPayload, 'grid');
        else if (current === 'generator') renderMeter(state.lastPayload, 'generator');
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
        if (!state.lastPayload) refreshAll();
    }

    function start() {
        ensureViews();
        updateLanguage();
        hideLegacyOperatorContent();
        bindShellRefresh();
        onRoute();
        window.addEventListener('hashchange', onRoute);
        /* Unlocking Engineering changes what this view may offer -- the
         * automatic-control button among it -- so redraw when the scope
         * changes. Without this an engineer signs in and the button stays
         * disabled until the next poll, which reads as a broken control. */
        window.AutomatrixEngineeringAccess?.onScopeChange(() => renderCurrent());
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
