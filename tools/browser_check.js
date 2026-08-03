/*
 * browser_check.js — open every page in a real browser, against the real board.
 *
 * Everything else in this repository checks the source, the JSON on the wire, or
 * a renderer against a DOM shim. None of that catches a page that throws on
 * load, a panel that never appears, or a value that is correct in the payload
 * and wrong on the screen -- which is exactly the class of defect the plant
 * owner kept finding by hand.
 *
 * This loads the controller in Chromium, walks every route, captures a
 * screenshot, and asserts what must be true on screen. It also fails on ANY
 * console error, because a page that logs an exception and carries on is a page
 * whose next section silently did not render.
 *
 *   node tools/browser_check.js [board-ip]
 *
 * Screenshots land in build/screenshots/.
 */
'use strict';

const fs = require('fs');
const path = require('path');
const http = require('http');

const BOARD = process.argv[2] || '192.168.100.14';
const OUT = path.join(__dirname, '..', 'build', 'screenshots');

/* The routes are READ FROM THE PAGE, not listed here.
 *
 * A hand-written list was wrong within minutes: the sidebar says "Pre-lab
 * readiness" and "Network setup" while the routes are `readiness` and `wifi`,
 * so the check reported two blank pages that did not exist. A test that invents
 * its own idea of the product tests the invention.
 */

function fetchJson(pathname) {
    return new Promise((resolve, reject) => {
        http.get({ host: BOARD, path: pathname, timeout: 8000 }, (res) => {
            const chunks = [];
            res.on('data', (c) => chunks.push(c));
            res.on('end', () => {
                try { resolve(JSON.parse(Buffer.concat(chunks).toString('utf8'))); }
                catch (e) { reject(e); }
            });
        }).on('error', reject);
    });
}

(async () => {
    const { chromium } = require('playwright');
    fs.mkdirSync(OUT, { recursive: true });

    /* The board's own truth, to check the screen against. */
    const status = await fetchJson('/api/status');
    const attributed = status.source && status.source.attributed_to;
    console.log(`board: source=${attributed}  measured=${Number(status.grid_power_kw).toFixed(2)} kW`);
    console.log();

    const browser = await chromium.launch();
    const context = await browser.newContext({ viewport: { width: 1440, height: 1000 } });
    const page = await context.newPage();

    /* One load to discover what exists, then walk it. */
    await page.goto(`http://${BOARD}/`, { waitUntil: 'load', timeout: 30000 });
    await page.waitForTimeout(2500);
    const routes = await page.evaluate(() =>
        [...document.querySelectorAll('.page')].map((p) => p.dataset.page).filter(Boolean));
    console.log(`routes found in the shell: ${routes.join(', ')}`);

    /*
     * CAN A PERSON GET THERE?
     *
     * The Generator power page was added, opened correctly by hash, and was
     * walked clean by this very sweep -- and nobody could reach it. The sidebar
     * is built from anchors in index.html while the ROUTES table only supplies
     * the icon and title for anchors that already exist, so a route with no
     * anchor is a page reachable only by someone who already knows the URL.
     *
     * Nothing caught it because every tool here navigates by hash rather than by
     * clicking. A test that reaches a page the way no user can is testing a path
     * the product does not have -- so the check is: does a link exist for it.
     *
     * Anchors are read AFTER load because several are injected at runtime by the
     * modules that own their pages.
     */
    const unreachable = await page.evaluate((known) => {
        const linked = new Set([...document.querySelectorAll('[data-route]')]
            .map((a) => a.dataset.route));
        return known.filter((route) => !linked.has(route));
    }, routes);
    if (unreachable.length) {
        console.log();
        console.log(`${unreachable.length} page(s) exist with no way to reach them:`);
        unreachable.forEach((route) => console.log(`  - ${route}: no navigation link`));
        await browser.close();
        process.exit(1);
    }
    console.log();

    const problems = [];
    const consoleErrors = [];
    page.on('console', (m) => {
        if (m.type() === 'error') consoleErrors.push(m.text());
    });
    page.on('pageerror', (e) => consoleErrors.push(`uncaught: ${e.message}`));

    for (const route of routes) {
        consoleErrors.length = 0;
        await page.goto(`http://${BOARD}/#/${route}`, { waitUntil: 'load', timeout: 30000 });
        /* Long enough for the first status poll to land and the page to draw
         * from it, rather than screenshotting the empty scaffold. */
        await page.waitForTimeout(3500);

        const shot = path.join(OUT, `${route}.png`);
        await page.screenshot({ path: shot, fullPage: true });

        const text = await page.evaluate(() => document.body.innerText);
        const visible = await page.evaluate(() =>
            (document.querySelector('.page.active')?.innerText || '').trim().length);

        /* A page an operator cannot open without signing in must show its
         * LOCK, not an empty frame. "Nothing rendered" and "you may not see
         * this" look identical to a user and are completely different faults. */
        const locked = /engineering|unlock|sign in|authoris|authoriz/i.test(text);
        if (visible < 40 && !locked) {
            problems.push(`${route}: rendered nothing, and did not say why`);
        }
        for (const error of consoleErrors) {
            problems.push(`${route}: console error — ${error}`);
        }

        /*
         * THE DEFECT THE OWNER FOUND BY HAND, checked on the rendered screen.
         *
         * On a single-meter tariff plant the measured power belongs to whichever
         * source the controller resolved. If it says generator, no page may head
         * that figure "Grid power".
         */
        if (attributed === 'generator' && /Grid power/i.test(text)) {
            const badCard = await page.evaluate(() => {
                /* On the ACTIVE page, and visible.
                 *
                 * This looked the element up in the whole document, so it found
                 * the plant overview's card -- hidden, on an inactive page --
                 * and reported every other route as mislabelling the supply.
                 * A check that reads a hidden element on a page the reader is
                 * not on describes nothing anybody can see. */
                const dot = document.querySelector('.page.active #gridDot');
                const head = dot?.closest('.metric-head');
                const card = head?.closest('.metric-card');
                if (!card || card.getClientRects().length === 0) return null;
                return head.innerText.trim();
            });
            if (badCard && /grid/i.test(badCard)) {
                problems.push(`${route}: the plant is on the GENERATOR and a metric card is still headed "${badCard}"`);
            }
        }

        console.log(`  ${route.padEnd(14)} ${visible.toString().padStart(6)} chars   ` +
            `${consoleErrors.length ? consoleErrors.length + ' console errors' : 'clean'}   -> ${path.basename(shot)}`);
    }

    await browser.close();

    console.log();
    if (problems.length) {
        console.log(`${problems.length} problems:`);
        problems.forEach((p) => console.log(`  - ${p}`));
        process.exit(1);
    }
    console.log(`every route rendered, no console errors, screenshots in ${OUT}`);
})().catch((e) => { console.error('browser check failed:', e.message); process.exit(1); });
