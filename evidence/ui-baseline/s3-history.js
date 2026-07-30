/* S3 re-measurement: does /api/operator/history still return HTTP 500 under
 * real browser load now that max_open_sockets=10 and lru_purge_enable=true
 * (web_server.c:149,153) and CONFIG_SPIRAM=y (sdkconfig.defaults:19)?
 *
 * Three phases, read-only GETs only:
 *   A  30 standalone sequential GETs (the 2026-07-29 "curl passes" control)
 *   B  one browser tab on /#/dashboard held open 90 s (the app polls history
 *      every 10 s from operator-operations.js:669)
 *   C  three concurrent tabs on dashboard + meters + inverters for 60 s
 *      (the "real browser load" condition that produced the 500s)
 */
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const BASE = process.env.TARGET || 'http://192.168.100.14';
const HIST = '/api/operator/history';
const out = { base: BASE, generated: new Date().toISOString(), phases: {} };
const tally = (list) => list.reduce((a, s) => { a[s] = (a[s] || 0) + 1; return a; }, {});

const watch = (page, sink) => {
    page.on('response', (r) => {
        if (r.url().includes(HIST)) sink.push({ status: r.status(), t: Date.now() });
    });
    page.on('requestfailed', (r) => {
        if (r.url().includes(HIST)) sink.push({ status: `FAIL ${r.failure()?.errorText}`, t: Date.now() });
    });
};

(async () => {
    /* ---- A: standalone ------------------------------------------------ */
    const a = [];
    for (let i = 0; i < 30; i++) {
        const t0 = Date.now();
        try {
            const r = await fetch(`${BASE}${HIST}?range=15m`);
            const body = await r.text();
            a.push({ status: r.status, ms: Date.now() - t0, bytes: body.length });
        } catch (e) { a.push({ status: `FAIL ${e.message}`, ms: Date.now() - t0 }); }
    }
    out.phases.A_standalone = { n: a.length, statuses: tally(a.map((x) => x.status)), samples: a };
    console.log('A standalone:', JSON.stringify(out.phases.A_standalone.statuses));

    const browser = await chromium.launch();

    /* ---- B: single tab, 90 s ------------------------------------------ */
    const bSink = [];
    const ctxB = await browser.newContext({ viewport: { width: 1440, height: 900 } });
    const pB = await ctxB.newPage();
    watch(pB, bSink);
    await pB.goto(`${BASE}/#/dashboard`, { waitUntil: 'domcontentloaded', timeout: 45000 });
    await pB.waitForTimeout(90000);
    await ctxB.close();
    out.phases.B_single_tab_90s = { n: bSink.length, statuses: tally(bSink.map((x) => x.status)) };
    console.log('B single tab 90s:', JSON.stringify(out.phases.B_single_tab_90s));
    await new Promise((r) => setTimeout(r, 3000));

    /* ---- C: three concurrent tabs, 60 s ------------------------------- */
    const cSink = [];
    const ctxs = [];
    for (const route of ['dashboard', 'meters', 'inverters']) {
        const c = await browser.newContext({ viewport: { width: 1440, height: 900 } });
        const p = await c.newPage();
        watch(p, cSink);
        await p.goto(`${BASE}/#/${route}`, { waitUntil: 'domcontentloaded', timeout: 45000 });
        ctxs.push(c);
    }
    await new Promise((r) => setTimeout(r, 60000));
    /* is the controller still answering an independent client? */
    let independent;
    try {
        const t0 = Date.now();
        const r = await fetch(`${BASE}/api/status`);
        independent = { status: r.status, ms: Date.now() - t0 };
    } catch (e) { independent = { status: `FAIL ${e.message}` }; }
    for (const c of ctxs) await c.close();
    out.phases.C_three_tabs_60s = {
        n: cSink.length, statuses: tally(cSink.map((x) => x.status)),
        independentClientDuringLoad: independent,
    };
    console.log('C three tabs 60s:', JSON.stringify(out.phases.C_three_tabs_60s));

    await browser.close();
    fs.writeFileSync(path.join(__dirname, 's3-history.json'), JSON.stringify(out, null, 2));
    console.log('done -> s3-history.json');
})();
