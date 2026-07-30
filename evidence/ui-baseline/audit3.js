/* Baseline visual + structural audit, 2026-07-30.
 * Extends evidence/ui-audit-2026-07-29/audit2.js:
 *   - reuses that run's checks.js verbatim for the defect checks
 *   - adds metrics.js (density, prose, duplication, cards, charts, jargon)
 *   - adds full per-route network capture (every request + status)
 *   - adds a 200% text-zoom pass
 *   - 4 viewports x 2 themes instead of 2 x 2
 *
 * Read-only. Issues GET requests only; never posts configuration.
 * A fresh context per route, closed immediately, keeps the socket pool free.
 */
const { chromium, devices } = require('playwright');
const fs = require('fs');
const path = require('path');

const BASE = process.env.TARGET || 'http://192.168.100.14';
const PASSWORD = process.env.ENG_PASSWORD || '';        /* not available: see report */
/* OUT_DIR lets the 2026-07-31 "after" run write into evidence/ui-after/ while
 * executing this identical harness (checks.js + metrics.js unchanged). */
const OUT = process.env.OUT_DIR || __dirname;
const SHOTS = path.join(OUT, 'shots');
const ONLY = process.env.ONLY_ROUTE ? process.env.ONLY_ROUTE.split(',') : null;
const REPORT = process.env.REPORT_NAME || 'report.json';
const SHOT_SUFFIX = process.env.SHOT_SUFFIX || '';
/* 1440x900 + 1920x1080 + 1024x768 + 390x844, or a subset for a spot check */
const ONLY_VP = process.env.ONLY_VP ? process.env.ONLY_VP.split(',') : null;
fs.mkdirSync(SHOTS, { recursive: true });

const ROUTES = ['dashboard', 'meters', 'inverters', 'control', 'alarms',
                'wifi', 'system', 'commissioning', 'readiness', 'engineering'];

const VP = {
    '1440x900': { viewport: { width: 1440, height: 900 }, deviceScaleFactor: 1, isMobile: false, hasTouch: false },
    '1920x1080': { viewport: { width: 1920, height: 1080 }, deviceScaleFactor: 1, isMobile: false, hasTouch: false },
    '1024x768': { viewport: { width: 1024, height: 768 }, deviceScaleFactor: 1, isMobile: false, hasTouch: true },
    '390x844': { ...devices['iPhone 12'], viewport: { width: 390, height: 844 } },
};

const COMBOS = [];
for (const vp of (ONLY_VP || Object.keys(VP))) {
    for (const theme of ['light', 'dark']) COMBOS.push({ vp, theme, access: 'operator', zoom: 1 });
}
/* 200% text-zoom pass, desktop light only */
if (!ONLY_VP) COMBOS.push({ vp: '1440x900', theme: 'light', access: 'operator', zoom: 2 });

const collectIssues = require('../ui-audit-2026-07-29/checks.js');
const collectMetrics = require('./metrics.js');

const summariseNetwork = (reqs) => {
    const byUrl = new Map();
    for (const r of reqs) {
        const u = r.url.replace(BASE, '').replace(/\?.*$/, '');
        if (!byUrl.has(u)) byUrl.set(u, { path: u, count: 0, statuses: {} });
        const e = byUrl.get(u);
        e.count++;
        const s = String(r.status || r.error || 'pending');
        e.statuses[s] = (e.statuses[s] || 0) + 1;
    }
    const endpoints = [...byUrl.values()].sort((a, b) => b.count - a.count);
    return {
        total: reqs.length,
        apiTotal: reqs.filter((r) => r.url.includes('/api/')).length,
        distinctEndpoints: endpoints.length,
        status401: reqs.filter((r) => r.status === 401).length,
        status403: reqs.filter((r) => r.status === 403).length,
        status500: reqs.filter((r) => r.status >= 500).length,
        status4xx: reqs.filter((r) => r.status >= 400 && r.status < 500).length,
        redundantPolls: endpoints.filter((e) => e.path.includes('/api/') && e.count > 1)
            .map((e) => ({ path: e.path, count: e.count })),
        endpoints,
    };
};

(async () => {
    const browser = await chromium.launch();
    const report = { base: BASE, generated: new Date().toISOString(), engineeringAuth: PASSWORD ? 'attempted' : 'unavailable-no-password', runs: [] };

    for (const combo of COMBOS) {
        for (const route of (ONLY || ROUTES)) {
            const access = route === 'engineering' ? 'engineering-locked' : 'operator';
            const zoomTag = combo.zoom === 2 ? '-zoom200' : '';
            const label = `${route}-${combo.vp}-${combo.theme}-${access}${zoomTag}${SHOT_SUFFIX}`;
            const context = await browser.newContext({ ...VP[combo.vp], colorScheme: combo.theme });
            const page = await context.newPage();
            const consoleMsgs = [];
            const requests = [];
            page.on('console', (m) => {
                if (m.type() === 'error' || m.type() === 'warning') consoleMsgs.push({ type: m.type(), text: m.text().slice(0, 300) });
            });
            page.on('pageerror', (e) => consoleMsgs.push({ type: 'pageerror', text: String(e).slice(0, 300) }));
            page.on('requestfailed', (r) => requests.push({ url: r.url(), method: r.method(), error: r.failure()?.errorText }));
            page.on('response', (r) => requests.push({ url: r.url(), method: r.request().method(), status: r.status() }));

            try {
                await page.goto(`${BASE}/#/${route}`, { waitUntil: 'domcontentloaded', timeout: 45000 });
                await page.evaluate((r) => { location.hash = `#/${r}`; }, route);
                if (combo.zoom === 2) {
                    /* Browser text zoom raises the root font size. 200% of the 16px default. */
                    await page.addStyleTag({ content: 'html{font-size:32px !important}' });
                }
                await page.waitForTimeout(6000);      /* let the pollers run at least one cycle */
                await page.evaluate((m) => { window.__isMobile = m; }, combo.vp === '390x844');
                const issues = await page.evaluate(collectIssues);
                const metrics = combo.zoom === 2 ? null : await page.evaluate(collectMetrics);
                await page.screenshot({ path: path.join(SHOTS, `${label}.png`) });
                if (combo.vp === '1440x900' && combo.zoom === 1) {
                    await page.screenshot({ path: path.join(SHOTS, `${label}-full.png`), fullPage: true });
                }
                report.runs.push({
                    route, viewport: combo.vp, theme: combo.theme, access, zoom: combo.zoom,
                    issues, metrics, console: consoleMsgs,
                    network: summariseNetwork(requests), requests,
                    screenshot: `${label}.png`,
                });
                process.stdout.write(`${label}: ${issues.length} issues, ${consoleMsgs.length} console, ` +
                    `${requests.length} reqs (${requests.filter((r) => r.status >= 400).length} >=400)` +
                    (metrics ? `, useful ${metrics.area.usefulPct}%, words ${metrics.wordCounts.visibleWords}` : '') + '\n');
            } catch (e) {
                report.runs.push({ route, viewport: combo.vp, theme: combo.theme, access, zoom: combo.zoom,
                                   error: String(e).slice(0, 240), console: consoleMsgs, requests });
                process.stdout.write(`${label}: ERROR ${String(e).slice(0, 140)}\n`);
            } finally {
                await context.close();                 /* release the sockets */
                fs.writeFileSync(path.join(OUT, REPORT), JSON.stringify(report, null, 2));
                await new Promise((r) => setTimeout(r, 1200));
            }
        }
    }
    await browser.close();
    console.log('\ndone ->', path.join(OUT, REPORT));
})();
