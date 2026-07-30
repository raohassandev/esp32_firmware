/* Why does chrome% differ between routes when the shell is identical?
 * Lists every leaf that metrics.js classifies as chrome, per route, with its
 * rect, so the area accounting in UI_BASELINE_AUDIT.md can be explained rather
 * than asserted. Read-only. */
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');
const BASE = process.env.TARGET || 'http://192.168.100.14';

const probe = () => {
    const CHROME_SEL = 'header, nav, footer, aside, .sidebar, .app-nav, .app-header, .topbar, ' +
        '[role="banner"], [role="navigation"], [role="contentinfo"], .shell-header, .shell-nav';
    const vw = innerWidth, vh = innerHeight;
    const out = [];
    for (const el of document.querySelectorAll('body *')) {
        if (el.children.length > 0 && !['BUTTON', 'INPUT', 'SELECT', 'TEXTAREA', 'A'].includes(el.tagName)) continue;
        const r = el.getBoundingClientRect();
        if (r.width <= 0 || r.height <= 0) continue;
        const s = getComputedStyle(el);
        if (s.visibility === 'hidden' || s.display === 'none' || s.opacity === '0') continue;
        if (!el.closest(CHROME_SEL)) continue;
        if (r.bottom <= 0 || r.top >= vh || r.right <= 0 || r.left >= vw) continue;
        out.push({
            sel: el.tagName.toLowerCase() + (el.id ? '#' + el.id : '') +
                 (typeof el.className === 'string' && el.className ? '.' + el.className.trim().split(/\s+/)[0] : ''),
            text: (el.textContent || '').trim().slice(0, 30),
            x: Math.round(r.x), y: Math.round(r.y), w: Math.round(r.width), h: Math.round(r.height),
            areaPct: Math.round((r.width * r.height) / (vw * vh) * 1000) / 10,
        });
    }
    return { count: out.length, totalAreaPct: Math.round(out.reduce((a, o) => a + o.areaPct, 0) * 10) / 10, items: out };
};

(async () => {
    const browser = await chromium.launch();
    const result = {};
    for (const route of ['dashboard', 'alarms', 'control', 'readiness']) {
        const ctx = await browser.newContext({ viewport: { width: 1440, height: 900 } });
        const page = await ctx.newPage();
        await page.goto(`${BASE}/#/${route}`, { waitUntil: 'domcontentloaded', timeout: 45000 });
        await page.evaluate((r) => { location.hash = `#/${r}`; }, route);
        await page.waitForTimeout(6000);
        result[route] = await page.evaluate(probe);
        console.log(route, 'chrome leaves:', result[route].count, 'summed rect area %:', result[route].totalAreaPct);
        result[route].items.sort((a, b) => b.areaPct - a.areaPct).slice(0, 8)
            .forEach((i) => console.log(`   ${i.areaPct}%  ${i.sel} ${i.w}x${i.h} @(${i.x},${i.y}) "${i.text}"`));
        await ctx.close();
        await new Promise((r) => setTimeout(r, 1500));
    }
    await browser.close();
    fs.writeFileSync(path.join(__dirname, 'chrome-breakdown.json'), JSON.stringify(result, null, 2));
    console.log('done -> chrome-breakdown.json');
})();
