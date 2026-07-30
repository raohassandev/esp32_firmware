/* Competing chart implementations — empirical test.
 *
 * operator-view.js:71   sparkline(values)  -> browser-session series, 320x74 viewBox
 * operator-operations.js:520 sparkline(series) -> controller history, 420x92 viewBox
 *
 * Suspected defect: operator-view.js:463-464 pushes a sample into state.gridTrend
 * inside refreshAll(), and then refreshAll() calls renderCurrent() ->
 * renderDashboard(), which pushes the SAME sample again at :242-243. If so the
 * polyline gains 2 points per 5 s poll instead of 1.
 *
 * Measure: count <polyline> points on the dashboard grid-trend sparkline at two
 * instants 30 s apart. 30 s / 5 s = 6 refresh cycles.
 *   +6  -> one sample per cycle (correct)
 *   +12 -> two samples per cycle (double push)
 * Read-only: the page polls on its own; the script only reads the DOM.
 */
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const BASE = process.env.TARGET || 'http://192.168.100.14';

const probe = () => {
    const out = { t: Date.now(), charts: [] };
    for (const wrap of document.querySelectorAll('.op-sparkline')) {
        const poly = wrap.querySelector('polyline');
        const card = wrap.closest('.op-card');
        out.charts.push({
            impl: wrap.classList.contains('op-history-chart')
                ? 'controller-history' : 'browser-session',
            headline: card ? (card.querySelector('.op-card-headline')?.textContent || '').trim() : null,
            note: card ? (card.querySelector('.op-chart-note')?.textContent || '').trim() : null,
            points: poly ? (poly.getAttribute('points') || '').trim().split(/\s+/).filter(Boolean).length : 0,
            rect: (() => { const r = wrap.getBoundingClientRect(); return { x: Math.round(r.x), y: Math.round(r.y), w: Math.round(r.width), h: Math.round(r.height) }; })(),
        });
    }
    out.sessionCharts = document.querySelectorAll('.op-sparkline:not(.op-history-chart)').length;
    out.historyCharts = document.querySelectorAll('.op-history-chart').length;
    return out;
};

(async () => {
    const browser = await chromium.launch();
    const result = { base: BASE, generated: new Date().toISOString(), routes: {} };
    for (const route of ['dashboard', 'meters', 'inverters']) {
        const ctx = await browser.newContext({ viewport: { width: 1440, height: 900 } });
        const page = await ctx.newPage();
        await page.goto(`${BASE}/#/${route}`, { waitUntil: 'domcontentloaded', timeout: 45000 });
        await page.waitForTimeout(8000);
        const first = await page.evaluate(probe);
        /* Sample presence of each implementation twice a second for 30 s.
         * operator-operations.js:669 re-adds the controller-history panel every
         * 10 s; operator-view.js:528 calls view.replaceChildren() every 5 s,
         * which removes it again. If both are true the panel flickers. */
        const presence = [];
        for (let i = 0; i < 60; i++) {
            presence.push(await page.evaluate(() => ({
                t: Date.now(),
                history: document.querySelectorAll('.op-history-chart').length,
                session: document.querySelectorAll('.op-sparkline:not(.op-history-chart)').length,
            })));
            await page.waitForTimeout(500);
        }
        const second = await page.evaluate(probe);
        const historySeen = presence.filter((p) => p.history > 0).length;
        result.routes[route] = {
            presenceSamples: presence.length,
            historyPanelPresentSamples: historySeen,
            historyPanelPresentPct: Math.round((historySeen / presence.length) * 1000) / 10,
            bothImplementationsOnScreenSamples: presence.filter((p) => p.history > 0 && p.session > 0).length,
            presence,
            elapsedMs: second.t - first.t,
            expectedCyclesAt5s: Math.round((second.t - first.t) / 5000),
            first, second,
            growth: second.charts.map((c, i) => ({
                impl: c.impl, headline: c.headline,
                before: first.charts[i]?.points ?? null, after: c.points,
                delta: c.points - (first.charts[i]?.points ?? 0),
            })),
        };
        console.log(route, JSON.stringify(result.routes[route].growth));
        console.log(`  session sparklines: ${second.sessionCharts}, controller-history charts: ${second.historyCharts}` +
            `, history panel present in ${result.routes[route].historyPanelPresentPct}% of 60 samples` +
            `, both on screen together in ${result.routes[route].bothImplementationsOnScreenSamples}`);
        await ctx.close();
        await new Promise((r) => setTimeout(r, 2000));
    }
    await browser.close();
    fs.writeFileSync(path.join(__dirname, 'chart-duplication.json'), JSON.stringify(result, null, 2));
    console.log('done -> chart-duplication.json');
})();
