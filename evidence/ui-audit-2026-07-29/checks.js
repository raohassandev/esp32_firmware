/* Runs inside the page. Collects measurable UI defects. */
module.exports = function collectIssues() {
    const issues = [];
    const vw = window.innerWidth;

    const visible = (el) => {
        const r = el.getBoundingClientRect();
        const s = getComputedStyle(el);
        return r.width > 0 && r.height > 0 && s.visibility !== 'hidden' && s.display !== 'none' && s.opacity !== '0';
    };
    const describe = (el) => {
        const id = el.id ? `#${el.id}` : '';
        const cls = typeof el.className === 'string' && el.className
            ? `.${el.className.trim().split(/\s+/).slice(0, 2).join('.')}` : '';
        return `${el.tagName.toLowerCase()}${id}${cls}`;
    };

    /* 1. The page body must never scroll sideways. */
    const docW = document.documentElement.scrollWidth;
    if (docW > vw + 1) {
        issues.push({ type: 'page-horizontal-overflow', detail: `scrollWidth ${docW} > viewport ${vw} (by ${docW - vw}px)` });
        for (const el of document.querySelectorAll('body *')) {
            if (!visible(el)) continue;
            const r = el.getBoundingClientRect();
            if (r.right > vw + 1) {
                const s = getComputedStyle(el);
                if (s.overflowX === 'auto' || s.overflowX === 'scroll') continue;
                issues.push({ type: 'element-overflows-viewport', selector: describe(el),
                              detail: `right edge ${Math.round(r.right)}px > viewport ${vw}px` });
            }
        }
    }

    /* 2. Wide content must scroll inside its own container. */
    for (const el of document.querySelectorAll('table, pre, .table-wrap, [data-scroll]')) {
        if (!visible(el)) continue;
        const s = getComputedStyle(el);
        const p = el.parentElement;
        const ps = p ? getComputedStyle(p) : null;
        const scrollable = s.overflowX === 'auto' || s.overflowX === 'scroll' ||
                           (ps && (ps.overflowX === 'auto' || ps.overflowX === 'scroll'));
        if (el.scrollWidth > el.clientWidth + 1 && !scrollable) {
            issues.push({ type: 'wide-content-not-scrollable', selector: describe(el),
                          detail: `content ${el.scrollWidth}px in ${el.clientWidth}px box, no overflow-x` });
        }
    }

    /* 3. Text clipped by its container. */
    for (const el of document.querySelectorAll('button, a, label, h1, h2, h3, td, th, .value, .metric')) {
        if (!visible(el)) continue;
        const s = getComputedStyle(el);
        if (s.overflow === 'visible' || s.textOverflow === 'ellipsis') continue;
        if (el.clientHeight > 0 && el.scrollHeight > el.clientHeight + 2) {
            issues.push({ type: 'text-clipped-vertically', selector: describe(el),
                          detail: `content ${el.scrollHeight}px in ${el.clientHeight}px`,
                          text: (el.textContent || '').trim().slice(0, 50) });
        }
    }

    /* 4. Tap targets. 44x44 CSS px is the accessibility floor. */
    if (window.__isMobile) {
        for (const el of document.querySelectorAll('button, a[href], input, select, textarea, [role="button"]')) {
            if (!visible(el)) continue;
            const r = el.getBoundingClientRect();
            if (r.width < 44 || r.height < 44) {
                issues.push({ type: 'tap-target-too-small', selector: describe(el),
                              detail: `${Math.round(r.width)}x${Math.round(r.height)}px < 44x44`,
                              text: (el.textContent || el.value || '').trim().slice(0, 40) });
            }
        }
    }

    /* 5. Accessible names and labelling. */
    for (const el of document.querySelectorAll('button, [role="button"]')) {
        if (!visible(el)) continue;
        const name = (el.textContent || '').trim() || el.getAttribute('aria-label') || el.getAttribute('title');
        if (!name) issues.push({ type: 'control-without-accessible-name', selector: describe(el) });
    }
    for (const el of document.querySelectorAll('input, select, textarea')) {
        if (!visible(el) || el.type === 'hidden') continue;
        const labelled = el.getAttribute('aria-label') || el.getAttribute('aria-labelledby') ||
                         (el.id && document.querySelector(`label[for="${CSS.escape(el.id)}"]`)) || el.closest('label');
        if (!labelled) issues.push({ type: 'input-without-label', selector: describe(el) });
    }
    for (const el of document.querySelectorAll('img')) {
        if (!visible(el)) continue;
        if (el.getAttribute('alt') === null) issues.push({ type: 'image-without-alt', selector: describe(el) });
    }

    /* 6. Duplicate ids break getElementById and label[for]. */
    const seen = new Map();
    for (const el of document.querySelectorAll('[id]')) seen.set(el.id, (seen.get(el.id) || 0) + 1);
    for (const [id, n] of seen) if (n > 1) issues.push({ type: 'duplicate-element-id', detail: `id "${id}" x${n}` });

    /* 7. Contrast against nearest opaque background (approximate; confirm manually). */
    const parseRGB = (c) => {
        const m = c.match(/rgba?\(([^)]+)\)/);
        if (!m) return null;
        const p = m[1].split(',').map(parseFloat);
        return { r: p[0], g: p[1], b: p[2], a: p.length > 3 ? p[3] : 1 };
    };
    const lum = ({ r, g, b }) => {
        const f = (v) => { v /= 255; return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); };
        return 0.2126 * f(r) + 0.7152 * f(g) + 0.0722 * f(b);
    };
    const bgOf = (el) => {
        let n = el;
        while (n && n !== document.documentElement) {
            const c = parseRGB(getComputedStyle(n).backgroundColor);
            if (c && c.a > 0.9) return c;
            n = n.parentElement;
        }
        const c = parseRGB(getComputedStyle(document.body).backgroundColor);
        return c && c.a > 0.9 ? c : { r: 255, g: 255, b: 255, a: 1 };
    };
    for (const el of document.querySelectorAll('p, span, a, button, label, h1, h2, h3, h4, td, th, small, li')) {
        if (!visible(el) || el.children.length > 0) continue;
        const txt = (el.textContent || '').trim();
        if (!txt) continue;
        const s = getComputedStyle(el);
        const fg = parseRGB(s.color);
        if (!fg || fg.a < 0.9) continue;
        const bg = bgOf(el);
        const ratio = (Math.max(lum(fg), lum(bg)) + 0.05) / (Math.min(lum(fg), lum(bg)) + 0.05);
        const size = parseFloat(s.fontSize);
        const large = size >= 24 || (size >= 18.66 && parseInt(s.fontWeight, 10) >= 700);
        const need = large ? 3.0 : 4.5;
        if (ratio < need) {
            issues.push({ type: 'low-contrast-text', selector: describe(el),
                          detail: `ratio ${ratio.toFixed(2)} < ${need} (${s.color} on rgb(${Math.round(bg.r)},${Math.round(bg.g)},${Math.round(bg.b)}), ${size}px)`,
                          text: txt.slice(0, 40) });
        }
    }

    return issues;
};
