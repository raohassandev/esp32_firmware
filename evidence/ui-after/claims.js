/* Claim verification for the 2026-07-31 "after" audit.
 *
 * The numeric before/after comparison comes from the unchanged baseline harness
 * (../ui-baseline/audit3.js + metrics.js + ../ui-audit-2026-07-29/checks.js).
 * This file adds only the *specific* structural claims of the redesign that the
 * numeric harness does not express, each measured in the rendered DOM:
 *
 *   C1  exactly one chart per page; no browser-session sparkline anywhere
 *   C2  an unmeasured gauge draws a track and NO needle / value arc
 *       (a needle parked at the bottom of the scale is the defect this replaced)
 *   C3  missing data is a visible gap: never interpolated, never plotted at zero
 *   C4  "Returned to normal / never acknowledged" is on the first screen,
 *       not behind a drawer
 *   C5  no operator screen shows register addresses, Modbus/PDU terms, unit ids,
 *       scale factors, poll intervals, or the word "milestone"
 *
 * Read-only: page loads and GETs only.
 */
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const BASE = process.env.TARGET || 'http://192.168.100.14';
const OUT = __dirname;
const ROUTES = ['dashboard', 'meters', 'inverters', 'control', 'alarms',
                'wifi', 'system', 'commissioning', 'readiness', 'engineering'];

const probe = function collectClaims() {
    const vh = window.innerHeight;
    const vw = window.innerWidth;
    const visible = (el) => {
        const r = el.getBoundingClientRect();
        const s = getComputedStyle(el);
        return r.width > 0 && r.height > 0 && s.visibility !== 'hidden' &&
               s.display !== 'none' && s.opacity !== '0';
    };
    const rectOf = (el) => {
        const r = el.getBoundingClientRect();
        return { x: Math.round(r.left), y: Math.round(r.top), w: Math.round(r.width), h: Math.round(r.height) };
    };
    const inFirstScreen = (el) => {
        const r = el.getBoundingClientRect();
        return r.bottom > 0 && r.top < vh && r.right > 0 && r.left < vw;
    };
    const cls = (el) => (typeof el.className === 'string' ? el.className
                        : (el.className && el.className.baseVal) || '');

    /* ---- C1 charts ---------------------------------------------------- */
    const chartSel = 'svg.pvc-svg, .op-sparkline svg, .op-history-chart svg, .sparkline svg, ' +
                     'svg[class*="spark"], canvas, svg.chart';
    const charts = [...document.querySelectorAll(chartSel)].filter(visible).map((el) => {
        const card = el.closest('.pvc-card, .op-card, .card, .panel');
        const cr = card ? card.getBoundingClientRect() : null;
        const r = el.getBoundingClientRect();
        return {
            tag: el.tagName.toLowerCase(), cls: cls(el), ...rectOf(el),
            card: card ? cls(card) : null,
            cardW: cr ? Math.round(cr.width) : null, cardH: cr ? Math.round(cr.height) : null,
            plotPctOfCard: cr && cr.width * cr.height
                ? Math.round(((r.width * r.height) / (cr.width * cr.height)) * 1000) / 10 : null,
            plotHeightPctOfCard: cr && cr.height ? Math.round((r.height / cr.height) * 1000) / 10 : null,
            inFirstScreen: inFirstScreen(el),
        };
    });
    const sparklineNodes = document.querySelectorAll('.op-sparkline, .op-history-chart, [class*="sparkline"]').length;
    const sessionCaption = [...document.querySelectorAll('body *')]
        .filter((el) => el.children.length === 0 &&
                        /browser session|live samples stored in this browser/i.test(el.textContent || ''))
        .map((el) => (el.textContent || '').trim().slice(0, 120));

    /* ---- C2 gauges ----------------------------------------------------- */
    const gauges = [...document.querySelectorAll('.op-gauge')].filter(visible).map((g) => {
        const svg = g.querySelector('svg');
        const kids = svg ? [...svg.children].map((k) => ({
            tag: k.tagName.toLowerCase(), cls: cls(k),
            d: k.getAttribute('d') || null,
            strokeDashoffset: getComputedStyle(k).strokeDashoffset,
            strokeDasharray: getComputedStyle(k).strokeDasharray,
            transform: k.getAttribute('transform') || getComputedStyle(k).transform,
        })) : [];
        const copy = g.querySelector('.op-gauge-copy');
        return {
            cls: cls(g),
            unmeasuredClass: /op-gauge-unmeasured/.test(cls(g)),
            title: (g.querySelector('.op-gauge-copy span') || {}).textContent || null,
            value: (g.querySelector('.op-gauge-copy strong') || {}).textContent || null,
            unit: (g.querySelector('.op-gauge-copy small') || {}).textContent || null,
            copyText: copy ? (copy.textContent || '').trim().slice(0, 120) : null,
            svgChildCount: kids.length,
            svgChildren: kids,
            hasTrack: !!g.querySelector('.op-gauge-track'),
            hasValueArc: !!g.querySelector('.op-gauge-value'),
            /* anything that could be read as a needle: a line, a marker, a
             * rotated element, or any stroked child that is not the track */
            needleCandidates: kids.filter((k) => k.cls !== 'op-gauge-track' &&
                /^(line|polyline|circle|rect|marker|g|polygon)$/.test(k.tag) ||
                /needle|pointer|marker/i.test(k.cls)).map((k) => `${k.tag}.${k.cls}`),
            ...rectOf(g),
        };
    });

    /* ---- C3 gap rendering ---------------------------------------------- */
    const svg = document.querySelector('svg.pvc-svg');
    const chart = svg ? {
        linePaths: [...svg.querySelectorAll('path.pvc-line, .pvc-line')].map((p) => {
            const d = p.getAttribute('d') || '';
            return {
                cls: cls(p),
                subpaths: (d.match(/M/g) || []).length,   /* >1 M = a broken line */
                length: d.length,
                dHead: d.slice(0, 120),
            };
        }),
        gapBands: [...svg.querySelectorAll('.pvc-gap-band')].map((b) => ({
            ...rectOf(b), cls: cls(b), visible: visible(b),
        })),
        commsBars: svg.querySelectorAll('.pvc-comms-bar').length,
        zeroLine: !!svg.querySelector('.pvc-zero-line'),
        ariaLabel: svg.getAttribute('aria-label') || null,
        desc: (svg.querySelector('desc, title') || {}).textContent || null,
    } : null;
    const chartSummary = [...document.querySelectorAll('.pvc-note, .pvc-quality-pill, .pvc-table-note, .pvc-summary, [class*="pvc-"] ')]
        .filter((el) => el.children.length === 0 && (el.textContent || '').trim())
        .map((el) => (el.textContent || '').trim().slice(0, 200)).slice(0, 30);

    /* ---- C4 alarm state on the first screen ----------------------------- */
    const ACK_RE = /never acknowledged/i;
    const ackNodes = [...document.querySelectorAll('body *')]
        .filter((el) => el.children.length === 0 && ACK_RE.test(el.textContent || ''))
        .map((el) => {
            const det = el.closest('details');
            const dlg = el.closest('dialog, [role="dialog"], .drawer, .modal, [hidden]');
            return {
                text: (el.textContent || '').trim().slice(0, 120),
                ...rectOf(el),
                visible: visible(el),
                inFirstScreen: inFirstScreen(el),
                insideDetails: !!det,
                detailsOpen: det ? det.open : null,
                insideDrawerOrDialog: !!dlg,
            };
        });

    /* ---- C5 jargon on operator screens ----------------------------------- */
    const TERMS = [
        ['modbus', /\bmodbus\b/i], ['pdu', /\bpdu\b/i], ['register', /\bregisters?\b/i],
        ['unit id', /\bunit[ _-]?id\b/i], ['slave id', /\bslave[ _-]?id\b/i],
        ['scale factor', /\bscale[ _-]?factor\b/i], ['poll interval', /\bpoll[ _-]?(interval|ms|rate)\b/i],
        ['baud', /\bbaud\b/i], ['crc', /\bcrc\b/i], ['rtu', /\bRTU\b/],
        ['hex address', /\b0x[0-9a-f]{2,}\b/i], ['register address', /\b4[0-9]{4}\b|\b3[0-9]{4}\b/],
        ['milestone', /\bmilestone\b/i], ['coil', /\bcoils?\b/i], ['endian', /\bendian/i],
        ['schema', /\bschema\b/i], ['api path', /\/api\//],
    ];
    const jargon = [];
    for (const el of document.querySelectorAll('body *')) {
        if (el.children.length > 0) continue;
        if (!visible(el)) continue;
        const t = (el.textContent || '').trim().replace(/\s+/g, ' ');
        if (!t) continue;
        for (const [name, re] of TERMS) {
            if (re.test(t)) { jargon.push({ term: name, text: t.slice(0, 140), inFirstScreen: inFirstScreen(el) }); break; }
        }
    }

    return {
        viewport: { w: vw, h: vh },
        charts, chartCount: charts.length, sparklineNodes, sessionCaption,
        gauges, chart, chartSummary, ackNodes, jargon,
        domNodes: document.getElementsByTagName('*').length,
    };
};

(async () => {
    const browser = await chromium.launch();
    const out = { base: BASE, generated: new Date().toISOString(), viewport: '1440x900', runs: [] };
    for (const route of ROUTES) {
        const ctx = await browser.newContext({ viewport: { width: 1440, height: 900 }, colorScheme: 'light' });
        const page = await ctx.newPage();
        const historyPayloads = [];
        page.on('response', async (r) => {
            if (r.url().includes('/api/operator/history') && r.status() === 200) {
                try {
                    const j = await r.json();
                    const series = j.series || j.samples || j.points || [];
                    historyPayloads.push({
                        url: r.url().replace(BASE, ''),
                        topKeys: Object.keys(j).slice(0, 12),
                        sampleCount: Array.isArray(series) ? series.length : null,
                        raw: JSON.stringify(j).slice(0, 400),
                    });
                } catch (e) { /* non-JSON */ }
            }
        });
        try {
            await page.goto(`${BASE}/#/${route}`, { waitUntil: 'domcontentloaded', timeout: 45000 });
            await page.evaluate((r) => { location.hash = `#/${r}`; }, route);
            await page.waitForTimeout(7000);
            const claims = await page.evaluate(probe);
            out.runs.push({ route, ...claims, history: historyPayloads.slice(0, 2) });
            console.log(`${route}: charts=${claims.chartCount} sparklineNodes=${claims.sparklineNodes} ` +
                `gauges=${claims.gauges.length} ack=${claims.ackNodes.length} jargon=${claims.jargon.length}`);
        } catch (e) {
            out.runs.push({ route, error: String(e).slice(0, 200) });
            console.log(`${route}: ERROR ${String(e).slice(0, 120)}`);
        } finally {
            await ctx.close();
            await new Promise((r) => setTimeout(r, 1200));
        }
    }
    await browser.close();
    fs.writeFileSync(path.join(OUT, 'claims.json'), JSON.stringify(out, null, 2));
    console.log('done -> claims.json');
})();
