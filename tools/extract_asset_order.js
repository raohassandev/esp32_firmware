/*
 * extract_asset_order.js — reads the serve order out of the firmware.
 *
 * WHY THIS IS A SCRIPT AND NOT A HAND-MAINTAINED LIST.
 *
 * The browser gets ONE app.js and ONE app.css, concatenated by the firmware in
 * the order of the asset arrays in components/web_server/web_server.c. That
 * order is load-bearing twice over: a module that calls another at definition
 * time must come after it, and the card layer must come last in the CSS or the
 * per-module panels win.
 *
 * CMakeLists.txt lists the same files in a DIFFERENT order. A preview built from
 * that list renders a cascade the product does not have -- so it would show a
 * layout nobody will ever see, and hide the one they will. That is worse than no
 * preview, because it is a preview that lies.
 *
 * tools/ui_preview_order.json was therefore kept in step by hand, which lasted
 * exactly as long as somebody remembered. This derives it, so adding an asset
 * and re-running is the whole procedure.
 *
 *   node tools/extract_asset_order.js          rewrite ui_preview_order.json
 *   node tools/extract_asset_order.js --check   fail if it is out of date
 *
 * --check is the form for a contract: it does not edit anything and exits
 * non-zero when the preview would disagree with the firmware.
 */
'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..');
const WEB_SERVER = path.join(ROOT, 'components', 'web_server', 'web_server.c');
const ORDER_FILE = path.join(__dirname, 'ui_preview_order.json');

/* Strips comments so a getter named in prose -- and the arrays are heavily
 * commented, deliberately -- cannot be mistaken for one in the array. */
function stripComments(source) {
    return source.replace(/\/\*[\s\S]*?\*\//g, ' ').replace(/\/\/[^\n]*/g, ' ');
}

/*
 * The firmware's getter names are C identifiers built from the file name with
 * dashes and dots flattened to underscores, so the mapping back is ambiguous in
 * principle: meter_detail_js could be meter-detail.js or meter_detail.js.
 *
 * It is resolved by looking at web/ rather than by guessing: the file that
 * exists is the file the build copied. A name that matches nothing is reported
 * rather than silently dropped, because a preview quietly missing a module is
 * the failure this script exists to prevent.
 */
function resolveAsset(getter, extension, available) {
    /* The base bundle's getters are just web_assets_js and web_assets_css, with
     * no stem at all -- they serve web/app.js and web/app.css. Checked before
     * the suffix is stripped, because "css" does not end in "_css" and the
     * general path would look for a file called css.css. */
    if (getter === `web_assets_${extension}`) return `app.${extension}`;

    const stem = getter
        .replace(/^web_assets_/, '')
        .replace(new RegExp(`_${extension}$`), '');
    const candidates = [
        `${stem.replace(/_/g, '-')}.${extension}`,
        `${stem}.${extension}`,
    ];
    for (const candidate of candidates) {
        if (available.has(candidate)) return candidate;
    }
    return null;
}

function extractArray(source, handlerName, extension, available, problems) {
    const start = source.indexOf(handlerName);
    if (start < 0) throw new Error(`${handlerName} not found in web_server.c`);
    const open = source.indexOf('{', source.indexOf('assets[]', start));
    const close = source.indexOf('};', open);
    if (open < 0 || close < 0) throw new Error(`could not bound the ${extension} array`);

    const body = source.slice(open + 1, close);
    const getters = body.match(/web_assets_[a-z0-9_]+/g) || [];
    return getters.map((getter) => {
        const file = resolveAsset(getter, extension, available);
        if (!file) problems.push(`${getter}: no matching file in web/`);
        return file;
    }).filter(Boolean);
}

function main() {
    const check = process.argv.includes('--check');
    const source = stripComments(fs.readFileSync(WEB_SERVER, 'utf8'));
    const available = new Set(fs.readdirSync(path.join(ROOT, 'web')));
    const problems = [];

    const order = {
        css: extractArray(source, 'css_handler', 'css', available, problems),
        js: extractArray(source, 'js_handler', 'js', available, problems),
    };

    if (problems.length) {
        problems.forEach((problem) => console.error(`asset order: ${problem}`));
        process.exit(1);
    }

    /* Every file in web/ should be served. One that is not is either dead weight
     * in the repository or a module somebody forgot to register -- and the
     * second is invisible until a page silently stops working. Reported, not
     * fatal: a file may legitimately be staged before it is wired up. */
    const served = new Set([...order.css, ...order.js]);
    const unserved = [...available].filter(
        (file) => /\.(js|css)$/.test(file) && !served.has(file));
    if (unserved.length) {
        console.error(`asset order: in web/ but never served: ${unserved.join(', ')}`);
    }

    const rendered = `${JSON.stringify(order, null, 4)}\n`;
    const current = fs.existsSync(ORDER_FILE) ? fs.readFileSync(ORDER_FILE, 'utf8') : '';

    /* Compared with line endings normalised. This repository is edited on
     * Windows and checked out with autocrlf, so a file that round-trips through
     * a different tool comes back with CRLF and is byte-different while being
     * identical in every way this contract is about. A check that fails for that
     * reason teaches people to ignore it, which costs more than the drift it was
     * meant to catch. */
    const same = (a, b) => a.replace(/\r\n/g, '\n') === b.replace(/\r\n/g, '\n');

    if (check) {
        if (!same(rendered, current)) {
            console.error(
                'asset order: tools/ui_preview_order.json is stale. The preview would '
                + 'render a cascade the firmware does not serve. Run: '
                + 'node tools/extract_asset_order.js');
            process.exit(1);
        }
        console.log(`asset order matches the firmware (${order.css.length} css, ${order.js.length} js)`);
        return;
    }

    fs.writeFileSync(ORDER_FILE, rendered);
    console.log(`wrote ${order.css.length} css and ${order.js.length} js assets in firmware order`);
}

main();
