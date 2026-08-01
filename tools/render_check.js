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
    constructor(tag, namespace) {
        this.tagName = String(tag).toUpperCase();
        /* Recorded, because an SVG element built with createElement is a
         * different thing from one built with createElementNS: the browser lays
         * the first out as an unknown inline box and draws nothing at all. A
         * shim that collapsed the two would pass a page that renders blank. */
        this.namespaceURI = namespace || 'http://www.w3.org/1999/xhtml';
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
    setAttribute(name, value) {
        this.attributes[name] = String(value);
        /* A real DOM reflects the class ATTRIBUTE into classList, and SVG
         * elements are created that way because they have no className setter
         * that behaves like an HTML one. Without this the shim reports every SVG
         * node as class-less, and a check for "the flow paths are marked
         * inactive" passes by finding nothing at all. */
        if (name === 'class') this._class = String(value);
    }
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
        /* The flow diagram builds real SVG. Namespaced creation is a different
         * call and a renderer that used createElement for it would produce
         * elements the browser lays out as unknown inline boxes -- which draws
         * nothing, silently. */
        createElementNS: (ns, tag) => new Element(tag, ns),
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
    /* 45% on a x10 register is the word 450. The check below asserts the WORD,
     * because that is the only place a wrong scale is visible before it is
     * sent -- the readback would echo a wrong value, decode it with the same
     * wrong scale, agree with the request and report CONFIRMED. */
    command_preview: { available: true, share_kw: 27, percent: 45, register: 40125,
        function: 6, raw_units_per_percent: 10, words: [450],
        would_write: false, blocked_by: 'this profile is not permitted to write' },
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

/*
 * THE COLUMN THAT MEANT TWO THINGS.
 *
 * Measured on the installed EM-500, 2026-08-01: the meter's whole-installation
 * voltage is the AVERAGE of the three phases (249.7/249.3/248.8 -> 249.25) while
 * its whole-installation current is their SUM (307.4/309.4/312.6 -> 929.4).
 * Under one column headed "Total" that told the reader 249 V was the total of
 * three 249 V phases, and invited them to read the current the same way.
 */
check('the meter column says which kind of total each row carries', () => {
    const sandbox = makeSandbox();
    load(sandbox, 'meter-detail.js');
    const rendered = sandbox.window.AutomatrixMeterDetail.render(METER);
    const text = rendered.textContent;

    assert.ok(!text.includes('Total'),
        '"Total" is wrong for the voltage rows, where the meter reports an average');
    assert.ok(text.includes('average'),
        'the voltage rows must say their meter figure is an average');
    assert.ok(text.includes('sum of the three'),
        'the current row must say its meter figure is a sum');
});

/*
 * A REGISTER THIS FIRMWARE DOES NOT UNDERSTAND IS NOT GIVEN A NAME.
 *
 * The manual calls 0048H "Neutral Current". On the installed meter it reported
 * 930.8 A while the three phases carried 307-313 A and disagreed by 0.4% -- a
 * balanced load puts a few amps in the neutral, not three times the phase
 * current. It is 98.8% of the arithmetic sum of the phases.
 *
 * It is still acquired and still published, because an engineer chasing it needs
 * it. It must not appear on a page under that name: somebody sizes a neutral
 * conductor from a plausible number with a confident label.
 */
check('the unverified neutral register is not drawn as a measurement', () => {
    const sandbox = makeSandbox();
    load(sandbox, 'meter-detail.js');
    const withNeutral = JSON.parse(JSON.stringify(METER));
    withNeutral.measurements.neutral_current_a = 930.8;
    const text = sandbox.window.AutomatrixMeterDetail.render(withNeutral).textContent;

    /* On the LABEL, not on the word. "V phase-neutral" is the unit of the
     * voltage row and is exactly right; banning the word would forbid it. */
    assert.ok(!/neutral current/i.test(text),
        'the page names a register whose meaning contradicts the instrument');
    assert.ok(!text.includes('930.8'),
        'the unverified value is drawn, which is the same claim by another route');
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

/* ------------------------------------------------------------- energy flow */

/* operator-view.js is a large module with dependencies. Only energyFlow is under
 * test here, so it is loaded with the collaborators it actually calls stubbed to
 * the real thing where that is cheap (AutomatrixCards) and to a marker where it
 * is not (icons). */
function flowSandbox() {
    const sandbox = makeSandbox();
    load(sandbox, 'operator-proof.js');   /* AutomatrixCards: measured(), icon() */
    /* The real attribution helper. Stubbing it would test the fallback rather
     * than the module the browser runs. */
    load(sandbox, 'source-attribution.js');
    /* Namespaced, like the real icon builder. A stub that produced a plain
     * element would fail the namespace assertion for a reason that lives in this
     * file rather than in the product -- which is a test reporting its own bug
     * as a defect in the code under test. */
    sandbox.window.AutomatrixIcons = new Proxy({}, {
        get: () => () => new Element('svg', 'http://www.w3.org/2000/svg')
    });
    return sandbox;
}

/* energyFlow lives inside an IIFE, so it is reached the way the page reaches it:
 * by rendering the dashboard. Rather than run the whole module, the function is
 * extracted and evaluated against the same sandbox -- which still executes the
 * REAL source text, so a change to it changes what is tested. */
function loadEnergyFlow(sandbox) {
    /* Line endings normalised first. This repository is edited on Windows and
     * checked out with autocrlf, so a marker written with \n never matches the
     * file's \r\n -- and the failure looks like "the block could not be located",
     * which reads as a missing feature rather than as a newline. */
    const source = fs.readFileSync(path.join(WEB, 'operator-view.js'), 'utf8')
        .replace(/\r\n/g, '\n');
    const start = source.indexOf('    const FLOW_GEOMETRY = {');
    const end = source.indexOf('    /*\n     * THE PLANT OVERVIEW.');
    assert.ok(start > 0 && end > start, 'the energy flow block could not be located');
    const block = source.slice(start, end);
    /* The helpers energyFlow uses from its module scope. */
    const preamble = `
        function node(tag, className, text) {
            const element = document.createElement(tag);
            if (className) element.className = className;
            if (text !== undefined && text !== null) element.textContent = text;
            return element;
        }
        function finite(value) { return typeof value === 'number' && Number.isFinite(value); }
    `;
    vm.runInContext(`${preamble}\n${block}\nwindow.__energyFlow = energyFlow;`,
                    sandbox, { filename: 'operator-view.js#energyFlow' });
    return sandbox.window.__energyFlow;
}

const ON_GRID = { available: true, state: 'grid', configured: true,
    evidence_fresh: true, conflict: false, fail_closed: false,
    attributed_to: 'grid', reason: '' };
const ON_GENERATOR = { ...ON_GRID, state: 'generator', attributed_to: 'generator' };
const SOURCE_UNKNOWN = { available: true, state: 'unknown', configured: true,
    evidence_fresh: false, conflict: false, fail_closed: true,
    attributed_to: 'unknown', reason: 'The source evidence is stale.' };

const PLANT = {
    status: {
        meter_online: true, meter_stale: false,
        grid_power_kw: 216.5, generator_power_kw: null, source: ON_GRID
    },
    inverterTelemetry: { summary: { measured_total_kw: 81.4 } }
};

check('the flow diagram draws four nodes, a junction and both path sets', () => {
    const sandbox = flowSandbox();
    const energyFlow = loadEnergyFlow(sandbox);
    const rendered = energyFlow(PLANT);

    const nodes = rendered.withClass('amx-flow-node');
    assert.strictEqual(nodes.length, 4, `expected solar, grid, load and generator, got ${nodes.length}`);
    ['is-solar', 'is-grid', 'is-load', 'is-generator'].forEach((kind) => {
        assert.ok(nodes.some((n) => n.classList.contains(kind)), `no ${kind} node`);
    });

    assert.strictEqual(rendered.withClass('amx-flow-junction').length, 1,
        'the junction is missing or duplicated');

    /* Both path sets are always emitted; CSS decides which is visible. Building
     * only one and switching it in JavaScript would mean a rotated phone shows
     * connectors drawn for the other layout until the next render. */
    assert.strictEqual(rendered.withClass('amx-flow-lines-desktop').length, 1);
    assert.strictEqual(rendered.withClass('amx-flow-lines-mobile').length, 1);

    /* NAMESPACED SVG, not HTML elements that happen to be named after SVG tags.
     * The browser lays the latter out as unknown inline boxes and draws nothing,
     * which is a blank diagram with no error anywhere. */
    const paths = rendered.findAll((n) => n.tagName === 'PATH');
    assert.ok(paths.length >= 16, `expected base and flow paths for both sets, got ${paths.length}`);
    paths.forEach((path) => {
        assert.strictEqual(path.namespaceURI, 'http://www.w3.org/2000/svg',
            'an SVG path built outside the SVG namespace draws nothing');
    });
    rendered.findAll((n) => n.tagName === 'SVG').forEach((svg) => {
        assert.strictEqual(svg.namespaceURI, 'http://www.w3.org/2000/svg');
    });

    /* THE JUNCTION CARRIES NO NUMBER. The moment it shows one, the reader has to
     * work out whether it is a fifth measurement or the sum of the other four --
     * and on a diagram whose whole job is to be read at a glance, that question
     * costs more than the number is worth. */
    const junction = rendered.withClass('amx-flow-junction')[0];
    assert.strictEqual(junction.textContent, '',
        `the junction shows "${junction.textContent}"; it must carry no value`);
});

/*
 * THE MISTAKE THE MOCKUP WOULD HAVE INTRODUCED.
 *
 * The supplied design computed `grid = load - solar - generator` from an
 * invented load. This product is the other way round: the grid is MEASURED by
 * the instrument the control loop regulates against, and the load is DERIVED.
 *
 * Adopting the mockup's direction would put a grid figure this file calculated
 * on the same screen as a controller acting on a different one -- and the screen
 * would be the more convincing of the two. So the fixture is arranged so the two
 * directions give different answers, and the measured value is asserted.
 */
check('the grid is the measured value and the load is derived from it', () => {
    const sandbox = flowSandbox();
    const energyFlow = loadEnergyFlow(sandbox);
    const rendered = energyFlow(PLANT);

    const nodeFor = (kind) =>
        rendered.withClass('amx-flow-node').find((n) => n.classList.contains(kind));

    const grid = nodeFor('is-grid');
    assert.ok(grid.textContent.includes('216.5'),
        `the grid node shows "${grid.textContent}", not the measured 216.5 kW`);

    /* 216.5 + 81.4 = 297.9. If the page had derived the grid instead, the two
     * figures would swap and this would fail. */
    const load = nodeFor('is-load');
    assert.ok(load.textContent.includes('297.9'),
        `the load node shows "${load.textContent}", not grid + solar = 297.9 kW`);
    assert.ok(load.textContent.includes('derived, not metered'),
        'the derived load must say it is derived');
});

check('a stale meter is not drawn as a live flow', () => {
    const sandbox = flowSandbox();
    const energyFlow = loadEnergyFlow(sandbox);
    const rendered = energyFlow({
        status: { meter_online: true, meter_stale: true, grid_power_kw: 216.5, source: ON_GRID },
        inverterTelemetry: { summary: { measured_total_kw: 81.4 } }
    });
    const grid = rendered.withClass('amx-flow-node')
        .find((n) => n.classList.contains('is-grid'));
    assert.ok(grid.textContent.includes('—'),
        'a retained grid value drawn as a live flow shows a plant importing from '
        + 'a meter that stopped answering');
    assert.ok(grid.classList.contains('is-absent'));
});

check('export reverses the grid dashes and says so in words', () => {
    const sandbox = flowSandbox();
    const energyFlow = loadEnergyFlow(sandbox);
    const rendered = energyFlow({
        status: { meter_online: true, meter_stale: false, grid_power_kw: -18.2, source: ON_GRID },
        inverterTelemetry: { summary: { measured_total_kw: 120.0 } }
    });

    const grid = rendered.withClass('amx-flow-node')
        .find((n) => n.classList.contains('is-grid'));
    /* Magnitude on the face, direction in the word: a minus sign in a large
     * figure is easy to miss and reading it backwards inverts the plant. */
    assert.ok(grid.textContent.includes('18.2'));
    assert.ok(grid.textContent.includes('exporting'),
        'export must be stated in words, not only by the arrow direction');

    const gridPaths = rendered.findAll(
        (n) => n.tagName === 'ANIMATE' && n.attributes.to === '28');
    assert.ok(gridPaths.length >= 2,
        'the grid dashes must run the other way on export, in both path sets');
});

check('a flow that carries nothing is not animated', () => {
    const sandbox = flowSandbox();
    const energyFlow = loadEnergyFlow(sandbox);
    const rendered = energyFlow({
        status: { meter_online: true, meter_stale: false, grid_power_kw: 216.5, source: ON_GRID },
        inverterTelemetry: { summary: { measured_total_kw: 0 } }
    });
    const solarPaths = rendered.findAll(
        (n) => n.classList.contains('amx-flow-path') && n.classList.contains('is-solar'));
    assert.ok(solarPaths.length >= 2);
    solarPaths.forEach((path) => {
        assert.ok(path.classList.contains('is-inactive'),
            'a flow at zero must be still -- a moving line means power is flowing now');
    });
});

/*
 * THE DEFECT THIS EXISTS FOR.
 *
 * Observed on the plant 2026-08-01: source detection resolved GENERATOR from the
 * EM-500 tariff input, and the diagram drew the measured 347.3 kW under GRID
 * with the generator dimmed and captioned "not running", while the site ran on
 * the generator.
 *
 * On a single-meter tariff plant one meter measures whichever source is live.
 * The number is identical; only its name changes.
 */
check('the measurement follows the source the controller resolved', () => {
    const sandbox = flowSandbox();
    const energyFlow = loadEnergyFlow(sandbox);
    const rendered = energyFlow({
        status: { meter_online: true, meter_stale: false,
                  grid_power_kw: 347.3, source: ON_GENERATOR },
        inverterTelemetry: { summary: { measured_total_kw: 0 } }
    });

    const nodeFor = (kind) =>
        rendered.withClass('amx-flow-node').find((n) => n.classList.contains(kind));

    const generator = nodeFor('is-generator');
    assert.ok(generator.textContent.includes('347.3'),
        `the generator node shows "${generator.textContent}" -- the measurement `
        + 'belongs to the source the controller resolved');
    assert.ok(!generator.textContent.includes('not running'),
        'the generator is carrying the plant and the page says it is not running');

    const grid = nodeFor('is-grid');
    assert.ok(!grid.textContent.includes('347.3'),
        'the measurement is drawn under GRID while the plant runs on the generator');
    assert.ok(grid.textContent.includes('—'),
        'the utility is not measured on this topology and must show an em dash');
});

/* And the third answer: the controller cannot say which supply is live. The
 * honest rendering is to say so, not to pick the likelier one. */
check('an unestablished source is stated, not guessed', () => {
    const sandbox = flowSandbox();
    const energyFlow = loadEnergyFlow(sandbox);
    const text = energyFlow({
        status: { meter_online: true, meter_stale: false,
                  grid_power_kw: 120.0, source: SOURCE_UNKNOWN },
        inverterTelemetry: { summary: { measured_total_kw: 0 } }
    }).textContent;

    assert.ok(text.includes('stale'),
        'the reason the source is unknown is not shown');
    assert.ok(!text.includes('importing'),
        'the page claims a direction on the utility while the source is unknown');
    assert.ok(!text.includes('not running'),
        'the page asserts the generator is not running while the source is unknown');
});

/* Negative power is ordinary export on a grid and REVERSE POWER on a generator
 * -- a fault. The same sign must not read the same way on both. */
check('reverse power is not called export', () => {
    const sandbox = flowSandbox();
    const energyFlow = loadEnergyFlow(sandbox);
    const text = energyFlow({
        status: { meter_online: true, meter_stale: false,
                  grid_power_kw: -8.4, source: ON_GENERATOR },
        inverterTelemetry: { summary: { measured_total_kw: 90 } }
    }).textContent;

    assert.ok(text.includes('reverse power'),
        'power flowing back into a generator is reported as export');
    assert.ok(!text.includes('exporting'),
        'the generator vocabulary is not used for a negative generator reading');
});

check('the register word that would be sent is shown, not just the percent', () => {
    const sandbox = makeSandbox();
    load(sandbox, 'inverter-detail.js');
    const text = sandbox.window.AutomatrixInverterDetail.render(INVERTER).textContent;

    assert.ok(text.includes('450'),
        'the register WORD is not shown. A wrong scale is the one error nothing '
        + 'downstream catches: 45 on a x10 register commands 4.5%, and the '
        + 'readback echoes it, decodes it the same wrong way and reports CONFIRMED.');
    assert.ok(text.includes('40125'), 'the register address is not shown');
    /* And it must say it will NOT be sent, or an engineer reads a preview as a
     * commanded state. */
    assert.ok(text.includes('Not written'),
        'a preview that will not be written must say so');
    assert.ok(text.includes('not permitted to write'),
        'the first gate that stops it is not stated');
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
