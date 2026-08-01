/*
 * check_asset_order.js — is the bundle order coherent?
 *
 * The browser receives ONE app.js and ONE app.css, concatenated at BUILD time
 * from web/app.js.order and web/app.css.order. Those two files are the source of
 * truth: the firmware embeds what the build produced, and the preview reads the
 * same lists, so there is nothing left to keep in step.
 *
 * That was not always so. The order lived in the assets[] arrays in
 * web_server.c, CMakeLists.txt listed the same files in a DIFFERENT order, and a
 * JSON file held a third copy for the preview. Three lists meant three chances
 * to disagree, and a disagreement is invisible until a page silently stops
 * working. This script is what remains of that problem: with one list there is
 * nothing to reconcile, only to check.
 *
 * What it checks:
 *   1. Every listed file exists. A missing module makes the build fail loudly
 *      now, but catching it here names the line rather than the exception.
 *   2. Every .js and .css in web/ is listed. One that is not is either dead
 *      weight or a module somebody forgot to register -- and the second is
 *      invisible until the page that needs it quietly stops working.
 *   3. Load order is respected: a module that calls another while rendering
 *      comes after it.
 *   4. The card layer is not overridden by a sheet it was meant to replace.
 *
 *   node tools/check_asset_order.js
 */
'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..');
const WEB = path.join(ROOT, 'web');

function readOrder(kind) {
    const file = path.join(WEB, `app.${kind}.order`);
    if (!fs.existsSync(file)) {
        console.error(`asset order: ${file} is missing; it is the source of truth`);
        process.exit(1);
    }
    return fs.readFileSync(file, 'utf8')
        .split(/\r?\n/)
        .map((line) => line.trim())
        .filter((line) => line && !line.startsWith('#'));
}

/* A module that calls another while rendering must be loaded after it. Each of
 * these is a real call site, not a stylistic preference: reversed, the call
 * finds nothing defined and the section it draws is simply absent. */
const PRECEDENCE = [
    ['icons.js', 'operator-view.js'],
    ['operator-proof.js', 'operator-view.js'],
    ['meter-detail.js', 'devices.js'],
    ['inverter-detail.js', 'devices.js'],
    ['alarm-journal.js', 'operator-operations.js'],
];

/* Sheets the card layer is replacing. While both exist, cards.css has to win at
 * equal specificity, so none of these may be served after it. */
const SUPERSEDED = new Set([
    'app.css', 'devices.css', 'em500.css', 'wifi.css', 'theme.css', 'product-mode.css',
]);

function main() {
    const problems = [];
    const js = readOrder('js');
    const css = readOrder('css');
    const listed = new Set([...js, ...css]);

    [...js, ...css].forEach((name) => {
        if (!fs.existsSync(path.join(WEB, name))) {
            problems.push(`${name} is listed in a bundle order but does not exist`);
        }
    });

    fs.readdirSync(WEB)
        .filter((name) => /\.(js|css)$/.test(name))
        .forEach((name) => {
            if (!listed.has(name)) {
                problems.push(
                    `${name} is in web/ but no bundle serves it: either dead weight, `
                    + 'or a module somebody forgot to register');
            }
        });

    PRECEDENCE.forEach(([earlier, later]) => {
        if (js.includes(earlier) && js.includes(later) && js.indexOf(earlier) > js.indexOf(later)) {
            problems.push(
                `${later} calls into ${earlier} while rendering, but ${earlier} is `
                + 'bundled after it, so the call would find nothing defined');
        }
    });

    if (!css.includes('cards.css')) {
        problems.push('cards.css is not bundled at all');
    } else {
        css.slice(css.indexOf('cards.css') + 1)
            .filter((name) => SUPERSEDED.has(name))
            .forEach((name) => {
                problems.push(
                    `${name} is bundled AFTER cards.css, so the per-module panels the `
                    + 'card layer replaces would win the cascade');
            });
    }

    if (problems.length) {
        problems.forEach((problem) => console.error(`asset order: ${problem}`));
        process.exit(1);
    }
    console.log(`asset order is coherent (${css.length} css, ${js.length} js, `
        + 'nothing in web/ orphaned, load order respected)');
}

main();
