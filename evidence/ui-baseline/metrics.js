/* Baseline layout / density metrics. Runs inside the page.
 * Extends the 2026-07-29 harness (checks.js keeps the defect checks; this file
 * adds the quantitative density, prose, duplication and chart measurements the
 * redesign will be graded against).
 *
 * METHOD (stated so the "after" run can repeat it exactly):
 *
 *  Area accounting is done on an 8x8 CSS-pixel grid covering the FIRST viewport
 *  only (0,0 -> innerWidth,innerHeight), because "page area utilisation" is a
 *  statement about what the operator sees without scrolling. Every visible LEAF
 *  element (an element with no element children and non-empty text, or a form
 *  control) is classified, and its rect is painted onto the grid. Later paints
 *  never overwrite a higher-priority class, so overlapping boxes are counted
 *  once. Classes:
 *
 *    value   - leaf whose text contains a digit (a measured/operational number),
 *              or a status pill / badge
 *    label   - leaf with 1..4 words and no digit (the caption a value needs)
 *    control - button, link, input, select, textarea outside the chrome regions
 *    prose   - leaf with >=5 words and no digit: explanatory copy
 *    chrome  - anything inside header / nav / footer / sidebar / [role=banner]
 *              / [role=navigation], plus the page title block
 *    (unpainted cells are empty space: padding, gaps, blank card interior)
 *
 *  usefulPct = (value + label + control) cells / total cells.
 *  This deliberately counts a value's caption as useful and counts sentences as
 *  not useful. Padding is not "bad" per se; it is reported separately as empty.
 */
module.exports = function collectMetrics() {
    const CELL = 8;
    const vw = window.innerWidth;
    const vh = window.innerHeight;
    const cols = Math.ceil(vw / CELL);
    const rows = Math.ceil(vh / CELL);
    const PRIORITY = { value: 5, control: 4, label: 3, prose: 2, chrome: 1 };
    const grid = new Uint8Array(cols * rows);          /* stores PRIORITY code */

    const visible = (el) => {
        const r = el.getBoundingClientRect();
        if (r.width <= 0 || r.height <= 0) return false;
        const s = getComputedStyle(el);
        return s.visibility !== 'hidden' && s.display !== 'none' && s.opacity !== '0';
    };
    const describe = (el) => {
        const id = el.id ? `#${el.id}` : '';
        const cls = typeof el.className === 'string' && el.className
            ? `.${el.className.trim().split(/\s+/).slice(0, 2).join('.')}` : '';
        return `${el.tagName.toLowerCase()}${id}${cls}`;
    };
    const rectOf = (el) => {
        const r = el.getBoundingClientRect();
        return { x: Math.round(r.left), y: Math.round(r.top), w: Math.round(r.width), h: Math.round(r.height) };
    };
    const paint = (r, cls) => {
        const p = PRIORITY[cls];
        const x0 = Math.max(0, Math.floor(r.left / CELL));
        const x1 = Math.min(cols - 1, Math.floor((r.right - 0.01) / CELL));
        const y0 = Math.max(0, Math.floor(r.top / CELL));
        const y1 = Math.min(rows - 1, Math.floor((r.bottom - 0.01) / CELL));
        for (let y = y0; y <= y1; y++) {
            for (let x = x0; x <= x1; x++) {
                const i = y * cols + x;
                if (grid[i] < p) grid[i] = p;
            }
        }
    };

    const CHROME_SEL = 'header, nav, footer, aside, .sidebar, .app-nav, .app-header, .topbar, ' +
                       '[role="banner"], [role="navigation"], [role="contentinfo"], .shell-header, .shell-nav';
    const isChrome = (el) => !!el.closest(CHROME_SEL);
    const CONTROL_TAGS = new Set(['BUTTON', 'INPUT', 'SELECT', 'TEXTAREA', 'A']);
    const DIGIT = /\d/;
    const words = (t) => t.split(/\s+/).filter(Boolean).length;

    /* ---- classify every visible leaf ---------------------------------- */
    const leaves = [];
    for (const el of document.querySelectorAll('body *')) {
        if (el.children.length > 0 && !CONTROL_TAGS.has(el.tagName)) continue;
        if (!visible(el)) continue;
        const text = (el.textContent || el.value || '').trim().replace(/\s+/g, ' ');
        const isControl = CONTROL_TAGS.has(el.tagName) || el.getAttribute('role') === 'button';
        if (!text && !isControl) continue;
        const r = el.getBoundingClientRect();
        const inView = r.bottom > 0 && r.top < vh && r.right > 0 && r.left < vw;
        let cls;
        if (isChrome(el)) cls = 'chrome';
        else if (DIGIT.test(text)) cls = 'value';
        else if (isControl) cls = 'control';
        else if (words(text) >= 5) cls = 'prose';
        else if (text) cls = 'label';
        else cls = 'control';
        leaves.push({ el, text, cls, r, inView, selector: describe(el) });
    }
    /* paint low priority first so high priority wins on overlap */
    for (const cname of ['chrome', 'prose', 'label', 'control', 'value']) {
        for (const l of leaves) if (l.cls === cname && l.inView) paint(l.r, cname);
    }

    const counts = { value: 0, control: 0, label: 0, prose: 0, chrome: 0, empty: 0 };
    for (let i = 0; i < grid.length; i++) {
        const v = grid[i];
        if (v === 5) counts.value++;
        else if (v === 4) counts.control++;
        else if (v === 3) counts.label++;
        else if (v === 2) counts.prose++;
        else if (v === 1) counts.chrome++;
        else counts.empty++;
    }
    const total = grid.length;
    const pct = (n) => Math.round((n / total) * 1000) / 10;
    const area = {
        cellPx: CELL, viewport: { w: vw, h: vh }, cells: total,
        usefulPct: pct(counts.value + counts.control + counts.label),
        valuePct: pct(counts.value), labelPct: pct(counts.label), controlPct: pct(counts.control),
        prosePct: pct(counts.prose), chromePct: pct(counts.chrome), emptyPct: pct(counts.empty),
    };

    /* ---- largest empty rectangles (unpainted cells) -------------------- */
    const emptyGrid = new Uint8Array(grid.length);
    for (let i = 0; i < grid.length; i++) emptyGrid[i] = grid[i] === 0 ? 1 : 0;
    const emptyRegions = [];
    for (let pass = 0; pass < 6; pass++) {
        const heights = new Int32Array(cols);
        let best = null;
        for (let y = 0; y < rows; y++) {
            for (let x = 0; x < cols; x++) heights[x] = emptyGrid[y * cols + x] ? heights[x] + 1 : 0;
            const stack = [];
            for (let x = 0; x <= cols; x++) {
                const h = x === cols ? 0 : heights[x];
                let start = x;
                while (stack.length && stack[stack.length - 1].h >= h) {
                    const top = stack.pop();
                    const areaCells = top.h * (x - top.x);
                    if (!best || areaCells > best.cells) {
                        best = { cells: areaCells, x: top.x, y: y - top.h + 1, w: x - top.x, h: top.h };
                    }
                    start = top.x;
                }
                stack.push({ x: start, h });
            }
        }
        if (!best || best.cells * CELL * CELL < 12000) break;
        emptyRegions.push({
            x: best.x * CELL, y: best.y * CELL, w: best.w * CELL, h: best.h * CELL,
            pctOfViewport: Math.round((best.cells / total) * 1000) / 10,
        });
        for (let y = best.y; y < best.y + best.h; y++) {
            for (let x = best.x; x < best.x + best.w; x++) emptyGrid[y * cols + x] = 0;
        }
    }

    /* ---- prose / word counts ------------------------------------------ */
    let visibleWords = 0, proseWords = 0, viewportWords = 0;
    const proseBlocks = [];
    for (const l of leaves) {
        if (l.cls === 'chrome') continue;
        const w = words(l.text);
        visibleWords += w;
        if (l.inView) viewportWords += w;
        if (l.cls === 'prose') {
            proseWords += w;
            proseBlocks.push({ selector: l.selector, words: w, text: l.text.slice(0, 160), inView: l.inView });
        }
    }
    /* repeated explanatory sentences */
    const proseSeen = new Map();
    for (const p of proseBlocks) {
        const key = p.text.toLowerCase().slice(0, 100);
        if (!proseSeen.has(key)) proseSeen.set(key, { text: p.text, count: 0, where: [] });
        proseSeen.get(key).count++;
        proseSeen.get(key).where.push(p.selector);
    }
    const repeatedProse = [...proseSeen.values()].filter((p) => p.count > 1);

    /* ---- repeated metrics --------------------------------------------- */
    const NUM = /[-+]?\d[\d,]*(?:\.\d+)?/;
    const valSeen = new Map();
    for (const l of leaves) {
        if (l.cls !== 'value') continue;
        const m = l.text.match(NUM);
        if (!m) continue;
        const unit = (l.text.match(/(kWh|kW|kVA|Wh|W|mV|V|mA|A|Hz|%|dBm|ms|°C|MB|KB|bytes)/) || [''])[0];
        const key = `${m[0]}|${unit}`;
        if (!valSeen.has(key)) valSeen.set(key, { value: m[0], unit, count: 0, locations: [] });
        const e = valSeen.get(key);
        e.count++;
        e.locations.push({ selector: l.selector, text: l.text.slice(0, 60), ...rectOf(l.el) });
    }
    const repeatedValues = [...valSeen.values()].filter((v) => v.count > 1)
        .sort((a, b) => b.count - a.count);
    const repeatedWithUnit = repeatedValues.filter((v) => v.unit);

    /* ---- cards --------------------------------------------------------- */
    const CARD_SEL = '.op-card, .op-kpi, .panel, .prelab-check, .card, .tile, .device-card, .pvc-card, ' +
                     '.device-summary-card, .device-runtime-card, .cw-catalog-card, .op-flow-node, .op-alarm-metric';
    const cardEls = [...document.querySelectorAll(CARD_SEL)]
        .filter((el) => visible(el) && !isChrome(el))
        /* .op-card-headline / .op-card-title are captions inside a card, not cards */
        .filter((el) => !/card-(head|title|headline)/.test(el.className || ''));
    const cards = cardEls.map((el) => {
        const r = el.getBoundingClientRect();
        /* fill ratio: fraction of the card's own box covered by painted content */
        const x0 = Math.max(0, Math.floor(r.left / CELL));
        const x1 = Math.min(cols - 1, Math.floor((r.right - 0.01) / CELL));
        const y0 = Math.max(0, Math.floor(r.top / CELL));
        const y1 = Math.min(rows - 1, Math.floor((r.bottom - 0.01) / CELL));
        let filled = 0, cells = 0;
        for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) { cells++; if (grid[y * cols + x]) filled++; }
        const s = getComputedStyle(el);
        return {
            selector: describe(el), ...rectOf(el),
            fixedHeight: s.height !== 'auto' && (el.style.height || s.minHeight !== '0px'),
            minHeight: s.minHeight,
            fillPct: cells ? Math.round((filled / cells) * 1000) / 10 : null,
            inView: r.bottom > 0 && r.top < vh,
            areaPctOfViewport: Math.round(((r.width * r.height) / (vw * vh)) * 1000) / 10,
        };
    });

    /* ---- charts -------------------------------------------------------- */
    const CHART_SEL = 'canvas, .op-sparkline svg, .op-gauge svg, .op-history-chart svg, ' +
                      '.sparkline svg, .chart svg, svg[class*="spark"], svg.chart, svg.pvc-svg';
    const charts = [...document.querySelectorAll(CHART_SEL)]
        .filter(visible).map((el) => {
            const r = el.getBoundingClientRect();
            const card = el.closest(CARD_SEL);
            const cr = card ? card.getBoundingClientRect() : null;
            const wrap = el.closest('.op-sparkline, .op-gauge, .chart, .sparkline');
            const poly = el.querySelector('polyline');
            return {
                selector: describe(el), tag: el.tagName.toLowerCase(), ...rectOf(el),
                /* which of the two competing implementations drew this */
                impl: wrap && wrap.classList.contains('op-history-chart') ? 'controller-history (operator-operations.js:520)'
                    : wrap && wrap.classList.contains('op-sparkline') ? 'browser-session sparkline (operator-view.js:71)'
                    : wrap && wrap.classList.contains('op-gauge') ? 'gauge (operator-view.js:88)' : 'other',
                points: poly ? (poly.getAttribute('points') || '').trim().split(/\s+/).filter(Boolean).length : null,
                card: card ? describe(card) : null,
                cardH: cr ? Math.round(cr.height) : null,
                cardW: cr ? Math.round(cr.width) : null,
                plotPctOfCard: cr && cr.width * cr.height
                    ? Math.round(((r.width * r.height) / (cr.width * cr.height)) * 1000) / 10 : null,
                plotHeightPctOfCard: cr && cr.height ? Math.round((r.height / cr.height) * 1000) / 10 : null,
            };
        });

    /* ---- below the fold ------------------------------------------------ */
    const belowFold = leaves.filter((l) => l.cls === 'value' && l.r.top >= vh)
        .map((l) => ({ selector: l.selector, text: l.text.slice(0, 60), top: Math.round(l.r.top) }));

    /* ---- developer terminology ----------------------------------------- */
    const TERMS = [
        ['modbus', /\bmodbus\b/i], ['pdu', /\bpdu\b/i], ['register', /\bregisters?\b/i],
        ['holding register', /\bholding register/i], ['coil', /\bcoils?\b/i],
        ['unit id', /\bunit[ _-]?id\b/i], ['slave id', /\bslave[ _-]?id\b/i],
        ['scale factor', /\bscale[ _-]?factor\b|\bscale\b\s*[:=]/i],
        ['poll interval', /\bpoll[ _-]?(interval|ms|rate)\b/i],
        ['timeout', /\btimeout(_ms)?\b/i], ['baud', /\bbaud\b/i], ['endian', /\bendian/i],
        ['crc', /\bcrc\b/i], ['rtu', /\bRTU\b/], ['tcp port 502', /\b502\b/],
        ['hex address', /\b0x[0-9a-f]{2,}\b/i], ['raw json', /[{[]\s*"[a-z_]+"\s*:/],
        ['schema', /\bschema\b/i], ['milestone', /\bmilestone\b/i],
        ['later milestone', /scheduled for a later milestone/i],
        ['endpoint/api path', /\/api\//],
        ['firmware identifier', /\bsha\b|\bcommit\b|-dirty\b/i],
        ['heap/psram', /\bheap\b|\bpsram\b/i],
        ['snake_case key', /\b[a-z]+_[a-z]+(_[a-z]+)*\b/],
    ];
    const jargon = [];
    for (const l of leaves) {
        for (const [name, re] of TERMS) {
            if (re.test(l.text)) {
                jargon.push({ term: name, selector: l.selector, text: l.text.slice(0, 100), inView: l.inView });
                break;
            }
        }
    }

    /* ---- misc ---------------------------------------------------------- */
    return {
        area, emptyRegions,
        wordCounts: { visibleWords, proseWords, viewportWords, proseBlockCount: proseBlocks.length },
        repeatedProse,
        repeatedValues: repeatedValues.slice(0, 40),
        repeatedValueCount: repeatedValues.length,
        repeatedValueWithUnitCount: repeatedWithUnit.length,
        cards: {
            count: cards.length,
            inViewCount: cards.filter((c) => c.inView).length,
            underfilled: cards.filter((c) => c.inView && c.fillPct !== null && c.fillPct < 40),
            list: cards,
        },
        charts,
        belowFold: { count: belowFold.length, items: belowFold.slice(0, 40) },
        jargon,
        dom: {
            nodes: document.getElementsByTagName('*').length,
            scrollHeight: document.documentElement.scrollHeight,
            foldsToScroll: Math.round((document.documentElement.scrollHeight / vh) * 10) / 10,
        },
        topProse: proseBlocks.sort((a, b) => b.words - a.words).slice(0, 10),
    };
};
