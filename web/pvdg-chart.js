/* Automatrix PV-DG Controller - the one time-series chart.
 *
 * This module replaces the two chart implementations the product used to carry:
 * the browser-session sparkline in operator-view.js and the controller-history
 * sparkline in operator-operations.js. Both plotted array position on X, both
 * dropped missing samples before drawing, and both therefore drew a straight
 * line across an interval in which nothing was measured.
 *
 * On a reverse-power controller that is not a cosmetic defect. A gap that is
 * interpolated - or worse, an unmeasured sample plotted at 0 kW - reads as "no
 * power flowing" when the truth is "nobody knows". The whole product exists to
 * stop power flowing the wrong way, so the rules below are structural:
 *
 *   1. X is a real timestamp. Never an array index.
 *   2. A missing sample is null and stays null. It is never interpolated across
 *      and never coerced to zero: it is drawn as a visible gap.
 *   3. Zero is always inside the Y domain, so import and export are always on
 *      opposite, labelled sides of a drawn zero line.
 *   4. Statistics are computed from the measured samples only, and the count of
 *      samples they are computed from is shown next to them.
 *
 * The pure functions are exported for web/tests/chart-utils.test.js; the DOM
 * part is only built when a document exists.
 */
(function (root, factory) {
    const api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.PvdgChart = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
    'use strict';

    /* The controller accepts exactly these three range values and silently
     * substitutes 15m for anything else, so anything else must never be sent. */
    const RANGES = [
        { value: '15m', label: '15 min', ms: 900000, intervalMs: 5000 },
        { value: '1h', label: '1 hour', ms: 3600000, intervalMs: 60000 },
        { value: '24h', label: '24 hours', ms: 86400000, intervalMs: 60000 }
    ];

    /* Validated with the data-visualisation palette checker against both the
     * dark panel (#0e2035) and the light surface: lightness band, chroma floor,
     * CVD separation, normal-vision separation and 3:1 contrast all pass.
     * Colour is never the only difference - each slot also owns a dash pattern
     * and a legend glyph shape (WCAG 2.2 1.4.1). */
    const SERIES_SLOTS = [
        { color: '#3f97ec', light: '#1f7ae0', dash: '', glyph: 'circle' },
        { color: '#c9761a', light: '#c2660d', dash: '7 4', glyph: 'triangle' },
        { color: '#a37ae0', light: '#8a5ad6', dash: '2 3', glyph: 'square' },
        { color: '#2f9b7a', light: '#0f8a63', dash: '10 3 2 3', glyph: 'diamond' }
    ];

    const TIME_STEPS = [
        1000, 5000, 15000, 30000, 60000, 300000, 600000, 900000,
        1800000, 3600000, 7200000, 10800000, 21600000, 43200000, 86400000
    ];

    function normalizeRange(value) {
        const found = RANGES.find((item) => item.value === value);
        return found ? found.value : RANGES[0].value;
    }

    function rangeInfo(value) {
        return RANGES.find((item) => item.value === normalizeRange(value));
    }

    /* A sample value is missing unless it is a real finite number. null, an
     * absent key, '' and true/false are all "not measured", never 0. */
    function measured(value) {
        if (value === null || value === undefined || value === '') return null;
        if (typeof value === 'boolean') return null;
        const number = Number(value);
        return Number.isFinite(number) ? number : null;
    }

    /* Samples carry age_ms, not a clock reading, so the wall-clock instant is
     * reconstructed from the moment the response was received. Points are
     * sorted by time and de-duplicated: a timestamp may only appear once, which
     * is what stops a refresh that overlaps a render from drawing a sample
     * twice. */
    function toPoints(samples, key, options) {
        const list = Array.isArray(samples) ? samples : [];
        const settings = options || {};
        const base = Number.isFinite(Number(settings.now)) ? Number(settings.now) : Date.now();
        const points = [];
        for (let index = 0; index < list.length; index += 1) {
            const sample = list[index] || {};
            const age = Number(sample.age_ms);
            if (!Number.isFinite(age)) continue;
            points.push({ t: base - age, v: measured(sample[key]), sample });
        }
        points.sort((a, b) => a.t - b.t);
        const unique = [];
        for (let index = 0; index < points.length; index += 1) {
            const point = points[index];
            const previous = unique[unique.length - 1];
            if (previous && previous.t === point.t) unique[unique.length - 1] = point;
            else unique.push(point);
        }
        return unique;
    }

    function gapLimit(intervalMs) {
        const interval = Number(intervalMs);
        return Number.isFinite(interval) && interval > 0 ? interval * 1.75 : Infinity;
    }

    /* Contiguous runs of measured samples. A null value ends a run, and so does
     * a time step longer than the tolerated interval: neither is bridged. */
    function segments(points, gapMs) {
        const limit = Number.isFinite(gapMs) && gapMs > 0 ? gapMs : Infinity;
        const list = Array.isArray(points) ? points : [];
        const out = [];
        let run = null;
        for (let index = 0; index < list.length; index += 1) {
            const point = list[index];
            if (point.v === null) { run = null; continue; }
            if (run && (point.t - run[run.length - 1].t) > limit) run = null;
            if (!run) { run = []; out.push(run); }
            run.push(point);
        }
        return out;
    }

    /* The intervals a run does not cover: an explicit null run ("missing") or a
     * silence longer than the sampling interval ("interval"). These are drawn,
     * counted and announced. */
    function gaps(points, gapMs) {
        const limit = Number.isFinite(gapMs) && gapMs > 0 ? gapMs : Infinity;
        const list = Array.isArray(points) ? points : [];
        const out = [];
        let previous = null;
        let missingFrom = null;
        for (let index = 0; index < list.length; index += 1) {
            const point = list[index];
            if (point.v === null) {
                if (missingFrom === null) missingFrom = previous ? previous.t : point.t;
                continue;
            }
            if (missingFrom !== null) {
                out.push({ from: missingFrom, to: point.t, reason: 'missing' });
                missingFrom = null;
            } else if (previous && (point.t - previous.t) > limit) {
                out.push({ from: previous.t, to: point.t, reason: 'interval' });
            }
            previous = point;
        }
        if (missingFrom !== null) out.push({ from: missingFrom, to: list[list.length - 1].t, reason: 'missing' });
        return out;
    }

    /* Statistics over the measured samples only. `current` is the most recent
     * measured value; `currentMissing` says the newest sample of all was not
     * measured, so the caller can refuse to present a stale value as current. */
    function stats(points) {
        const list = Array.isArray(points) ? points : [];
        let min = Infinity;
        let max = -Infinity;
        let sum = 0;
        let count = 0;
        let current = null;
        let currentAt = null;
        for (let index = 0; index < list.length; index += 1) {
            const value = list[index].v;
            if (value === null) continue;
            count += 1;
            sum += value;
            if (value < min) min = value;
            if (value > max) max = value;
            current = value;
            currentAt = list[index].t;
        }
        const last = list.length ? list[list.length - 1] : null;
        return {
            samples: list.length,
            count,
            missing: list.length - count,
            min: count ? min : null,
            max: count ? max : null,
            average: count ? sum / count : null,
            current,
            currentAt,
            currentMissing: Boolean(last && last.v === null),
            coverage: list.length ? count / list.length : 0
        };
    }

    function niceStep(rough) {
        const value = Number(rough);
        if (!Number.isFinite(value) || value <= 0) return 1;
        const exponent = Math.floor(Math.log10(value));
        const base = Math.pow(10, exponent);
        const scaled = value / base;
        const step = scaled <= 1 ? 1 : scaled <= 2 ? 2 : scaled <= 2.5 ? 2.5 : scaled <= 5 ? 5 : 10;
        return step * base;
    }

    /* Zero is inside the domain by default. On a chart whose subject is the
     * direction of power flow, an axis that does not contain zero cannot show
     * which side of zero the plant is on. */
    function niceScale(min, max, options) {
        const settings = options || {};
        const target = Math.max(2, Math.round(settings.ticks || 5));
        let low = Number.isFinite(min) ? min : 0;
        let high = Number.isFinite(max) ? max : 0;
        if (high < low) { const swap = low; low = high; high = swap; }
        if (settings.includeZero !== false) { low = Math.min(low, 0); high = Math.max(high, 0); }
        if (high - low < 1e-9) { low -= 1; high += 1; }
        const step = niceStep((high - low) / target);
        const lo = Math.floor(low / step) * step;
        const hi = Math.ceil(high / step) * step;
        const ticks = [];
        const epsilon = step / 1e6;
        for (let value = lo; value <= hi + epsilon; value += step) {
            ticks.push(Math.abs(value) < epsilon ? 0 : Number(value.toFixed(6)));
        }
        return { min: lo, max: hi, step, ticks };
    }

    /* Bucketing is done in local time (offsetMs) so that an hourly tick lands on
     * the hour the operator's clock shows, not on a UTC boundary. */
    function bucketTimestamp(time, stepMs, offsetMs) {
        const t = Number(time);
        const step = Number(stepMs);
        if (!Number.isFinite(t) || !Number.isFinite(step) || step <= 0) return null;
        const offset = Number.isFinite(Number(offsetMs)) ? Number(offsetMs) : 0;
        return Math.floor((t + offset) / step) * step - offset;
    }

    function timeStep(spanMs, target) {
        const want = Math.max(2, Math.round(target || 5));
        const rough = Math.max(1, Number(spanMs) || 1) / want;
        for (let index = 0; index < TIME_STEPS.length; index += 1) {
            if (TIME_STEPS[index] >= rough) return TIME_STEPS[index];
        }
        return TIME_STEPS[TIME_STEPS.length - 1];
    }

    function timeTicks(from, to, target, offsetMs) {
        const start = Number(from);
        const end = Number(to);
        if (!Number.isFinite(start) || !Number.isFinite(end) || end <= start) return [];
        const step = timeStep(end - start, target);
        let tick = bucketTimestamp(start, step, offsetMs);
        if (tick < start) tick += step;
        const ticks = [];
        while (tick <= end && ticks.length < 64) { ticks.push(tick); tick += step; }
        return ticks;
    }

    function scale(domainLow, domainHigh, rangeLow, rangeHigh) {
        const span = domainHigh - domainLow;
        if (!Number.isFinite(span) || span === 0) {
            return function () { return (rangeLow + rangeHigh) / 2; };
        }
        return function (value) {
            return rangeLow + ((value - domainLow) / span) * (rangeHigh - rangeLow);
        };
    }

    function formatKw(value) {
        if (value === null || value === undefined || !Number.isFinite(Number(value))) return 'No measurement';
        const number = Number(value);
        return `${number.toFixed(Math.abs(number) >= 100 ? 1 : 2)} kW`;
    }

    function pad2(value) { return String(value).padStart(2, '0'); }

    function formatClock(time, spanMs) {
        const date = new Date(Number(time));
        if (Number.isNaN(date.getTime())) return '';
        const base = `${pad2(date.getHours())}:${pad2(date.getMinutes())}`;
        return Number(spanMs) <= 900000 ? `${base}:${pad2(date.getSeconds())}` : base;
    }

    function formatSpan(ms) {
        const value = Number(ms);
        if (!Number.isFinite(value) || value < 0) return 'an unknown period';
        if (value < 60000) return `${Math.max(1, Math.round(value / 1000))} seconds`;
        if (value < 3600000) return `${Math.round(value / 60000)} minutes`;
        return `${(value / 3600000).toFixed(1)} hours`;
    }

    /* Direction words, so "positive" never has to be inferred from the picture. */
    function describeFlow(value, meaning) {
        if (value === null || !Number.isFinite(Number(value))) return 'not measured';
        const number = Number(value);
        if (!meaning) return formatKw(number);
        if (Math.abs(number) < 0.005) return `${formatKw(number)} (balanced)`;
        return `${formatKw(number)} (${number > 0 ? meaning.positive : meaning.negative})`;
    }

    /* The text a screen reader gets instead of the picture. It states the
     * coverage explicitly, because "no data" and "zero power" are the two things
     * this chart must never blur. */
    function summaryText(entries, context) {
        const settings = context || {};
        const rangeLabel = settings.rangeLabel || 'the selected range';
        const parts = [];
        (entries || []).forEach((entry) => {
            const figures = entry.stats || {};
            if (!figures.count) {
                parts.push(`${entry.label}: no measured samples over ${rangeLabel}.`);
                return;
            }
            let sentence = `${entry.label} over ${rangeLabel}: current ${figures.currentMissing ? 'not measured' : formatKw(figures.current)}`;
            sentence += `, minimum ${formatKw(figures.min)}, average ${formatKw(figures.average)}, peak ${formatKw(figures.max)}`;
            sentence += `, from ${figures.count} measured samples`;
            if (figures.missing > 0) sentence += ` with ${figures.missing} not measured`;
            sentence += '.';
            if (entry.gaps && entry.gaps.length) {
                const longest = entry.gaps.reduce((best, gap) => Math.max(best, gap.to - gap.from), 0);
                sentence += ` ${entry.gaps.length} gap${entry.gaps.length === 1 ? '' : 's'} in the record, the longest ${formatSpan(longest)}; the line is broken there and no value is drawn.`;
            }
            parts.push(sentence);
        });
        if (!parts.length) parts.push('No series is currently shown.');
        return parts.join(' ');
    }

    /* Runs of samples for which a flag holds - used for communication-loss
     * bands, which are a property of the record, not of the value. */
    function flagRuns(points, test) {
        const list = Array.isArray(points) ? points : [];
        const out = [];
        let open = null;
        for (let index = 0; index < list.length; index += 1) {
            const point = list[index];
            const hit = Boolean(test(point.sample || {}, point));
            if (hit && !open) open = { from: point.t, to: point.t };
            else if (hit && open) open.to = point.t;
            else if (!hit && open) { open.to = point.t; out.push(open); open = null; }
        }
        if (open) out.push(open);
        return out;
    }

    /* Points at which a recorded state changed between two consecutive samples. */
    function transitions(points, read) {
        const list = Array.isArray(points) ? points : [];
        const out = [];
        let previous;
        for (let index = 0; index < list.length; index += 1) {
            const value = read(list[index].sample || {}, list[index]);
            if (index > 0 && value !== previous) out.push({ t: list[index].t, from: previous, to: value });
            previous = value;
        }
        return out;
    }

    const pure = {
        RANGES,
        SERIES_SLOTS,
        normalizeRange,
        rangeInfo,
        measured,
        toPoints,
        gapLimit,
        segments,
        gaps,
        stats,
        niceStep,
        niceScale,
        bucketTimestamp,
        timeStep,
        timeTicks,
        scale,
        formatKw,
        formatClock,
        formatSpan,
        describeFlow,
        summaryText,
        flagRuns,
        transitions
    };

    if (typeof document === 'undefined') return pure;

    /* ------------------------------------------------------------------ view */

    const NS = 'http://www.w3.org/2000/svg';
    /* The drawing area is what this component exists to enlarge. The card it
     * replaces was 220 px tall with a 92 px sparkline - 41.8% of the card was
     * the picture and only 34.5% was the plotted rectangle. Here the gutters
     * are the only figure space that is not plot. */
    const MARGIN = { top: 12, right: 14, bottom: 28, left: 52 };
    let uid = 0;

    function el(tag, className, text) {
        const item = document.createElement(tag);
        if (className) item.className = className;
        if (text != null) item.textContent = String(text);
        return item;
    }

    function svg(tag, attributes) {
        const item = document.createElementNS(NS, tag);
        if (attributes) {
            Object.keys(attributes).forEach((name) => {
                if (attributes[name] != null) item.setAttribute(name, String(attributes[name]));
            });
        }
        return item;
    }

    function glyphPath(shape) {
        if (shape === 'triangle') return 'M6 1 11 10H1z';
        if (shape === 'square') return 'M2 2h8v8H2z';
        if (shape === 'diamond') return 'M6 1 11 6 6 11 1 6z';
        return 'M6 1a5 5 0 1 0 .01 0z';
    }

    function nearestIndex(times, value) {
        if (!times.length) return -1;
        let low = 0;
        let high = times.length - 1;
        while (low < high) {
            const mid = (low + high) >> 1;
            if (times[mid] < value) low = mid + 1; else high = mid;
        }
        if (low > 0 && Math.abs(times[low - 1] - value) <= Math.abs(times[low] - value)) return low - 1;
        return low;
    }

    function create(options) {
        const config = Object.assign({
            title: 'Power trend',
            description: '',
            height: 500,
            ranges: true,
            range: '15m',
            series: [],
            overlays: ['gaps', 'zero', 'comms', 'alarms', 'control'],
            onRangeChange: null
        }, options || {});

        uid += 1;
        const id = `pvc${uid}`;
        const view = {
            range: normalizeRange(config.range),
            samples: [],
            intervalMs: null,
            receivedAt: Date.now(),
            state: 'loading',
            message: '',
            hidden: Object.create(null),
            cursor: -1,
            width: 0,
            height: 0,
            frameQueued: false,
            series: config.series.slice(0, SERIES_SLOTS.length)
        };

        const card = el('article', 'op-card pvc-card');
        card.style.setProperty('--pvc-plot-height', `${Math.round(config.height)}px`);

        const head = el('div', 'pvc-head');
        const heading = el('div', 'pvc-heading');
        heading.append(el('h3', 'pvc-title', config.title));
        if (config.description) heading.append(el('p', 'pvc-subtitle', config.description));
        head.append(heading);

        const rangeGroup = el('div', 'op-range-selector pvc-ranges');
        rangeGroup.setAttribute('role', 'group');
        rangeGroup.setAttribute('aria-label', 'Chart time range');
        const rangeButtons = new Map();
        if (config.ranges) {
            RANGES.forEach((entry) => {
                const button = el('button', 'op-range-button', entry.label);
                button.type = 'button';
                button.addEventListener('click', () => {
                    if (view.range === entry.value) return;
                    view.range = entry.value;
                    paintRanges();
                    if (typeof config.onRangeChange === 'function') config.onRangeChange(entry.value);
                });
                rangeButtons.set(entry.value, button);
                rangeGroup.append(button);
            });
            head.append(rangeGroup);
        }
        card.append(head);

        const legend = el('div', 'pvc-legend');
        legend.setAttribute('role', 'group');
        legend.setAttribute('aria-label', 'Series visibility');
        card.append(legend);

        const figure = el('div', 'pvc-figure');
        figure.tabIndex = 0;
        figure.setAttribute('role', 'img');
        figure.setAttribute('aria-describedby', `${id}-summary`);
        const canvas = svg('svg', { class: 'pvc-svg', focusable: 'false', 'aria-hidden': 'true' });
        figure.append(canvas);
        const tip = el('div', 'pvc-tip');
        tip.hidden = true;
        figure.append(tip);
        const overlayMessage = el('div', 'pvc-overlay');
        overlayMessage.hidden = true;
        figure.append(overlayMessage);
        card.append(figure);

        const quality = el('div', 'pvc-quality');
        card.append(quality);

        const statsRow = el('div', 'pvc-stats');
        card.append(statsRow);

        const note = el('p', 'pvc-note');
        card.append(note);

        const summary = el('p', 'pvc-summary');
        summary.id = `${id}-summary`;
        card.append(summary);

        const live = el('p', 'pvc-live');
        live.setAttribute('aria-live', 'polite');
        card.append(live);

        const table = el('details', 'pvc-table');
        const tableSummary = el('summary', '', 'Show sample values');
        const tableBody = el('div', 'pvc-table-body');
        table.append(tableSummary, tableBody);
        table.addEventListener('toggle', () => { if (table.open) buildTable(); else tableBody.replaceChildren(); });
        card.append(table);

        function paintRanges() {
            rangeButtons.forEach((button, value) => {
                const active = value === view.range;
                button.classList.toggle('active', active);
                button.setAttribute('aria-pressed', active ? 'true' : 'false');
            });
        }
        paintRanges();

        function activeSeries() {
            return view.series.filter((entry) => !view.hidden[entry.key]);
        }

        /* The stroke colour is carried by a class, not an inline attribute, so
         * the light and dark steps of the validated palette both come from the
         * component stylesheet and neither has to be recomputed in script. */
        function slotFor(index) {
            const slot = SERIES_SLOTS[index % SERIES_SLOTS.length];
            return { index: index % SERIES_SLOTS.length, dash: slot.dash, glyph: slot.glyph, className: `pvc-s${index % SERIES_SLOTS.length}` };
        }

        function model() {
            const info = rangeInfo(view.range);
            const interval = Number.isFinite(Number(view.intervalMs)) && Number(view.intervalMs) > 0
                ? Number(view.intervalMs)
                : info.intervalMs;
            const limit = gapLimit(interval);
            const entries = view.series.map((entry, index) => {
                const points = toPoints(view.samples, entry.key, { now: view.receivedAt });
                return {
                    key: entry.key,
                    label: entry.label,
                    meaning: entry.meaning || null,
                    slot: slotFor(index),
                    hidden: Boolean(view.hidden[entry.key]),
                    points,
                    segments: segments(points, limit),
                    gaps: gaps(points, limit),
                    stats: stats(points)
                };
            });
            const times = entries.length ? entries[0].points.map((point) => point.t) : [];
            const anchor = entries.length ? entries[0].points : [];
            return { info, interval, limit, entries, times, anchor };
        }

        function buildLegend(built) {
            legend.replaceChildren();
            /* A legend is always present for two or more series; with one series
             * the single button doubles as its visibility control and the
             * overlay key beside it explains the bands. */
            built.entries.forEach((entry) => {
                const button = el('button', 'pvc-legend-item');
                button.type = 'button';
                button.setAttribute('aria-pressed', entry.hidden ? 'false' : 'true');
                button.classList.toggle('off', entry.hidden);
                const mark = svg('svg', { class: 'pvc-legend-mark', viewBox: '0 0 24 12', 'aria-hidden': 'true' });
                mark.append(svg('path', {
                    d: 'M0 6h24',
                    class: `pvc-line ${entry.slot.className}`,
                    'stroke-width': 2.5,
                    'stroke-dasharray': entry.slot.dash || null
                }));
                mark.append(svg('path', { d: glyphPath(entry.slot.glyph), class: `pvc-fill ${entry.slot.className}`, transform: 'translate(6 0)' }));
                button.append(mark, el('span', '', entry.label));
                button.addEventListener('click', () => {
                    if (!entry.hidden && activeSeries().length <= 1) return;
                    view.hidden[entry.key] = !entry.hidden;
                    view.cursor = -1;
                    render();
                });
                legend.append(button);
            });
            const key = el('div', 'pvc-legend-key');
            key.append(el('span', 'pvc-key pvc-key-gap', 'Hatched band = no measurement'));
            if (config.overlays.includes('comms')) key.append(el('span', 'pvc-key pvc-key-comms', 'Bar = meter communication lost'));
            if (config.overlays.includes('alarms')) key.append(el('span', 'pvc-key pvc-key-alarm', '▲ = alarm active'));
            if (config.overlays.includes('control')) key.append(el('span', 'pvc-key pvc-key-control', '┊ = control mode change'));
            legend.append(key);
        }

        function buildStats(built) {
            statsRow.replaceChildren();
            built.entries.filter((entry) => !entry.hidden).forEach((entry) => {
                const group = el('div', 'pvc-stat-group');
                const title = el('span', 'pvc-stat-series');
                const dot = svg('svg', { class: 'pvc-stat-mark', viewBox: '0 0 12 12', 'aria-hidden': 'true' });
                dot.append(svg('path', { d: glyphPath(entry.slot.glyph), class: `pvc-fill ${entry.slot.className}` }));
                title.append(dot, el('span', '', entry.label));
                group.append(title);
                const figures = entry.stats;
                [
                    ['Current', figures.currentMissing ? 'No measurement' : formatKw(figures.current)],
                    ['Minimum', formatKw(figures.min)],
                    ['Average', formatKw(figures.average)],
                    ['Peak', formatKw(figures.max)]
                ].forEach(([label, value]) => {
                    const item = el('div', 'pvc-stat');
                    item.append(el('span', '', label), el('strong', '', value));
                    group.append(item);
                });
                group.append(el('small', 'pvc-stat-basis', `n=${figures.count} measured`));
                statsRow.append(group);
            });
        }

        function buildQuality(built) {
            quality.replaceChildren();
            const shown = built.entries.filter((entry) => !entry.hidden);
            /* A quantity this site never measures and a quantity whose record has
             * holes are two different conditions and are reported separately: an
             * uninstrumented series is not a degraded one, and rolling it into a
             * coverage percentage would hide real gaps in the series that is
             * instrumented. */
            const absent = shown.filter((entry) => entry.stats.count === 0);
            const present = shown.filter((entry) => entry.stats.count > 0);
            if (!shown.length) {
                quality.append(el('span', 'pvc-quality-pill', 'No series shown'));
            } else if (present.length) {
                let worst = 1;
                let missing = 0;
                let gapCount = 0;
                present.forEach((entry) => {
                    worst = Math.min(worst, entry.stats.coverage);
                    missing += entry.stats.missing;
                    gapCount += entry.gaps.length;
                });
                const tone = worst >= 0.995 ? 'good' : worst >= 0.9 ? 'warning' : 'bad';
                const label = worst >= 0.995
                    ? 'Data quality: complete record'
                    : `Data quality: ${Math.round(worst * 100)}% measured · ${missing} sample${missing === 1 ? '' : 's'} not measured in ${gapCount} gap${gapCount === 1 ? '' : 's'}`;
                quality.append(el('span', `pvc-quality-pill ${tone}`, label));
            }
            absent.forEach((entry) => {
                quality.append(el('span', 'pvc-quality-pill warning', `${entry.label}: not measured in this range`));
            });
            const comms = config.overlays.includes('comms')
                ? flagRuns(built.anchor, (sample) => sample.meter_online === false)
                : [];
            if (comms.length) {
                quality.append(el('span', 'pvc-quality-pill bad',
                    `Meter communication lost in ${comms.length} interval${comms.length === 1 ? '' : 's'}`));
            }
        }

        function buildTable() {
            const built = model();
            const shown = built.entries.filter((entry) => !entry.hidden);
            tableBody.replaceChildren();
            if (!built.anchor.length || !shown.length) {
                tableBody.append(el('p', '', 'No samples in this range.'));
                return;
            }
            /* Capped: this device has very little heap and a 24 h range is 1440
             * samples. The stride is stated so the table is not mistaken for the
             * complete record. */
            const maxRows = 60;
            const stride = Math.max(1, Math.ceil(built.anchor.length / maxRows));
            const grid = el('table');
            const headRow = el('tr');
            headRow.append(el('th', '', 'Time'));
            shown.forEach((entry) => headRow.append(el('th', '', entry.label)));
            const header = el('thead');
            header.append(headRow);
            grid.append(header);
            const body = el('tbody');
            for (let index = 0; index < built.anchor.length; index += stride) {
                const row = el('tr');
                row.append(el('td', '', formatClock(built.anchor[index].t, built.info.ms)));
                shown.forEach((entry) => {
                    const point = entry.points[index];
                    const value = point ? point.v : null;
                    row.append(el('td', value === null ? 'not measured' : '', value === null ? null : formatKw(value)));
                });
                body.append(row);
            }
            grid.append(body);
            tableBody.append(grid);
            if (stride > 1) tableBody.append(el('p', 'pvc-table-note', `Showing every ${stride}${stride === 2 ? 'nd' : stride === 3 ? 'rd' : 'th'} sample of ${built.anchor.length}.`));
        }

        function render() {
            const built = model();
            buildLegend(built);
            buildStats(built);
            buildQuality(built);

            const shown = built.entries.filter((entry) => !entry.hidden);
            const summaryEntries = shown.map((entry) => ({ label: entry.label, stats: entry.stats, gaps: entry.gaps }));
            const text = summaryText(summaryEntries, { rangeLabel: built.info.label });
            summary.textContent = text;
            figure.setAttribute('aria-label', `${config.title}. ${text}`);
            note.textContent = 'Real sample times; the axis always contains zero so import stays above the zero line and export below. A missing measurement breaks the line — nothing is drawn across a gap and no unmeasured value is drawn as zero.';

            const measuredTotal = shown.reduce((total, entry) => total + entry.stats.count, 0);
            if (view.state === 'loading') { showOverlay('Loading controller history…', 'loading'); }
            else if (view.state === 'error') { showOverlay(view.message || 'Controller history is unavailable.', 'error'); }
            else if (!built.anchor.length) { showOverlay('The controller has not stored any sample for this range yet.', 'empty'); }
            else if (!measuredTotal) { showOverlay('No quantity in this range was measured. Nothing is plotted — this is not zero power.', 'empty'); }
            else { overlayMessage.hidden = true; }

            draw(built);
            if (table.open) buildTable();
        }

        function showOverlay(message, kind) {
            overlayMessage.hidden = false;
            overlayMessage.className = `pvc-overlay pvc-overlay-${kind}`;
            overlayMessage.replaceChildren(el('span', '', message));
        }

        function draw(built) {
            /* The viewBox is the figure's real pixel box, so one user unit is
             * one CSS pixel: text stays at its intended size and nothing is
             * stretched when a media query shortens the plot on a small screen. */
            const width = Math.max(320, Math.round(figure.clientWidth || view.width || 640));
            const height = Math.max(200, Math.round(figure.clientHeight || view.height || config.height));
            view.width = width;
            view.height = height;
            canvas.setAttribute('viewBox', `0 0 ${width} ${height}`);
            canvas.replaceChildren();

            const left = MARGIN.left;
            const right = width - MARGIN.right;
            const top = MARGIN.top;
            const bottom = height - MARGIN.bottom;
            const shown = built.entries.filter((entry) => !entry.hidden);
            const anchor = built.anchor;
            if (!anchor.length) return;

            const t0 = anchor[0].t;
            const t1 = anchor[anchor.length - 1].t;
            const spanMs = Math.max(1000, t1 - t0);
            let low = Infinity;
            let high = -Infinity;
            shown.forEach((entry) => {
                if (entry.stats.count) {
                    low = Math.min(low, entry.stats.min);
                    high = Math.max(high, entry.stats.max);
                }
            });
            const axis = niceScale(Number.isFinite(low) ? low : 0, Number.isFinite(high) ? high : 0, { ticks: 5 });
            const x = scale(t0, t1 === t0 ? t0 + spanMs : t1, left, right);
            const y = scale(axis.min, axis.max, bottom, top);
            const zeroY = y(0);

            const defs = svg('defs');
            defs.append(hatch(`${id}-gap`, 'currentColor'));
            defs.append(dots(`${id}-export`));
            canvas.append(defs);

            /* Export half-plane gets a texture as well as a label, so the two
             * sides of zero are distinguishable without colour. */
            if (axis.min < 0) {
                canvas.append(svg('rect', {
                    x: left, y: zeroY, width: right - left, height: Math.max(0, bottom - zeroY),
                    fill: `url(#${id}-export)`, class: 'pvc-export-region'
                }));
            }

            const gridLayer = svg('g', { class: 'pvc-grid' });
            axis.ticks.forEach((tick) => {
                const ty = y(tick);
                gridLayer.append(svg('line', { x1: left, x2: right, y1: ty, y2: ty, class: tick === 0 ? 'pvc-zero-line' : 'pvc-grid-line' }));
                const label = svg('text', { x: left - 8, y: ty + 4, class: 'pvc-axis-label', 'text-anchor': 'end' });
                label.textContent = tick === 0 ? '0' : String(Number(tick.toFixed(3)));
                gridLayer.append(label);
            });
            const offset = -new Date().getTimezoneOffset() * 60000;
            timeTicks(t0, t1, Math.max(3, Math.round((right - left) / 130)), offset).forEach((tick) => {
                const tx = x(tick);
                gridLayer.append(svg('line', { x1: tx, x2: tx, y1: top, y2: bottom, class: 'pvc-grid-line' }));
                const label = svg('text', { x: tx, y: bottom + 18, class: 'pvc-axis-label', 'text-anchor': 'middle' });
                label.textContent = formatClock(tick, spanMs);
                gridLayer.append(label);
            });
            canvas.append(gridLayer);

            const zeroLabel = svg('text', { x: left + 6, y: zeroY - 6, class: 'pvc-zero-label' });
            zeroLabel.textContent = 'Zero export';
            canvas.append(zeroLabel);
            const importLabel = svg('text', { x: left - 8, y: top + 12, class: 'pvc-side-label', 'text-anchor': 'end' });
            importLabel.textContent = '▲ import';
            canvas.append(importLabel);
            if (axis.min < 0) {
                const exportLabel = svg('text', { x: left - 8, y: bottom - 4, class: 'pvc-side-label', 'text-anchor': 'end' });
                exportLabel.textContent = '▼ export';
                canvas.append(exportLabel);
            }

            /* Communication-loss intervals come from the record's own
             * meter_online flag, not from the value being absent. */
            if (config.overlays.includes('comms')) {
                flagRuns(anchor, (sample) => sample.meter_online === false).forEach((run) => {
                    const x0 = x(run.from);
                    const x1 = x(run.to);
                    canvas.append(svg('rect', {
                        x: x0, y: bottom - 6, width: Math.max(2, x1 - x0), height: 6, class: 'pvc-comms-bar'
                    }));
                });
            }

            /* Gaps are drawn per series and hatched, so an absence is a visible
             * object on the chart rather than an invisible interpolation. */
            if (config.overlays.includes('gaps')) {
                const gapLayer = svg('g', { class: 'pvc-gaps' });
                shown.forEach((entry) => {
                    entry.gaps.forEach((gap) => {
                        const x0 = x(gap.from);
                        const x1 = x(gap.to);
                        gapLayer.append(svg('rect', {
                            x: x0, y: top, width: Math.max(3, x1 - x0), height: bottom - top,
                            fill: `url(#${id}-gap)`, class: 'pvc-gap-band'
                        }));
                    });
                });
                canvas.append(gapLayer);
            }

            if (config.overlays.includes('control')) {
                transitions(anchor, (sample) => sample.control_enabled === true).forEach((change) => {
                    const tx = x(change.t);
                    canvas.append(svg('line', { x1: tx, x2: tx, y1: top, y2: bottom, class: 'pvc-control-line' }));
                    const label = svg('text', { x: tx + 4, y: top + 12, class: 'pvc-marker-label' });
                    label.textContent = change.to ? 'Auto on' : 'Auto off';
                    canvas.append(label);
                });
            }

            if (config.overlays.includes('alarms')) {
                flagRuns(anchor, (sample) => Number(sample.alarms) > 0).forEach((run) => {
                    const x0 = x(run.from);
                    const x1 = x(run.to);
                    canvas.append(svg('rect', { x: x0, y: top, width: Math.max(2, x1 - x0), height: 5, class: 'pvc-alarm-bar' }));
                    const marker = svg('path', { d: `M${x0} ${top + 9}l4-7 4 7z`, class: 'pvc-alarm-mark' });
                    canvas.append(marker);
                });
            }

            shown.forEach((entry) => {
                const layer = svg('g', { class: 'pvc-series' });
                entry.segments.forEach((run) => {
                    if (run.length === 1) {
                        layer.append(svg('circle', { cx: x(run[0].t), cy: y(run[0].v), r: 3, class: `pvc-fill ${entry.slot.className}` }));
                        return;
                    }
                    let path = '';
                    for (let index = 0; index < run.length; index += 1) {
                        path += `${index ? 'L' : 'M'}${x(run[index].t).toFixed(1)} ${y(run[index].v).toFixed(1)}`;
                    }
                    layer.append(svg('path', {
                        d: path,
                        class: `pvc-line ${entry.slot.className}`,
                        'stroke-dasharray': entry.slot.dash || null
                    }));
                });
                canvas.append(layer);
            });

            const cursorLayer = svg('g', { class: 'pvc-cursor' });
            canvas.append(cursorLayer);

            const hit = svg('rect', { x: left, y: top, width: Math.max(1, right - left), height: Math.max(1, bottom - top), class: 'pvc-hit' });
            canvas.append(hit);

            const geometry = { built, shown, x, y, left, right, top, bottom, spanMs, t0, t1, cursorLayer, width };
            view.geometry = geometry;
            attachPointer(hit, geometry);
            if (view.cursor >= 0) paintCursor(geometry, view.cursor);
        }

        function hatch(name, color) {
            const pattern = svg('pattern', { id: name, width: 8, height: 8, patternUnits: 'userSpaceOnUse', patternTransform: 'rotate(45)' });
            pattern.append(svg('rect', { width: 8, height: 8, class: 'pvc-hatch-bg' }));
            pattern.append(svg('line', { x1: 0, y1: 0, x2: 0, y2: 8, class: 'pvc-hatch-line', stroke: color }));
            return pattern;
        }

        function dots(name) {
            const pattern = svg('pattern', { id: name, width: 6, height: 6, patternUnits: 'userSpaceOnUse' });
            pattern.append(svg('circle', { cx: 1.5, cy: 1.5, r: 1, class: 'pvc-dot' }));
            return pattern;
        }

        function attachPointer(hit, geometry) {
            hit.addEventListener('pointermove', (event) => {
                const rect = canvas.getBoundingClientRect();
                if (!rect.width) return;
                const px = (event.clientX - rect.left) * (geometry.width / rect.width);
                const ratio = (px - geometry.left) / Math.max(1, geometry.right - geometry.left);
                const time = geometry.t0 + ratio * (geometry.t1 - geometry.t0);
                const index = nearestIndex(geometry.built.times, time);
                if (index >= 0 && index !== view.cursor) { view.cursor = index; paintCursor(geometry, index); }
            });
            hit.addEventListener('pointerleave', () => { view.cursor = -1; clearCursor(); });
        }

        function clearCursor() {
            tip.hidden = true;
            if (view.geometry && view.geometry.cursorLayer) view.geometry.cursorLayer.replaceChildren();
        }

        function paintCursor(geometry, index) {
            const anchor = geometry.built.anchor;
            const point = anchor[index];
            if (!point) return;
            const layer = geometry.cursorLayer;
            layer.replaceChildren();
            const cx = geometry.x(point.t);
            layer.append(svg('line', { x1: cx, x2: cx, y1: geometry.top, y2: geometry.bottom, class: 'pvc-crosshair' }));
            const lines = [formatClock(point.t, geometry.spanMs)];
            geometry.shown.forEach((entry) => {
                const value = entry.points[index] ? entry.points[index].v : null;
                if (value !== null) {
                    layer.append(svg('circle', { cx, cy: geometry.y(value), r: 4, class: `pvc-point pvc-fill ${entry.slot.className}` }));
                    layer.append(svg('path', { d: glyphPath(entry.slot.glyph), class: `pvc-point-glyph pvc-fill ${entry.slot.className}`, transform: `translate(${cx - 6} ${geometry.y(value) - 14})` }));
                }
                lines.push(`${entry.label}: ${value === null ? 'not measured' : describeFlow(value, entry.meaning)}`);
            });
            const sample = point.sample || {};
            if (sample.meter_online === false) lines.push('Meter communication lost');
            if (Number(sample.alarms) > 0) lines.push('Alarm active');
            if (sample.control_enabled === true) lines.push('Automatic control enabled');
            tip.replaceChildren();
            lines.forEach((line, position) => tip.append(el(position ? 'span' : 'strong', '', line)));
            tip.hidden = false;
            /* The tooltip is positioned against the figure box, so it follows the
             * crosshair's pixel position rather than its position within the plot
             * rectangle, and flips side before it can leave the card. */
            const ratio = cx / Math.max(1, geometry.width);
            tip.style.left = `${Math.round(ratio * 100)}%`;
            tip.classList.toggle('flip', ratio > 0.62);
            live.textContent = lines.join('. ');
        }

        figure.addEventListener('keydown', (event) => {
            const geometry = view.geometry;
            if (!geometry || !geometry.built.anchor.length) return;
            const last = geometry.built.anchor.length - 1;
            let next = view.cursor;
            if (event.key === 'ArrowRight') next = view.cursor < 0 ? 0 : Math.min(last, view.cursor + 1);
            else if (event.key === 'ArrowLeft') next = view.cursor < 0 ? last : Math.max(0, view.cursor - 1);
            else if (event.key === 'Home') next = 0;
            else if (event.key === 'End') next = last;
            else if (event.key === 'Escape') { view.cursor = -1; clearCursor(); live.textContent = 'Point details cleared.'; return; }
            else return;
            event.preventDefault();
            view.cursor = next;
            paintCursor(geometry, next);
        });
        figure.addEventListener('focus', () => {
            if (view.cursor < 0) live.textContent = 'Chart focused. Use the left and right arrow keys to read individual samples, Escape to stop.';
        });
        figure.addEventListener('blur', () => { view.cursor = -1; clearCursor(); });

        let observer = null;
        if (typeof ResizeObserver === 'function') {
            observer = new ResizeObserver(() => {
                if (view.frameQueued) return;
                view.frameQueued = true;
                window.requestAnimationFrame(() => {
                    view.frameQueued = false;
                    const width = Math.round(figure.clientWidth || 0);
                    const height = Math.round(figure.clientHeight || 0);
                    if (Math.abs(width - view.width) < 5 && Math.abs(height - view.height) < 5) return;
                    draw(model());
                });
            });
            observer.observe(figure);
        }

        render();

        return {
            element: card,
            get range() { return view.range; },
            setRange(value) {
                const next = normalizeRange(value);
                if (next === view.range) return;
                view.range = next;
                paintRanges();
                render();
            },
            setSeries(list) {
                view.series = (Array.isArray(list) ? list : []).slice(0, SERIES_SLOTS.length);
                view.hidden = Object.create(null);
                view.cursor = -1;
                render();
            },
            setTitle(title, description) {
                /* Heading text only - no redraw. Callers change the title as part
                 * of reconfiguring the chart and always follow with setSeries and
                 * a data or state call, each of which renders. */
                heading.replaceChildren(el('h3', 'pvc-title', title));
                if (description) heading.append(el('p', 'pvc-subtitle', description));
                config.title = title;
            },
            setState(state, message) {
                view.state = state;
                view.message = message || '';
                render();
            },
            setData(payload) {
                const data = payload || {};
                view.samples = Array.isArray(data.samples) ? data.samples : [];
                view.intervalMs = Number(data.sample_interval_ms) || null;
                view.receivedAt = Number.isFinite(Number(data.receivedAt)) ? Number(data.receivedAt) : Date.now();
                if (data.range) { view.range = normalizeRange(data.range); paintRanges(); }
                view.state = 'ready';
                view.message = '';
                view.cursor = -1;
                render();
            },
            destroy() {
                if (observer) observer.disconnect();
                observer = null;
                card.remove();
            }
        };
    }

    return Object.assign({}, pure, { create, nearestIndex });
});
