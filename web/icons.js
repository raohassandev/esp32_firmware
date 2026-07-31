/* icons.js — the icon set, as inline SVG paths.
 *
 * OWNS: window.AutomatrixIcons. Nothing renders here; it hands back an <svg>.
 *
 * WHY THIS REPLACES THE TEXT GLYPHS. The navigation and the device rows used
 * characters — ⌂ ▤ ◇ ⇄ ⚙ ≈ — chosen because they were free. Their cost is that
 * every platform draws them from a different font: they arrive at different
 * weights, different baselines and sometimes different widths, so a row of them
 * never lines up and the interface reads as unfinished on precisely the tablets
 * this product ships on. Two of them render as colour emoji on some Android
 * builds.
 *
 * These are Material Design Icons paths on a 24x24 grid (Apache-2.0, and the
 * paths are data rather than code). Only the ones actually used are here —
 * about a dozen, a few kilobytes — because this is served from an ESP32's flash
 * alongside the firmware and a full icon font would be most of the web budget.
 *
 * NO EMOJI, EVER. A coloured pictogram on a control screen is at the mercy of
 * whichever font the operator's tablet happens to ship, and a safety state must
 * not be rendered by something outside this repository.
 */
(() => {
    'use strict';

    const PATHS = {
        /* navigation */
        home: 'M10 20v-6h4v6h5v-8h3L12 3 2 12h3v8z',
        grid: 'M11 20v-4H8v-2h3v-2H8V6h3V4H5v16zm2 0h6V4h-6v2h3v4h-3v2h3v2h-3z',
        solar: 'M4 4h16v10H4zm-2 12h20v2H2zm10 3 3 3H9z',
        control: 'M3 17v2h6v-2zm0-8v2h10V9zM3 5v2h14V5zm12 16v-2h6v-2h-6v-2h-2v6zM7 9v2h14V9zm4-4v6h2V5z',
        alarm: 'M12 2 1 21h22zm0 5 7.5 12h-15zm-1 4v4h2v-4zm0 5v2h2v-2z',
        wifi: 'M12 21 1 8a17 17 0 0 1 22 0zm0-3.5 6.3-7.5a13 13 0 0 0-12.6 0z',
        gear: 'M12 15.5A3.5 3.5 0 1 1 15.5 12 3.5 3.5 0 0 1 12 15.5m7.4-2.5a7.5 7.5 0 0 0 0-2l2-1.6-2-3.4-2.4 1a7.6 7.6 0 0 0-1.7-1L14.9 3H9.1l-.4 2.6a7.6 7.6 0 0 0-1.7 1l-2.4-1-2 3.4L4.6 11a7.5 7.5 0 0 0 0 2l-2 1.6 2 3.4 2.4-1a7.6 7.6 0 0 0 1.7 1l.4 2.6h5.8l.4-2.6a7.6 7.6 0 0 0 1.7-1l2.4 1 2-3.4z',
        check: 'M9 16.2 4.8 12l-1.4 1.4L9 19 21 7l-1.4-1.4z',
        clipboard: 'M19 3h-4.2a3 3 0 0 0-5.6 0H5a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2V5a2 2 0 0 0-2-2m-7 0a1 1 0 1 1-1 1 1 1 0 0 1 1-1M7 7h10V5h2v14H5V5h2z',
        lock: 'M18 8h-1V6a5 5 0 0 0-10 0v2H6a2 2 0 0 0-2 2v10a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V10a2 2 0 0 0-2-2M9 6a3 3 0 0 1 6 0v2H9zm3 12a2 2 0 1 1 2-2 2 2 0 0 1-2 2',

        /* device and state */
        meter: 'M12 2a10 10 0 1 0 10 10A10 10 0 0 0 12 2m0 18a8 8 0 1 1 8-8 8 8 0 0 1-8 8m.5-13h-1v6l4.7 2.9.5-.9-4.2-2.5z',
        inverter: 'M4 3h16a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2m0 2v14h16V5zm7 2h2l-3 5h3l-4 6 1-5H7z',
        generator: 'M6 4h12a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2m1 3v2h10V7zm0 4v6h4v-6zm6 0v2h4v-2zm0 4v2h4v-2z',
        offline: 'M2.3 3.7 1 5l3 3a17 17 0 0 0-3 0L12 21l4.4-5.2 3.3 3.3 1.3-1.3zM12 3a17 17 0 0 1 11 5l-2.3 2.7A13 13 0 0 0 12 7z'
    };

    /* Returns a fresh element every call. A shared node would be moved by the
     * second caller rather than copied, which shows up as an icon that vanishes
     * from one row when another renders. */
    function icon(name) {
        const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
        svg.setAttribute('viewBox', '0 0 24 24');
        /* Decorative: the accessible name is always the adjacent text, so a
         * screen reader must not announce the glyph as well. */
        svg.setAttribute('aria-hidden', 'true');
        svg.setAttribute('focusable', 'false');
        const path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
        path.setAttribute('d', PATHS[name] || PATHS.gear);
        svg.append(path);
        return svg;
    }

    window.AutomatrixIcons = { icon, names: () => Object.keys(PATHS) };
})();
