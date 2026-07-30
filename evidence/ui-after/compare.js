/* Numeric before/after diff. Reads the two metrics files produced by the SAME
 * summariser (../ui-baseline/summarize3.js) from the SAME harness, so every
 * column is measured identically on both sides.
 *
 *   before: ../ui-baseline/baseline-metrics.json   (build A, 2026-07-30)
 *   after:  ./after-metrics.json                   (redesign, 2026-07-31)
 *
 * Writes before-after.json and prints the markdown tables used in
 * docs/UI_AFTER_AUDIT.md.
 */
const fs = require('fs');
const path = require('path');

const before = JSON.parse(fs.readFileSync(path.join(__dirname, '..', 'ui-baseline', 'baseline-metrics.json'), 'utf8'));
const after = JSON.parse(fs.readFileSync(path.join(__dirname, 'after-metrics.json'), 'utf8'));
const afterReport = JSON.parse(fs.readFileSync(path.join(__dirname, 'report.json'), 'utf8'));
const beforeReport = JSON.parse(fs.readFileSync(path.join(__dirname, '..', 'ui-baseline', 'report.json'), 'utf8'));

const ROUTES = ['dashboard', 'meters', 'inverters', 'control', 'alarms',
                'wifi', 'system', 'commissioning', 'readiness', 'engineering'];
const key = (r) => `${r.route}|${r.viewport}|${r.theme}`;
const bMap = new Map(before.rows.map((r) => [key(r), r]));
const aMap = new Map(after.rows.map((r) => [key(r), r]));

const pick = (m, route, vp = '1440x900', theme = 'light') => m.get(`${route}|${vp}|${theme}`);

/* chart pixels as a share of the first viewport - derived from the same
 * measured chart rects the harness already records; stated separately because
 * metrics.js deliberately does not count graphics as "useful" area. */
const chartViewportPct = (report, route) => {
    const run = report.runs.find((r) => r.route === route && r.viewport === '1440x900' &&
                                        r.theme === 'light' && r.zoom === 1 && r.metrics);
    if (!run) return null;
    const vw = run.metrics.area.viewport.w, vh = run.metrics.area.viewport.h;
    let px = 0;
    for (const c of run.metrics.charts) {
        const top = Math.max(0, c.y), bottom = Math.min(vh, c.y + c.h);
        const left = Math.max(0, c.x), right = Math.min(vw, c.x + c.w);
        if (bottom > top && right > left) px += (bottom - top) * (right - left);
    }
    return Math.round((px / (vw * vh)) * 1000) / 10;
};

const rows = [];
for (const route of ROUTES) {
    const b = pick(bMap, route), a = pick(aMap, route);
    rows.push({
        route,
        words: [b?.visibleWords, a?.visibleWords],
        prose: [b?.proseWords, a?.proseWords],
        useful: [b?.usefulPct, a?.usefulPct],
        value: [b?.valuePct, a?.valuePct],
        empty: [b?.emptyPct, a?.emptyPct],
        dupValues: [b?.repeatedValueGroups, a?.repeatedValueGroups],
        dom: [b?.domNodes, a?.domNodes],
        requests: [b?.requests, a?.requests],
        req401: [b?.req401, a?.req401],
        consoleErr: [b?.consoleErrors, a?.consoleErrors],
        consoleWarn: [b?.consoleWarnings, a?.consoleWarnings],
        charts: [b?.charts, a?.charts],
        plotPctOfCard: [b?.chartPlotPctOfCard, a?.chartPlotPctOfCard],
        chartPctOfViewport: [chartViewportPct(beforeReport, route), chartViewportPct(afterReport, route)],
        issues: [b?.issues, a?.issues],
        belowFold: [b?.belowFoldValues, a?.belowFoldValues],
        jargon: [b?.jargonHits, a?.jargonHits],
        scrollHeight: [b?.scrollHeight, a?.scrollHeight],
    });
}

const sum = (f) => rows.reduce((acc, r) => [acc[0] + (r[f][0] || 0), acc[1] + (r[f][1] || 0)], [0, 0]);
const totals = {
    words: sum('words'), prose: sum('prose'), dupValues: sum('dupValues'),
    requests: sum('requests'), req401: sum('req401'),
    consoleErr: sum('consoleErr'), consoleWarn: sum('consoleWarn'), issues: sum('issues'),
};

const d = (p) => (p[0] == null || p[1] == null) ? '—'
    : (p[1] - p[0] > 0 ? '+' : '') + (Math.round((p[1] - p[0]) * 10) / 10);
const pctChange = (p) => (!p[0]) ? '—' : ((p[1] - p[0]) / p[0] * 100).toFixed(1) + ' %';

console.log('\n===== PER-ROUTE, 1440x900 light operator =====');
console.log('| Route | words B→A | prose B→A | useful % B→A | value % B→A | empty % B→A | dup groups B→A | DOM B→A | reqs B→A | 401 B→A | console err/warn B→A | charts B→A | plot % of card B→A |');
console.log('|---|---|---|---|---|---|---|---|---|---|---|---|---|');
for (const r of rows) {
    console.log(`| ${r.route} | ${r.words[0]} → **${r.words[1]}** | ${r.prose[0]} → ${r.prose[1]} | ` +
        `${r.useful[0]} → **${r.useful[1]}** | ${r.value[0]} → ${r.value[1]} | ${r.empty[0]} → ${r.empty[1]} | ` +
        `${r.dupValues[0]} → ${r.dupValues[1]} | ${r.dom[0]} → ${r.dom[1]} | ${r.requests[0]} → ${r.requests[1]} | ` +
        `${r.req401[0]} → ${r.req401[1]} | ${r.consoleErr[0]}/${r.consoleWarn[0]} → ${r.consoleErr[1]}/${r.consoleWarn[1]} | ` +
        `${r.charts[0]} → ${r.charts[1]} | ${r.plotPctOfCard[0] ?? '—'} → ${r.plotPctOfCard[1] ?? '—'} |`);
}
console.log(`| **total** | ${totals.words[0]} → **${totals.words[1]}** (${pctChange(totals.words)}) | ` +
    `${totals.prose[0]} → ${totals.prose[1]} (${pctChange(totals.prose)}) | — | — | — | ` +
    `${totals.dupValues[0]} → ${totals.dupValues[1]} | — | ${totals.requests[0]} → ${totals.requests[1]} | ` +
    `${totals.req401[0]} → ${totals.req401[1]} | ${totals.consoleErr[0]}/${totals.consoleWarn[0]} → ${totals.consoleErr[1]}/${totals.consoleWarn[1]} | — | — |`);

console.log('\n===== CHART PIXELS AS % OF FIRST VIEWPORT (measured rects; not counted as "useful" by metrics.js) =====');
for (const r of rows) if (r.chartPctOfViewport[0] || r.chartPctOfViewport[1])
    console.log(`  ${r.route.padEnd(14)} ${r.chartPctOfViewport[0]} → ${r.chartPctOfViewport[1]}`);

console.log('\n===== USEFUL % ACROSS VIEWPORTS (light) =====');
const VPS = ['1440x900', '1920x1080', '1024x768', '390x844'];
console.log('| Route | ' + VPS.map((v) => `${v} B→A`).join(' | ') + ' |');
console.log('|---|' + VPS.map(() => '---').join('|') + '|');
for (const route of ROUTES) {
    const cells = VPS.map((v) => {
        const b = pick(bMap, route, v), a = pick(aMap, route, v);
        return `${b?.usefulPct ?? '—'} → ${a?.usefulPct ?? '—'}`;
    });
    console.log(`| ${route} | ${cells.join(' | ')} |`);
}

console.log('\n===== DARK THEME CHECK (1440x900 dark) =====');
for (const route of ROUTES) {
    const b = pick(bMap, route, '1440x900', 'dark'), a = pick(aMap, route, '1440x900', 'dark');
    console.log(`  ${route.padEnd(14)} useful ${b?.usefulPct} → ${a?.usefulPct}, words ${b?.visibleWords} → ${a?.visibleWords}, issues ${b?.issues} → ${a?.issues}`);
}

/* ---- whole-run aggregates -------------------------------------------- */
const agg = (report) => {
    const runs = report.runs.filter((r) => !r.error);
    let reqs = 0, s401 = 0, s4xx = 0, s5xx = 0, cerr = 0, cwarn = 0;
    const issueTypes = new Map();
    for (const r of runs) {
        reqs += r.network?.total || 0;
        s401 += r.network?.status401 || 0;
        s4xx += r.network?.status4xx || 0;
        s5xx += r.network?.status500 || 0;
        for (const c of r.console || []) (c.type === 'warning' ? cwarn++ : cerr++);
        for (const i of r.issues || []) issueTypes.set(i.type, (issueTypes.get(i.type) || 0) + 1);
    }
    const contrast = [];
    for (const r of runs) for (const i of r.issues || [])
        if (i.type === 'low-contrast-text') contrast.push(`${i.selector}|${i.detail}`);
    return { runs: runs.length, total: report.runs.length, reqs, s401, s4xx, s5xx, cerr, cwarn,
             issueTypes: [...issueTypes.entries()].sort((a, b) => b[1] - a[1]),
             contrastOccurrences: contrast.length, contrastDistinct: new Set(contrast).size };
};
const aggB = agg(beforeReport), aggA = agg(afterReport);
console.log('\n===== WHOLE-RUN AGGREGATES =====');
console.log(`  runs completed        ${aggB.runs}/${aggB.total} → ${aggA.runs}/${aggA.total}`);
console.log(`  HTTP requests         ${aggB.reqs} → ${aggA.reqs}`);
console.log(`  401 / 4xx / 5xx       ${aggB.s401}/${aggB.s4xx}/${aggB.s5xx} → ${aggA.s401}/${aggA.s4xx}/${aggA.s5xx}`);
console.log(`  console errors/warns  ${aggB.cerr}/${aggB.cwarn} → ${aggA.cerr}/${aggA.cwarn}`);
console.log(`  contrast occ/distinct ${aggB.contrastOccurrences}/${aggB.contrastDistinct} → ${aggA.contrastOccurrences}/${aggA.contrastDistinct}`);
console.log('  issue types before:', JSON.stringify(aggB.issueTypes));
console.log('  issue types after: ', JSON.stringify(aggA.issueTypes));

fs.writeFileSync(path.join(__dirname, 'before-after.json'), JSON.stringify({
    generated: new Date().toISOString(),
    before: { source: 'evidence/ui-baseline/baseline-metrics.json', generated: before.generated },
    after: { source: 'evidence/ui-after/after-metrics.json', generated: after.generated },
    method: 'evidence/ui-baseline/metrics.js header (unchanged)',
    rows, totals, aggregates: { before: aggB, after: aggA },
}, null, 2));
console.log('\nwrote before-after.json');
