/* Summarise report.json into the baseline tables and a compact
 * baseline-metrics.json that the "after" run can be diffed against numerically.
 * Extends evidence/ui-audit-2026-07-29/summarize.js. */
const fs = require('fs');
const path = require('path');
const r = JSON.parse(fs.readFileSync(path.join(__dirname, 'report.json'), 'utf8'));

const runs = r.runs.filter((x) => !x.error);
const errs = r.runs.filter((x) => x.error);
console.log(`runs completed: ${runs.length} / ${r.runs.length}`);
errs.forEach((e) => console.log(`  ERROR ${e.route}/${e.viewport}/${e.theme}: ${e.error}`));

/* ---- issue roll-up (same shape as the 2026-07-29 summary) -------------- */
const byType = new Map();
for (const run of runs) {
    const where = `${run.route}/${run.viewport}/${run.theme}`;
    for (const i of run.issues || []) {
        if (!byType.has(i.type)) byType.set(i.type, []);
        byType.get(i.type).push({ ...i, where });
    }
    for (const c of run.console || []) {
        const t = `console-${c.type}`;
        if (!byType.has(t)) byType.set(t, []);
        byType.get(t).push({ detail: c.text, where });
    }
}
console.log('\n===== ISSUE TYPES =====');
const sorted = [...byType.entries()].sort((a, b) => b[1].length - a[1].length);
for (const [t, items] of sorted) {
    const distinct = new Set(items.map((i) => `${i.selector || ''}|${(i.detail || '').slice(0, 80)}`));
    console.log(`${String(items.length).padStart(5)}  ${t}  (${distinct.size} distinct)`);
}

console.log('\n===== WORST CONTRAST =====');
const contrast = (byType.get('low-contrast-text') || []).map((i) => ({
    ratio: parseFloat((i.detail.match(/ratio ([\d.]+)/) || [])[1]), ...i }));
const cSeen = new Map();
for (const c of contrast) {
    const k = `${c.selector}|${c.detail}`;
    if (!cSeen.has(k)) cSeen.set(k, { ...c, count: 0, wheres: new Set() });
    cSeen.get(k).count++; cSeen.get(k).wheres.add(c.where);
}
[...cSeen.values()].sort((a, b) => a.ratio - b.ratio).slice(0, 15).forEach((c) => {
    console.log(`  ${c.ratio.toFixed(2)}  ${c.selector}  "${(c.text || '').slice(0, 30)}"  ${c.detail.slice(0, 90)}`);
    console.log(`         x${c.count} seen: ${[...c.wheres].slice(0, 4).join(', ')}`);
});

console.log('\n===== TAP TARGETS < 44 (390x844) =====');
const taps = new Map();
for (const i of byType.get('tap-target-too-small') || []) {
    const k = `${i.selector}|${i.detail}`;
    if (!taps.has(k)) taps.set(k, { ...i, count: 0, wheres: new Set() });
    taps.get(k).count++; taps.get(k).wheres.add(i.where);
}
[...taps.values()].sort((a, b) => b.count - a.count).slice(0, 20).forEach((t) =>
    console.log(`  x${t.count} ${t.selector} ${t.detail} "${(t.text || '').slice(0, 25)}"`));

/* ---- network across every run ----------------------------------------- */
console.log('\n===== NETWORK (all runs) =====');
const net = new Map();
for (const run of runs) {
    for (const e of run.network?.endpoints || []) {
        if (!net.has(e.path)) net.set(e.path, { path: e.path, count: 0, statuses: {} });
        const n = net.get(e.path);
        n.count += e.count;
        for (const [s, c] of Object.entries(e.statuses)) n.statuses[s] = (n.statuses[s] || 0) + c;
    }
}
[...net.values()].sort((a, b) => b.count - a.count).forEach((n) =>
    console.log(`  ${String(n.count).padStart(5)}  ${n.path}  ${JSON.stringify(n.statuses)}`));

/* ---- per-route baseline table ----------------------------------------- */
const table = [];
for (const run of runs) {
    if (!run.metrics) continue;
    const m = run.metrics;
    const chartsWithCard = m.charts.filter((c) => c.plotPctOfCard !== null);
    table.push({
        route: run.route, viewport: run.viewport, theme: run.theme, access: run.access,
        usefulPct: m.area.usefulPct, valuePct: m.area.valuePct, labelPct: m.area.labelPct,
        controlPct: m.area.controlPct, prosePct: m.area.prosePct, chromePct: m.area.chromePct,
        emptyPct: m.area.emptyPct,
        visibleWords: m.wordCounts.visibleWords, proseWords: m.wordCounts.proseWords,
        viewportWords: m.wordCounts.viewportWords,
        repeatedProseBlocks: m.repeatedProse.length,
        repeatedValueGroups: m.repeatedValueCount,
        repeatedValueWithUnit: m.repeatedValueWithUnitCount,
        cards: m.cards.count, cardsInView: m.cards.inViewCount, underfilledCards: m.cards.underfilled.length,
        charts: m.charts.length,
        chartPlotPctOfCard: chartsWithCard.length
            ? Math.round(chartsWithCard.reduce((a, c) => a + c.plotPctOfCard, 0) / chartsWithCard.length * 10) / 10 : null,
        belowFoldValues: m.belowFold.count,
        jargonHits: m.jargon.length,
        domNodes: m.dom.nodes, scrollHeight: m.dom.scrollHeight, folds: m.dom.foldsToScroll,
        requests: run.network.total, apiRequests: run.network.apiTotal,
        req401: run.network.status401, req500: run.network.status500, req4xx: run.network.status4xx,
        redundantPolls: run.network.redundantPolls.length,
        consoleErrors: (run.console || []).filter((c) => c.type !== 'warning').length,
        consoleWarnings: (run.console || []).filter((c) => c.type === 'warning').length,
        issues: run.issues.length,
    });
}
const base = table.filter((t) => t.viewport === '1440x900' && t.theme === 'light');
console.log('\n===== BASELINE 1440x900 light =====');
console.log('route         useful%  value%  prose%  chrome%  empty%  words  prose-w  dupVals  cards  charts  belowFold  jargon  DOM   reqs');
for (const t of base) {
    console.log(`${t.route.padEnd(14)}${String(t.usefulPct).padStart(6)}${String(t.valuePct).padStart(8)}` +
        `${String(t.prosePct).padStart(8)}${String(t.chromePct).padStart(9)}${String(t.emptyPct).padStart(8)}` +
        `${String(t.visibleWords).padStart(7)}${String(t.proseWords).padStart(9)}${String(t.repeatedValueGroups).padStart(9)}` +
        `${String(t.cards).padStart(7)}${String(t.charts).padStart(8)}${String(t.belowFoldValues).padStart(11)}` +
        `${String(t.jargonHits).padStart(8)}${String(t.domNodes).padStart(6)}${String(t.requests).padStart(7)}`);
}

/* ---- jargon roll-up ---------------------------------------------------- */
console.log('\n===== DEVELOPER TERMINOLOGY (1440x900 light) =====');
const jarg = new Map();
for (const run of runs.filter((x) => x.viewport === '1440x900' && x.theme === 'light' && x.metrics)) {
    for (const j of run.metrics.jargon) {
        const k = `${j.term}|${j.text.slice(0, 60)}`;
        if (!jarg.has(k)) jarg.set(k, { ...j, routes: new Set() });
        jarg.get(k).routes.add(run.route);
    }
}
[...jarg.values()].sort((a, b) => a.term.localeCompare(b.term)).forEach((j) =>
    console.log(`  [${j.term}] ${j.selector}  "${j.text.slice(0, 80)}"  routes: ${[...j.routes].join(',')}`));

/* ---- repeated values --------------------------------------------------- */
console.log('\n===== REPEATED VALUES (1440x900 light) =====');
for (const run of runs.filter((x) => x.viewport === '1440x900' && x.theme === 'light' && x.metrics)) {
    const rep = run.metrics.repeatedValues.filter((v) => v.count > 1);
    if (!rep.length) continue;
    console.log(`  --- ${run.route}: ${rep.length} repeated value groups`);
    rep.slice(0, 8).forEach((v) => {
        console.log(`      "${v.value}${v.unit ? ' ' + v.unit : ''}" x${v.count}`);
        v.locations.slice(0, 4).forEach((l) => console.log(`         ${l.selector} @(${l.x},${l.y}) ${l.w}x${l.h} "${l.text}"`));
    });
}

/* ---- charts ------------------------------------------------------------ */
console.log('\n===== CHARTS (1440x900 light) =====');
for (const run of runs.filter((x) => x.viewport === '1440x900' && x.theme === 'light' && x.metrics)) {
    if (!run.metrics.charts.length) continue;
    console.log(`  --- ${run.route}`);
    run.metrics.charts.forEach((c) => console.log(
        `      ${c.selector} ${c.w}x${c.h} in card ${c.card} ${c.cardW}x${c.cardH} -> ${c.plotPctOfCard}% of card area, ${c.plotHeightPctOfCard}% of card height`));
}

/* ---- empty regions ----------------------------------------------------- */
console.log('\n===== LARGEST EMPTY REGIONS (1440x900 light) =====');
for (const run of runs.filter((x) => x.viewport === '1440x900' && x.theme === 'light' && x.metrics)) {
    const e = run.metrics.emptyRegions.slice(0, 3);
    if (!e.length) continue;
    console.log(`  ${run.route}: ` + e.map((x) => `${x.w}x${x.h} @(${x.x},${x.y}) = ${x.pctOfViewport}%`).join('; '));
}

/* ---- underfilled cards -------------------------------------------------- */
console.log('\n===== CARDS UNDER 40% FILLED (1440x900 light) =====');
for (const run of runs.filter((x) => x.viewport === '1440x900' && x.theme === 'light' && x.metrics)) {
    run.metrics.cards.underfilled.forEach((c) =>
        console.log(`  ${run.route}: ${c.selector} ${c.w}x${c.h} @(${c.x},${c.y}) fill ${c.fillPct}%`));
}

fs.writeFileSync(path.join(__dirname, 'baseline-metrics.json'),
    JSON.stringify({ generated: r.generated, base: r.base, method: 'see metrics.js header', rows: table,
                     network: [...net.values()].sort((a, b) => b.count - a.count) }, null, 2));
console.log('\nwrote baseline-metrics.json');
