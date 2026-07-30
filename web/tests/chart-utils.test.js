'use strict';

/* The parts of the one chart that can be silently wrong: the scale, the
 * statistics, the time bucketing, and above all the gap handling. A gap that is
 * quietly bridged, or an unmeasured sample that arrives on the chart as 0 kW,
 * reads on this product as "no power" rather than "no measurement" - so those
 * cases are asserted here rather than left to the eye. */

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const chart = require('../pvdg-chart.js');

/* ---------------------------------------------------------------- ranges */

/* The controller supports exactly three ranges and silently substitutes 15m for
 * anything else, so the module must never hand any other value to a caller. */
assert.deepStrictEqual(chart.RANGES.map((entry) => entry.value), ['15m', '1h', '24h']);
assert.strictEqual(chart.normalizeRange('1h'), '1h');
assert.strictEqual(chart.normalizeRange('24h'), '24h');
assert.strictEqual(chart.normalizeRange('5m'), '15m');
assert.strictEqual(chart.normalizeRange('7d'), '15m');
assert.strictEqual(chart.normalizeRange(undefined), '15m');
assert.strictEqual(chart.normalizeRange('15M'), '15m');
assert.strictEqual(chart.rangeInfo('1h').intervalMs, 60000);

/* ------------------------------------------------- missing-value detection */

assert.strictEqual(chart.measured(null), null);
assert.strictEqual(chart.measured(undefined), null);
assert.strictEqual(chart.measured(''), null);
assert.strictEqual(chart.measured(false), null, 'false must not become 0');
assert.strictEqual(chart.measured(true), null);
assert.strictEqual(chart.measured(NaN), null);
assert.strictEqual(chart.measured('not a number'), null);
assert.strictEqual(chart.measured(0), 0, 'a measured zero is a real reading');
assert.strictEqual(chart.measured(-4.5), -4.5);
assert.strictEqual(chart.measured('12.25'), 12.25);

/* --------------------------------------------------------- timestamp model */

const NOW = 1700000000000;
const sample = (age, grid, extra) => Object.assign({ age_ms: age, grid_kw: grid }, extra || {});

const points = chart.toPoints([
    sample(15000, 1),
    sample(10000, null),
    sample(5000, 3),
    sample(0, 4)
], 'grid_kw', { now: NOW });

// X is a real timestamp, never an array index.
assert.deepStrictEqual(points.map((p) => p.t), [NOW - 15000, NOW - 10000, NOW - 5000, NOW]);
assert.deepStrictEqual(points.map((p) => p.v), [1, null, 3, 4]);

// Out-of-order samples are ordered by time, not by array position.
const shuffled = chart.toPoints([sample(0, 4), sample(15000, 1), sample(5000, 3)], 'grid_kw', { now: NOW });
assert.deepStrictEqual(shuffled.map((p) => p.t), [NOW - 15000, NOW - 5000, NOW]);

// A sample without a usable age cannot be placed in time and is dropped rather
// than guessed at.
assert.strictEqual(chart.toPoints([{ grid_kw: 5 }, { age_ms: 'x', grid_kw: 5 }], 'grid_kw', { now: NOW }).length, 0);

// The same instant may only appear once: this is what stops a refresh that
// overlaps a render from recording a reading twice.
const duplicated = chart.toPoints([sample(5000, 3), sample(5000, 3), sample(0, 4), sample(0, 4)], 'grid_kw', { now: NOW });
assert.strictEqual(duplicated.length, 2);
assert.deepStrictEqual(duplicated.map((p) => p.v), [3, 4]);

// A key the payload does not carry at all is missing, not zero.
assert.deepStrictEqual(
    chart.toPoints([sample(0, 4)], 'solar_kw', { now: NOW }).map((p) => p.v),
    [null]
);

/* ------------------------------------------------------------ gap handling */

assert.strictEqual(chart.gapLimit(5000), 8750);
assert.strictEqual(chart.gapLimit(0), Infinity);
assert.strictEqual(chart.gapLimit(null), Infinity);

const withHole = chart.toPoints([
    sample(20000, 1),
    sample(15000, 2),
    sample(10000, null),
    sample(5000, null),
    sample(0, 5)
], 'grid_kw', { now: NOW });

const runs = chart.segments(withHole, chart.gapLimit(5000));
assert.strictEqual(runs.length, 2, 'a run of missing samples must break the line');
assert.deepStrictEqual(runs[0].map((p) => p.v), [1, 2]);
assert.deepStrictEqual(runs[1].map((p) => p.v), [5]);
// Nothing in any drawn run is null, so nothing is ever plotted at an invented value.
runs.forEach((run) => run.forEach((point) => assert.notStrictEqual(point.v, null)));

const holes = chart.gaps(withHole, chart.gapLimit(5000));
assert.strictEqual(holes.length, 1);
assert.strictEqual(holes[0].reason, 'missing');
assert.strictEqual(holes[0].from, NOW - 15000, 'the gap starts at the last measured sample');
assert.strictEqual(holes[0].to, NOW, 'the gap ends at the next measured sample');

// Silence: the controller stopped sampling for two minutes. The samples either
// side are both measured, but the interval between them must still not be
// bridged with a straight line.
const silent = chart.toPoints([sample(130000, 10), sample(5000, 12), sample(0, 13)], 'grid_kw', { now: NOW });
const silentRuns = chart.segments(silent, chart.gapLimit(5000));
assert.strictEqual(silentRuns.length, 2, 'a long interval between samples is a gap too');
const silentGaps = chart.gaps(silent, chart.gapLimit(5000));
assert.strictEqual(silentGaps.length, 1);
assert.strictEqual(silentGaps[0].reason, 'interval');

// A gap that is still open at the newest sample is reported, not swallowed.
const trailing = chart.toPoints([sample(10000, 1), sample(5000, null), sample(0, null)], 'grid_kw', { now: NOW });
const trailingGaps = chart.gaps(trailing, chart.gapLimit(5000));
assert.strictEqual(trailingGaps.length, 1);
assert.strictEqual(trailingGaps[0].to, NOW);
assert.strictEqual(chart.segments(trailing, chart.gapLimit(5000)).length, 1);

// A leading gap has no earlier measurement to start from and must not reach
// backwards past the first sample.
const leading = chart.toPoints([sample(10000, null), sample(5000, null), sample(0, 7)], 'grid_kw', { now: NOW });
const leadingGaps = chart.gaps(leading, chart.gapLimit(5000));
assert.strictEqual(leadingGaps.length, 1);
assert.strictEqual(leadingGaps[0].from, NOW - 10000);

// A record with no measurement at all produces no drawn run whatsoever.
const empty = chart.toPoints([sample(10000, null), sample(5000, null)], 'grid_kw', { now: NOW });
assert.deepStrictEqual(chart.segments(empty, chart.gapLimit(5000)), []);
assert.deepStrictEqual(chart.segments([], 5000), []);
assert.deepStrictEqual(chart.gaps([], 5000), []);

/* -------------------------------------------------------------- statistics */

const figures = chart.stats(withHole);
assert.strictEqual(figures.samples, 5);
assert.strictEqual(figures.count, 3, 'only measured samples count');
assert.strictEqual(figures.missing, 2);
assert.strictEqual(figures.min, 1);
assert.strictEqual(figures.max, 5);
assert.strictEqual(figures.average, (1 + 2 + 5) / 3, 'the mean must exclude missing samples');
assert.strictEqual(figures.current, 5);
assert.strictEqual(figures.currentAt, NOW);
assert.strictEqual(figures.currentMissing, false);
assert.strictEqual(figures.coverage, 3 / 5);

// A missing sample must never be averaged in as zero. If it were, the mean here
// would be 1.6 rather than 8/3.
assert.notStrictEqual(figures.average, (1 + 2 + 0 + 0 + 5) / 5);

// Export is negative and must survive min/peak intact.
const signed = chart.toPoints([sample(15000, -4), sample(10000, 0), sample(5000, 6), sample(0, -1)], 'grid_kw', { now: NOW });
const signedStats = chart.stats(signed);
assert.strictEqual(signedStats.min, -4);
assert.strictEqual(signedStats.max, 6);
assert.strictEqual(signedStats.average, 0.25);
assert.strictEqual(signedStats.current, -1);

// When the newest sample is missing, the caller is told so rather than being
// handed a stale reading labelled "current".
const stale = chart.stats(trailing);
assert.strictEqual(stale.current, 1);
assert.strictEqual(stale.currentMissing, true);

const nothing = chart.stats(empty);
assert.strictEqual(nothing.count, 0);
assert.strictEqual(nothing.min, null);
assert.strictEqual(nothing.max, null);
assert.strictEqual(nothing.average, null);
assert.strictEqual(nothing.current, null);
assert.strictEqual(nothing.coverage, 0);
assert.strictEqual(chart.stats([]).samples, 0);

// A genuine run of measured zeros is a real measurement and must be averaged.
const zeros = chart.toPoints([sample(10000, 0), sample(5000, 0), sample(0, 0)], 'grid_kw', { now: NOW });
assert.strictEqual(chart.stats(zeros).count, 3);
assert.strictEqual(chart.stats(zeros).average, 0);

// Cross-check against a real controller payload shape: the firmware computes
// min/max/mean the same way over the same samples, so the two must agree.
const controllerSamples = [];
let total = 0;
for (let index = 0; index < 40; index += 1) {
    const value = 24.8 + (index % 7) * 0.03;
    total += value;
    controllerSamples.push(sample((40 - index) * 5000, value));
}
const controllerStats = chart.stats(chart.toPoints(controllerSamples, 'grid_kw', { now: NOW }));
assert.strictEqual(controllerStats.count, 40);
assert.ok(Math.abs(controllerStats.average - total / 40) < 1e-9);

/* ------------------------------------------------------------ scale/domain */

assert.strictEqual(chart.niceStep(0.9), 1);
assert.strictEqual(chart.niceStep(11), 20);
assert.strictEqual(chart.niceStep(0), 1);
assert.strictEqual(chart.niceStep(-3), 1);

// Zero is inside the domain by default: on a chart about the direction of power
// flow, an axis that excludes zero cannot show which side of zero the plant is on.
const positive = chart.niceScale(24.78, 25.01, { ticks: 5 });
assert.strictEqual(positive.min, 0);
assert.ok(positive.max >= 25.01);
assert.ok(positive.ticks.includes(0));

const bipolar = chart.niceScale(-12, 30, { ticks: 5 });
assert.ok(bipolar.min <= -12);
assert.ok(bipolar.max >= 30);
assert.ok(bipolar.ticks.includes(0), 'zero must be an actual tick, not an interpolated position');

const exporting = chart.niceScale(-9, -1, { ticks: 4 });
assert.strictEqual(exporting.max, 0, 'an all-export range still reaches zero');
assert.ok(exporting.ticks.includes(0));

// Degenerate ranges must still produce a usable, non-zero-height axis.
const flat = chart.niceScale(0, 0, { ticks: 5 });
assert.ok(flat.max > flat.min);
assert.ok(flat.ticks.includes(0));
const missingDomain = chart.niceScale(null, undefined, { ticks: 5 });
assert.ok(missingDomain.max > missingDomain.min);

// Ticks are ordered, evenly spaced, and cover the domain.
[positive, bipolar, exporting].forEach((axis) => {
    assert.strictEqual(axis.ticks[0], axis.min);
    assert.ok(axis.ticks[axis.ticks.length - 1] >= axis.max - 1e-9);
    for (let index = 1; index < axis.ticks.length; index += 1) {
        assert.ok(Math.abs((axis.ticks[index] - axis.ticks[index - 1]) - axis.step) < 1e-6);
    }
});

const toPixels = chart.scale(0, 10, 100, 0);
assert.strictEqual(toPixels(0), 100);
assert.strictEqual(toPixels(10), 0);
assert.strictEqual(toPixels(5), 50);
// A collapsed domain must not divide by zero.
assert.strictEqual(chart.scale(5, 5, 0, 100)(5), 50);

/* ------------------------------------------------------ timestamp bucketing */

const HOUR = 3600000;
assert.strictEqual(chart.bucketTimestamp(HOUR + 1234, HOUR, 0), HOUR);
assert.strictEqual(chart.bucketTimestamp(HOUR, HOUR, 0), HOUR);
assert.strictEqual(chart.bucketTimestamp(-1, HOUR, 0), -HOUR);
assert.strictEqual(chart.bucketTimestamp('x', HOUR, 0), null);
assert.strictEqual(chart.bucketTimestamp(HOUR, 0, 0), null);

// Bucketing happens in local time, so an hourly tick lands on the hour the
// operator's clock shows rather than on a UTC boundary.
const offset = 5.5 * HOUR;
assert.strictEqual((chart.bucketTimestamp(10 * HOUR, HOUR, offset) + offset) % HOUR, 0);

assert.strictEqual(chart.timeStep(900000, 5), 300000);
assert.strictEqual(chart.timeStep(86400000, 6), 21600000);
assert.strictEqual(chart.timeStep(60000, 5), 15000);

const ticks = chart.timeTicks(NOW - 900000, NOW, 5, 0);
assert.ok(ticks.length >= 2 && ticks.length <= 8);
ticks.forEach((tick) => {
    assert.ok(tick >= NOW - 900000 && tick <= NOW);
    assert.strictEqual(tick % chart.timeStep(900000, 5), 0, 'ticks sit on bucket boundaries');
});
for (let index = 1; index < ticks.length; index += 1) assert.ok(ticks[index] > ticks[index - 1]);
assert.deepStrictEqual(chart.timeTicks(NOW, NOW, 5, 0), []);
assert.deepStrictEqual(chart.timeTicks(NOW, NOW - 1000, 5, 0), []);
// A 24 h span must not be expanded into an unbounded tick list.
assert.ok(chart.timeTicks(NOW - 86400000, NOW, 6, 0).length <= 64);

/* ---------------------------------------------------------------- overlays */

const overlaySamples = [
    sample(20000, 1, { meter_online: true, alarms: 0, control_enabled: false }),
    sample(15000, null, { meter_online: false, alarms: 2, control_enabled: false }),
    sample(10000, null, { meter_online: false, alarms: 2, control_enabled: false }),
    sample(5000, 3, { meter_online: true, alarms: 0, control_enabled: true }),
    sample(0, 4, { meter_online: true, alarms: 0, control_enabled: true })
];
const overlayPoints = chart.toPoints(overlaySamples, 'grid_kw', { now: NOW });

const commsLoss = chart.flagRuns(overlayPoints, (item) => item.meter_online === false);
assert.strictEqual(commsLoss.length, 1);
assert.strictEqual(commsLoss[0].from, NOW - 15000);
assert.strictEqual(commsLoss[0].to, NOW - 5000);

const alarmRuns = chart.flagRuns(overlayPoints, (item) => Number(item.alarms) > 0);
assert.strictEqual(alarmRuns.length, 1);

const modeChanges = chart.transitions(overlayPoints, (item) => item.control_enabled === true);
assert.strictEqual(modeChanges.length, 1);
assert.strictEqual(modeChanges[0].t, NOW - 5000);
assert.strictEqual(modeChanges[0].to, true);
assert.deepStrictEqual(chart.transitions([], () => true), []);
// A run still open at the end of the record is reported.
assert.strictEqual(chart.flagRuns(overlayPoints, (item) => item.meter_online === true).length, 2);

/* ------------------------------------------------------------- presentation */

assert.strictEqual(chart.formatKw(null), 'No measurement');
assert.strictEqual(chart.formatKw(undefined), 'No measurement');
assert.strictEqual(chart.formatKw(0), '0.00 kW');
assert.strictEqual(chart.formatKw(-4.904), '-4.90 kW');
assert.strictEqual(chart.formatKw(120.44), '120.4 kW');

assert.strictEqual(chart.formatSpan(45000), '45 seconds');
assert.strictEqual(chart.formatSpan(300000), '5 minutes');
assert.strictEqual(chart.formatSpan(-1), 'an unknown period');

const meaning = { positive: 'importing from the utility', negative: 'exporting to the utility' };
assert.ok(chart.describeFlow(5, meaning).includes('importing'));
assert.ok(chart.describeFlow(-5, meaning).includes('exporting'));
assert.ok(chart.describeFlow(0, meaning).includes('balanced'));
assert.strictEqual(chart.describeFlow(null, meaning), 'not measured');

/* The text alternative must say the gap out loud. A screen-reader user who is
 * told only "average 2.67 kW" has no way to know a third of the record is
 * missing. */
const text = chart.summaryText(
    [{ label: 'Grid power', stats: figures, gaps: holes }],
    { rangeLabel: '15 min' }
);
assert.ok(text.includes('Grid power'));
assert.ok(text.includes('minimum'));
assert.ok(text.includes('average'));
assert.ok(text.includes('peak'));
assert.ok(text.includes('3 measured samples'));
assert.ok(text.includes('2 not measured'));
assert.ok(text.includes('gap'));
assert.ok(text.includes('no value is drawn'));

const emptyText = chart.summaryText([{ label: 'Solar production', stats: nothing, gaps: [] }], { rangeLabel: '24 hours' });
assert.ok(emptyText.includes('no measured samples'));
assert.ok(!emptyText.includes('0.00 kW'), 'an unmeasured series must never be summarised as zero');
assert.ok(chart.summaryText([], {}).includes('No series'));

/* ------------------------------------------------- palette and stylesheet */

/* Colour is never the only difference between two series. */
const dashes = chart.SERIES_SLOTS.map((slot) => slot.dash);
assert.strictEqual(new Set(dashes).size, dashes.length);
const glyphs = chart.SERIES_SLOTS.map((slot) => slot.glyph);
assert.strictEqual(new Set(glyphs).size, glyphs.length);

/* The stylesheet carries the same validated steps the module declares, so the
 * two cannot drift apart unnoticed. */
const css = fs.readFileSync(path.join(__dirname, '..', 'pvdg-chart.css'), 'utf-8');
chart.SERIES_SLOTS.forEach((slot) => {
    assert.ok(css.includes(slot.color), `dark series step ${slot.color} missing from pvdg-chart.css`);
    assert.ok(css.includes(slot.light), `light series step ${slot.light} missing from pvdg-chart.css`);
});

console.log('chart-utils test passed');
