/* Visual + UI-engineering audit, socket-safe.
 * The controller allows only ~4 concurrent client sockets and has LRU purge
 * disabled, so a browser holding keep-alive connections locks everyone out.
 * A fresh context per route, closed immediately, keeps the pool free. */
const { chromium, devices } = require('playwright');
const fs = require('fs');
const path = require('path');

const BASE = process.env.TARGET || 'http://192.168.100.14';
const PASSWORD = process.env.ENG_PASSWORD || '';
const OUT = path.join(__dirname, 'out');
const SHOTS = path.join(OUT, 'shots');
fs.mkdirSync(SHOTS, { recursive: true });

const ROUTES = ['dashboard', 'meters', 'inverters', 'control', 'alarms',
                'wifi', 'system', 'commissioning', 'readiness', 'engineering'];

const COMBOS = [
    { vp: 'desktop', theme: 'light', auth: 'operator' },
    { vp: 'desktop', theme: 'dark', auth: 'operator' },
    { vp: 'mobile', theme: 'light', auth: 'operator' },
    { vp: 'mobile', theme: 'dark', auth: 'operator' },
    { vp: 'desktop', theme: 'light', auth: 'engineering' },
    { vp: 'mobile', theme: 'light', auth: 'engineering' },
];

const VP = {
    desktop: { viewport: { width: 1440, height: 900 }, deviceScaleFactor: 1, isMobile: false, hasTouch: false },
    mobile: { ...devices['iPhone 12'] },
};

const collectIssues = require('./checks.js');

(async () => {
    const browser = await chromium.launch();
    const report = { base: BASE, generated: new Date().toISOString(), runs: [] };

    for (const combo of COMBOS) {
        if (combo.auth === 'engineering' && !PASSWORD) continue;
        for (const route of ROUTES) {
            const label = `${combo.vp}-${combo.theme}-${combo.auth}-${route}`;
            const context = await browser.newContext({ ...VP[combo.vp], colorScheme: combo.theme });
            const page = await context.newPage();
            const consoleMsgs = [];
            const failed = [];
            page.on('console', (m) => {
                if (m.type() === 'error' || m.type() === 'warning') consoleMsgs.push({ type: m.type(), text: m.text().slice(0, 300) });
            });
            page.on('pageerror', (e) => consoleMsgs.push({ type: 'pageerror', text: String(e).slice(0, 300) }));
            page.on('requestfailed', (r) => failed.push({ url: r.url(), error: r.failure()?.errorText }));
            page.on('response', (r) => { if (r.status() >= 400) failed.push({ url: r.url(), status: r.status() }); });

            try {
                if (combo.auth === 'engineering') {
                    const res = await context.request.post(`${BASE}/api/engineering/login`, {
                        data: { password: PASSWORD }, timeout: 20000,
                    });
                    if (!res.ok()) consoleMsgs.push({ type: 'audit', text: `login HTTP ${res.status()}` });
                }
                await page.goto(`${BASE}/#/${route}`, { waitUntil: 'domcontentloaded', timeout: 45000 });
                await page.evaluate((r) => { location.hash = `#/${r}`; }, route);
                await page.waitForTimeout(3000);
                await page.evaluate((m) => { window.__isMobile = m; }, combo.vp === 'mobile');
                const issues = await page.evaluate(collectIssues);
                await page.screenshot({ path: path.join(SHOTS, `${label}.png`), fullPage: true });
                report.runs.push({ ...combo, route, issues, console: consoleMsgs, failed, screenshot: `${label}.png` });
                process.stdout.write(`${label}: ${issues.length} issues, ${consoleMsgs.length} console, ${failed.length} failed\n`);
            } catch (e) {
                report.runs.push({ ...combo, route, error: String(e).slice(0, 200), console: consoleMsgs, failed });
                process.stdout.write(`${label}: ERROR ${String(e).slice(0, 120)}\n`);
            } finally {
                await context.close();               /* release the sockets */
                fs.writeFileSync(path.join(OUT, 'report.json'), JSON.stringify(report, null, 2));
                await new Promise((r) => setTimeout(r, 1200));
            }
        }
    }
    await browser.close();
    console.log('\ndone ->', path.join(OUT, 'report.json'));
})();
