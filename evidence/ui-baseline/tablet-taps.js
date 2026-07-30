/* checks.js only evaluates the 44x44 tap-target rule when window.__isMobile is
 * set, and audit3.js sets that flag for the 390x844 phone only. The 44x44 floor
 * applies to any coarse pointer, and web/mobile-prelab-fixes.css:52 keys its fix
 * on `@media (pointer: coarse)` — which a 1024x768 tablet also matches. This
 * pass forces the flag at 1024x768 (hasTouch) so the tablet is measured too. */
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const BASE = process.env.TARGET || 'http://192.168.100.14';
const collectIssues = require('../ui-audit-2026-07-29/checks.js');
const ROUTES = ['dashboard', 'meters', 'inverters', 'control', 'alarms',
                'wifi', 'system', 'commissioning', 'readiness', 'engineering'];

(async () => {
    const browser = await chromium.launch();
    const out = { base: BASE, generated: new Date().toISOString(), viewport: '1024x768', runs: [] };
    for (const route of ROUTES) {
        const ctx = await browser.newContext({
            viewport: { width: 1024, height: 768 }, hasTouch: true, isMobile: false,
        });
        const page = await ctx.newPage();
        await page.goto(`${BASE}/#/${route}`, { waitUntil: 'domcontentloaded', timeout: 45000 });
        await page.evaluate((r) => { location.hash = `#/${r}`; }, route);
        await page.waitForTimeout(4000);
        await page.evaluate(() => { window.__isMobile = true; });
        const coarse = await page.evaluate(() => window.matchMedia('(pointer: coarse)').matches);
        const issues = await page.evaluate(collectIssues);
        const taps = issues.filter((i) => i.type === 'tap-target-too-small');
        out.runs.push({ route, pointerCoarse: coarse, tapTargets: taps });
        console.log(`${route}: pointer:coarse=${coarse}, ${taps.length} tap targets < 44x44`);
        await ctx.close();
        await new Promise((r) => setTimeout(r, 1000));
    }
    await browser.close();
    fs.writeFileSync(path.join(__dirname, 'tablet-taps.json'), JSON.stringify(out, null, 2));
    console.log('done -> tablet-taps.json');
})();
