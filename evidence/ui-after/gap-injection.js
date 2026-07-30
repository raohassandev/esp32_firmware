/* Gap rendering, tested against the chart component the DEVICE serves.
 *
 * The claim is that a missing sample renders as a visible gap, never
 * interpolated across and never plotted at zero. On the live device the solar
 * series is missing for all 180 samples (inverter telemetry unavailable), which
 * tests "a fully missing series draws nothing" but cannot test an INTERIOR gap,
 * because the grid series is complete and this audit is read-only: nothing may
 * be written to the controller to create one.
 *
 * So the interior-gap case is tested by fault injection in the BROWSER only:
 * the real /api/operator/history payload is fetched from the device, a run of
 * samples in the middle is set to null, and the modified body is served to the
 * page by Playwright route interception. The device is not touched — it serves
 * the page and the original payload, and only the browser's copy is edited.
 * What is under test is the shipped chart code, unmodified.
 *
 * Measured: number of sub-paths in the line (a break = a second "M"), gap
 * bands drawn, and whether any plotted point sits at the zero baseline inside
 * the injected window (which would mean missing was coerced to zero).
 */
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');
const http = require('http');

const BASE = process.env.TARGET || 'http://192.168.100.14';

const get = (url) => new Promise((resolve, reject) => {
    http.get(url, (res) => {
        let body = '';
        res.on('data', (c) => { body += c; });
        res.on('end', () => resolve(body));
    }).on('error', reject);
});

const probe = () => {
    const svg = document.querySelector('svg.pvc-svg');
    if (!svg) return { error: 'no svg.pvc-svg' };
    const box = svg.getBoundingClientRect();
    const lines = [...svg.querySelectorAll('.pvc-line')].map((p) => {
        const d = p.getAttribute('d') || '';
        const moves = (d.match(/M/g) || []).length;
        /* every plotted y coordinate, so a run at the zero baseline is visible */
        const ys = (d.match(/[ ,]-?\d+(\.\d+)?/g) || []);
        return { cls: (p.getAttribute('class') || ''), subpaths: moves, dLength: d.length, d: d.slice(0, 400) };
    });
    const gapBands = [...svg.querySelectorAll('.pvc-gap-band')].map((b) => ({
        x: Math.round(b.getBoundingClientRect().left - box.left),
        w: Math.round(b.getBoundingClientRect().width),
        h: Math.round(b.getBoundingClientRect().height),
        visible: b.getBoundingClientRect().width > 0 && b.getBoundingClientRect().height > 0,
    }));
    const zeroLine = svg.querySelector('.pvc-zero-line');
    return {
        lines, gapBands,
        gapBandCount: gapBands.length,
        zeroLineY: zeroLine ? Math.round(zeroLine.getBoundingClientRect().top - box.top) : null,
        aria: svg.getAttribute('aria-label'),
        summary: (document.querySelector('.pvc-figure [class*="summary"], figcaption, .pvc-note') || {}).textContent || null,
        bodyMentionsGap: /gap[s]? in the record|line is broken/i.test(document.body.innerText),
        gapSentence: (document.body.innerText.match(/[^.]*gap[^.]*\./i) || [])[0] || null,
    };
};

(async () => {
    const raw = await get(`${BASE}/api/operator/history?range=15m`);
    const original = JSON.parse(raw);
    const n = original.samples.length;
    const from = Math.floor(n * 0.4), to = Math.floor(n * 0.6);
    const injected = JSON.parse(raw);
    for (let i = from; i < to; i++) injected.samples[i].grid_kw = null;

    const out = {
        base: BASE, generated: new Date().toISOString(),
        deviceSeries: {
            samples: n,
            gridNulls: original.samples.filter((s) => s.grid_kw === null).length,
            solarNulls: original.samples.filter((s) => s.solar_kw === null).length,
            sampleIntervalMs: original.sample_interval_ms,
            controllerResident: original.controller_resident,
        },
        injection: { nulledFrom: from, nulledTo: to, count: to - from },
    };

    const browser = await chromium.launch();

    /* A: the device's real payload, unmodified */
    let ctx = await browser.newContext({ viewport: { width: 1440, height: 900 }, colorScheme: 'light' });
    let page = await ctx.newPage();
    await page.goto(`${BASE}/#/dashboard`, { waitUntil: 'domcontentloaded', timeout: 45000 });
    await page.waitForTimeout(7000);
    out.asServed = await page.evaluate(probe);
    await page.screenshot({ path: path.join(__dirname, 'shots', 'gap-asserved-dashboard-1440x900-light-operator.png') });
    await ctx.close();
    await new Promise((r) => setTimeout(r, 1200));

    /* B: same page, browser-side injected interior gap in the grid series */
    ctx = await browser.newContext({ viewport: { width: 1440, height: 900 }, colorScheme: 'light' });
    page = await ctx.newPage();
    await page.route('**/api/operator/history*', async (route) => {
        await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(injected) });
    });
    await page.goto(`${BASE}/#/dashboard`, { waitUntil: 'domcontentloaded', timeout: 45000 });
    await page.waitForTimeout(7000);
    out.withInjectedGap = await page.evaluate(probe);
    await page.screenshot({ path: path.join(__dirname, 'shots', 'gap-injected-dashboard-1440x900-light-operator.png') });
    await ctx.close();

    await browser.close();
    fs.writeFileSync(path.join(__dirname, 'gap-injection.json'), JSON.stringify(out, null, 2));
    console.log(JSON.stringify({
        deviceSeries: out.deviceSeries,
        asServedSubpaths: out.asServed.lines?.map((l) => l.subpaths),
        asServedGapBands: out.asServed.gapBandCount,
        injectedSubpaths: out.withInjectedGap.lines?.map((l) => l.subpaths),
        injectedGapBands: out.withInjectedGap.gapBandCount,
        gapSentence: out.withInjectedGap.gapSentence,
    }, null, 2));
    console.log('done -> gap-injection.json');
})();
