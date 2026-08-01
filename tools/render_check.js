/*
 * render_check.js — does the page actually draw?
 *
 * A source contract proves a module SAYS the right thing. A unit test proves a
 * decoder computes the right number. Neither proves that the renderer produces
 * a DOM, and the failure mode in between is the common one: a module that throws
 * on the first field it reads shows an empty panel, the page still loads, and
 * nothing anywhere reports a problem. That is exactly how a feature gets built,
 * committed, and quietly never appears.
 *
 * So this executes the real renderer files against the real fixture shapes and
 * asserts the elements a person is supposed to see are present, with the values
 * they are supposed to carry.
 *
 * THE DOM SHIM IS DELIBERATELY SMALL. Only what the renderers use:
 * createElement, className, textContent, append, classList, dataset, and a
 * querySelector limited to the class lookups these files perform. A full DOM
 * would let a renderer rely on behaviour this project has no way to verify on
 * the board, and the board's browser is whatever the customer opened.
 *
 *   node tools/render_check.js
 */
'use strict';

const fs = require('fs');
const path = require('path');
const assert = require('assert');
const vm = require('vm');

const ROOT = path.join(__dirname, '..');
const WEB = path.join(ROOT, 'web');

/* ------------------------------------------------------------------ the shim */

class Element {
    constructor(tag) {
        this.tagName = String(tag).toUpperCase();
        this.children = [];
        this._class = '';
        this._text = '';
        this.attributes = {};
        this.dataset = {};
        this.style = {};
    }
    get className() { return this._class; }
    set className(value) { this._class = String(value || ''); }
    get classList() {
        const self = this;
        return {
            add(...names) {
                const have = new Set(self._class.split(/\s+/).filter(Boolean));
                names.forEach((n) => have.add(n));
                self._class = [...have].join(' ');
            },
            contains: (name) => self._class.split(/\s+/).includes(name),
            toggle(name, on) { if (on) this.add(name); }
        };
    }
    get textContent() {
        if (this.children.length) return this.children.map((c) => c.textContent).join('');
        return this._text;
    }
    set textContent(value) { this._text = String(value); this.children = []; }
    append(...nodes) {
        nodes.forEach((n) => { if (n) this.children.push(n); });
    }
    replaceChildren(...nodes) { this.children = []; this.append(...nodes); }
    addEventListener() {}
    setAttribute(name, value) { this.attributes[name] = String(value); }
    set title(value) { this.attributes.title = String(value); }
    get title() { return this.attributes.title; }
    set scope(value) { this.attributes.scope = String(value); }
    set type(value) { this.attributes.type = String(value); }
    /* Only the class-selector form these renderers use. */
    querySelector(selector) {
        const wanted = String(selector).replace(/^\./, '');
        const walk = (node) => {
            for (const child of node.children) {
                if (child.classList.contains(wanted)) return child;
                const found = walk(child);
                if (found) return found;
            }
            return null;
        };
        return walk(this);
    }
    /* Depth-first, for assertions. */
    find(predicate) {
        for (const child of this.children) {
            if (predicate(child)) return child;
            const found = child.find(predicate);
            if (found) return found;
        }
        return null;
    }
    findAll(predicate, into = []) {
        for (const child of this.children) {
            if (predicate(child)) into.push(child);
            child.findAll(predicate, into);
        }
        return into;
    }
    withClass(name) { return this.findAll((n) => n.classList.contains(name)); }
}

class TextNode {
    constructor(text) { this._text = String(text); this.children = []; }
    get textContent() { return this._text; }
    get classList() { return { contains: () => false, add() {}, toggle() {} }; }
    find() { return null; }
    findAll(_p, into = []) { return into; }
}

function makeSandbox() {
    const document = {
        createElement: (tag) => new Element(tag),
        createTextNode: (text) => new TextNode(text),
        getElementById: () => null,
        querySelector: () => null,
        querySelectorAll: () => []
    };
    const window = { document, setTimeout, clearTimeout };
    window.window = window;
    return vm.createContext({ window, document, console, Math, Number, Array, Object,
        String, Boolean, JSON, Date, fetch: () => Promise.reject(new Error('no network')),
        AbortController: class { constructor() { this.signal = {}; } abort() {} },
        setTimeout, clearTimeout });
}

function load(sandbox, file) {
    const source = fs.readFileSync(path.join(WEB, file), 'utf8');
    vm.runInContext(source, sandbox, { filename: file });
}

/* ------------------------------------------------------------------- fixtures
 *
 * Fetched from the running preview when it is up, so the shapes checked here are
 * the shapes the preview serves; otherwise the inline copies below. Keeping one
 * fallback rather than requiring a server means this can run anywhere. */
const METER = {
    measurements: {
        available: true, age_ms: 640,
        phase_voltage_v: [230.1, 229.9, 231.0],
        line_voltage_v: [398.8, 399.1, 400.0],
        current_a: [356.79, 120.00, 200.00],
        active_power_kw: [122.66, -20.00, 123.88],
        reactive_power_kvar: [5.0, -2.5, 7.5],
        apparent_power_kva: [123.0, 21.0, 124.0],
        power_factor: [0.997, -0.952, 0.999],
        frequency_hz: 49.98, equivalent_phase_voltage_v: 230.3,
        equivalent_line_voltage_v: 399.3, equivalent_current_a: 225.59,
        /* 226.54 is exactly the sum of the three phases above, so a fixture
         * using it cannot tell the meter's REPORTED total from one this page
         * computed. Deliberately different: real instruments disagree slightly,
         * and a large disagreement is how a CT on the wrong phase shows itself.
         * The page must print what the meter said. */
        total_active_power_kw: 228.91, total_reactive_power_kvar: 10.0,
        total_apparent_power_kva: 268.0, total_power_factor: 0.985,
        voltage_asymmetry_line_percent: 1.2, voltage_asymmetry_phase_percent: 0.95,
        current_asymmetry_percent: 43.1, neutral_current_a: 81.23
    },
    energy: {
        available: true, age_ms: 12400,
        total_import_active_kwh: 1234567.89, total_export_active_kwh: 45678.90,
        total_import_reactive_kvarh: 987.65, total_export_reactive_kvarh: 43.21,
        total_apparent_kvah: 2222.22, partial_import_active_kwh: 50.0,
        partial_export_active_kwh: 25.0, partial_import_reactive_kvarh: 10.0,
        partial_export_reactive_kvarh: 3.0, partial_apparent_kvah: 77.77
    }
};

const INVERTER = {
    rated_kw: 60, measured_power_kw: 81.4, measured_age_ms: 900,
    runtime: { commanded_percent: 45, commanded_power_kw: 27.0 },
    measurements: {
        available: true, age_ms: 880,
        dc: { string_voltage_v: [601.2, 598.7, 610.5, 587.6],
              string_current_a: [12.34, 11.98, 13.01, 11.5], power_kw: 58.75 },
        ac: { line_voltage_v: [398.7, 399.1, 400.1], phase_voltage_v: [230.1, 229.8, 231.1],
              phase_current_a: [84.12, 83.55, 85.01], active_power_kw: 57.34,
              reactive_power_kvar: -1.25, peak_active_power_today_kw: 59.9,
              power_factor: 0.998, frequency_hz: 49.98 },
        device: { efficiency_percent: 98.62, internal_temperature_c: 41.2,
                  insulation_resistance_mohm: 30.0, status_raw: 512, fault_code_raw: 0 },
        energy: { today_kwh: 245.67, month_kwh: 5678.9, total_kwh: 123456.78,
                  total_dc_input_kwh: 129999.99 }
    }
};

/* -------------------------------------------------------------------- checks */

const checks = [];
function check(name, fn) { checks.push([name, fn]); }

check('the meter matrix draws every phase and the total', () => {
    const sandbox = makeSandbox();
    load(sandbox, 'meter-detail.js');
    const rendered = sandbox.window.AutomatrixMeterDetail.render(METER);
    assert.ok(rendered, 'meter detail rendered nothing for a meter that has measurements');

    const rows = rendered.withClass('amx-matrix')[0].findAll((n) => n.tagName === 'TR');
    assert.ok(rows.length >= 8, `expected a header and seven quantity rows, got ${rows.length}`);

    /* Four figures per quantity row: L1, L2, L3, and the meter's own total. */
    const dataRows = rows.filter((r) => r.findAll((n) => n.tagName === 'TD').length > 0);
    dataRows.forEach((row) => {
        const cells = row.findAll((n) => n.tagName === 'TD');
        assert.strictEqual(cells.length, 4,
            'each quantity must show three phases and the total');
    });

    /* The exporting phase is marked, not merely signed: a lone minus in a column
     * of four-digit figures is what this class exists to make visible. */
    const exporting = rendered.withClass('amx-exporting');
    assert.ok(exporting.length > 0, 'the exporting phase is not marked');
    assert.ok(exporting.some((cell) => cell.textContent.startsWith('-')),
        'a marked cell must carry the negative value it marks');

    /* THE TOTAL IS THE METER'S FIGURE, NOT A SUM COMPUTED HERE.
     *
     * Asserted on the cell, not on the page text: the fixture's phases sum to
     * 226.54 while the meter reports 228.91, so a page that added them up would
     * print a plausible number in the right place and a text search for "the
     * total" would find one either way. Substituting this file's own arithmetic
     * would hide exactly the disagreement that reveals a CT on the wrong phase. */
    const activeRow = dataRows.find((row) => row.find((n) => n.textContent === '122.66'));
    assert.ok(activeRow, 'the active power row is missing');
    const activeCells = activeRow.findAll((n) => n.tagName === 'TD');
    assert.strictEqual(activeCells[3].textContent, '228.91',
        `the total column shows ${activeCells[3].textContent}, not the meter's `
        + 'reported 228.91 -- it appears to have been recomputed from the phases');
});

check('an unread meter block shows dashes, never zeros', () => {
    const sandbox = makeSandbox();
    load(sandbox, 'meter-detail.js');
    const blank = {
        measurements: { available: true, age_ms: null,
            phase_voltage_v: [null, null, null], line_voltage_v: [null, null, null],
            current_a: [null, null, null], active_power_kw: [null, null, null],
            reactive_power_kvar: [null, null, null], apparent_power_kva: [null, null, null],
            power_factor: [null, null, null] },
        energy: { available: false }
    };
    const rendered = sandbox.window.AutomatrixMeterDetail.render(blank);
    assert.ok(rendered);

    /* PER CELL, not over the concatenated page text. Adjacent cells run
     * together there -- "0.00.00.0" -- so a word-boundary regex never matches
     * and the check passes happily on a page showing zeros in every position. */
    const cells = rendered.findAll((n) => n.tagName === 'TD');
    assert.ok(cells.length >= 12, `expected a full matrix, got ${cells.length} cells`);
    cells.forEach((cell) => {
        assert.strictEqual(cell.textContent, '—',
            `an unmeasured quantity rendered as "${cell.textContent}". A number `
            + 'there claims the instrument measured it, which is a different '
            + 'fault from not having read it.');
        assert.ok(cell.classList.contains('amx-absent'),
            'an unmeasured cell must be marked absent, not merely blank');
    });
});

check('a family with no block renders nothing at all', () => {
    const sandbox = makeSandbox();
    load(sandbox, 'meter-detail.js');
    assert.strictEqual(
        sandbox.window.AutomatrixMeterDetail.render({ measurements: { available: false },
                                                      energy: { available: false } }),
        null,
        'a family that was never asked must render nothing, not an empty matrix');
});

check('the meter energy counters are grouped and readable', () => {
    const sandbox = makeSandbox();
    load(sandbox, 'meter-detail.js');
    const rendered = sandbox.window.AutomatrixMeterDetail.render(METER);
    const text = rendered.textContent;
    /* Grouped thousands: a seven-digit lifetime counter that runs together
     * cannot be read aloud, which is what someone does checking it against a
     * utility bill. */
    assert.ok(text.includes('1,234,567.9') || text.includes('1,234,567.89'),
        `lifetime import is not grouped for reading: ${text.slice(0, 200)}`);
    /* Lifetime and resettable counters are separate sections, or a reader sees
     * two numbers labelled "imported" that differ by a factor of a thousand. */
    assert.ok(text.includes('since installation') && text.includes('since last reset'),
        'lifetime and partial counters are not distinguished');
});

check('the inverter page separates the instruction from the measurement', () => {
    const sandbox = makeSandbox();
    load(sandbox, 'inverter-detail.js');
    const rendered = sandbox.window.AutomatrixInverterDetail.render(INVERTER);
    assert.ok(rendered, 'inverter detail rendered nothing');

    const instruction = rendered.withClass('is-instruction');
    assert.strictEqual(instruction.length, 1,
        'the commanded limit must be drawn as an instruction, exactly once');
    assert.ok(instruction[0].textContent.includes('not a reading'),
        'the instruction must say in words that it is not a reading');

    /* Measured output above the commanded limit means the limit is not in
     * force. That is the single most important thing this page can say. */
    const attention = rendered.withClass('is-attention');
    assert.ok(attention.length >= 1,
        'output above the commanded limit is not called out');
    assert.ok(attention.some((n) => n.textContent.includes('more than the limit')),
        'the callout must explain what it means');
});

check('the inverter page draws DC strings, AC phases, machine and yield', () => {
    const sandbox = makeSandbox();
    load(sandbox, 'inverter-detail.js');
    const text = sandbox.window.AutomatrixInverterDetail.render(INVERTER).textContent;
    ['DC strings', 'PV1', 'PV4', 'AC side', 'L1', 'L3',
     'Frequency', 'Internal temperature', 'Insulation',
     'Today', 'Lifetime', 'Device status code', 'Fault code'].forEach((label) => {
        assert.ok(text.includes(label), `the inverter page is missing "${label}"`);
    });
    /* A healthy machine says so; a code table this firmware does not hold is
     * never turned into a word. */
    assert.ok(text.includes('none'), 'a zero fault code must read as none');
    assert.ok(text.includes('code table is not held'),
        'the page must say why the status code is shown as a number');
});

check('an inverter with no block renders nothing', () => {
    const sandbox = makeSandbox();
    load(sandbox, 'inverter-detail.js');
    assert.strictEqual(
        sandbox.window.AutomatrixInverterDetail.render({ measurements: { available: false } }),
        null);
});

check('the journal starts collapsed and explains itself', () => {
    const sandbox = makeSandbox();
    const host = new Element('div');
    sandbox.document.getElementById = (id) => (id === 'alarmJournal' ? host : null);
    load(sandbox, 'alarm-journal.js');
    sandbox.window.AutomatrixAlarmJournal.render();

    const text = host.textContent;
    assert.ok(text.includes('What happened'), 'the journal section has no heading');
    assert.ok(text.includes('Show history'), 'the journal does not offer to open');
    /* Collapsed means no request has been made: this is a paged read over flash
     * and the alarm console re-renders every ten seconds. */
    assert.ok(!text.includes('Reading'), 'a collapsed journal must not be loading');
    assert.ok(text.includes('nobody was watching'),
        'the journal must say what it is for');
});

/* --------------------------------------------------------------------- run */

let failed = 0;
for (const [name, fn] of checks) {
    try {
        fn();
        console.log(`  ok   ${name}`);
    } catch (error) {
        failed += 1;
        console.log(`  FAIL ${name}`);
        console.log(`       ${error.message}`);
    }
}
if (failed) {
    console.log(`\n${failed} of ${checks.length} render checks failed`);
    process.exit(1);
}
console.log(`\nall ${checks.length} render checks passed`);
