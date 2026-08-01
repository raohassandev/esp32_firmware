/* meter-detail.js — everything the meter measures, on a screen.
 *
 * The firmware now reads the EM-500's whole instantaneous block and its energy
 * counters. This renders them, and it renders ONLY them: no averages this file
 * computed, no totals it summed itself, no values carried over from a previous
 * poll. A meter page is evidence that the instrument is wired the way the drawing
 * says, and evidence that was massaged in transit is not evidence.
 *
 * SO NULL IS A VALUE HERE, not a gap to fill. The API sends null for a quantity
 * the firmware does not have; this draws an em dash for it and says how old the
 * block is. "0.0 V" would claim the meter measured zero volts, which is a
 * different fault from not having read it -- and sends an electrician to the
 * wrong place.
 */
(function () {
    'use strict';

    function node(tag, className, text) {
        const element = document.createElement(tag);
        if (className) element.className = className;
        if (text !== undefined && text !== null) element.textContent = text;
        return element;
    }

    function finite(value) {
        return typeof value === 'number' && Number.isFinite(value);
    }

    /* Fixed decimals per quantity, chosen from what the instrument can actually
     * resolve rather than from what looks tidy. The EM-500 reports current to
     * 1/10000 A and power factor to 1/10000; printing volts to four places would
     * imply a precision the V/100 register does not have. */
    const DECIMALS = {
        v: 1,
        a: 2,
        kw: 2,
        pf: 3,
        hz: 2,
        pct: 2,
        kwh: 1
    };

    function figure(value, kind) {
        if (!finite(value)) return null;
        return value.toFixed(DECIMALS[kind] ?? 2);
    }

    /* One cell. `signed` marks the quantities where a minus sign means a
     * direction rather than a smaller number -- active power, reactive power and
     * power factor. Those get a tint as well as the sign, because a lone '-' in a
     * column of four-digit figures is genuinely easy to miss and reading it
     * backwards inverts what the plant is doing. */
    function cell(value, kind, options) {
        const settings = options || {};
        const text = figure(value, kind);
        const td = node('td', settings.total ? 'amx-matrix-total' : '');
        if (text === null) {
            td.classList.add('amx-absent');
            td.textContent = '—';
            td.title = 'Not measured';
            return td;
        }
        td.textContent = text;
        if (settings.signed && value < 0) {
            td.classList.add('amx-exporting');
            td.title = 'Negative: exporting';
        }
        return td;
    }

    function matrixRow(label, unit, values, kind, options) {
        const settings = options || {};
        const tr = node('tr');
        const th = node('th');
        th.scope = 'row';
        th.append(document.createTextNode(label));
        if (unit) th.append(node('span', 'amx-matrix-unit', unit));
        tr.append(th);
        const list = Array.isArray(values) ? values : [];
        for (let phase = 0; phase < 3; phase += 1) {
            tr.append(cell(list[phase], kind, { signed: settings.signed }));
        }
        tr.append(cell(settings.total, kind, { signed: settings.signed, total: true }));
        return tr;
    }

    /*
     * THE PHASE MATRIX.
     *
     * Phases across, quantities down. The single question this page is ever
     * asked is "are the three phases the same?", and a matrix answers it by
     * position -- a ragged column is visible before a digit has been read --
     * where a stack of labelled values would make the reader compare three
     * numbers from memory.
     *
     * The fourth column is the whole installation as the meter reports it. It is
     * NOT the sum of the three: the meter computes it, and showing this file's
     * own arithmetic in its place would hide exactly the disagreement that
     * reveals a CT on the wrong phase.
     */
    function phaseMatrix(m) {
        const table = node('table', 'amx-matrix');
        table.append(node('caption', '', 'Per phase, as measured'));

        const head = node('thead');
        const headRow = node('tr');
        ['Quantity', 'L1', 'L2', 'L3', 'Total'].forEach((title, index) => {
            const th = node('th', index === 4 ? 'amx-matrix-total' : '', title);
            th.scope = 'col';
            headRow.append(th);
        });
        head.append(headRow);
        table.append(head);

        const body = node('tbody');
        body.append(
            matrixRow('Voltage', 'V phase-neutral', m.phase_voltage_v, 'v',
                { total: m.equivalent_phase_voltage_v }),
            matrixRow('Line voltage', 'V L1-L2, L2-L3, L3-L1', m.line_voltage_v, 'v',
                { total: m.equivalent_line_voltage_v }),
            matrixRow('Current', 'A', m.current_a, 'a',
                { total: m.equivalent_current_a }),
            matrixRow('Active power', 'kW, negative = exporting', m.active_power_kw, 'kw',
                { total: m.total_active_power_kw, signed: true }),
            matrixRow('Reactive power', 'kvar', m.reactive_power_kvar, 'kw',
                { total: m.total_reactive_power_kvar, signed: true }),
            matrixRow('Apparent power', 'kVA', m.apparent_power_kva, 'kw',
                { total: m.total_apparent_power_kva }),
            matrixRow('Power factor', 'negative = leading', m.power_factor, 'pf',
                { total: m.total_power_factor, signed: true })
        );
        table.append(body);

        const wrap = node('div', 'amx-matrix-wrap');
        wrap.append(table);
        return wrap;
    }

    function counter(label, value, unit, kind) {
        const box = node('div', 'amx-counter');
        box.append(node('span', 'amx-counter-label', label));
        const text = figure(value, kind || 'kwh');
        const figureEl = node('span', 'amx-counter-value');
        if (text === null) {
            figureEl.textContent = '—';
            figureEl.classList.add('amx-absent');
        } else {
            /* Grouped thousands. A factory's lifetime import runs to seven
             * digits and an ungrouped run of them cannot be read aloud, which is
             * what someone does when they check it against a utility bill. */
            const [whole, fraction] = text.split('.');
            const grouped = Number(whole).toLocaleString('en-US');
            figureEl.append(document.createTextNode(fraction ? `${grouped}.${fraction}` : grouped));
            figureEl.append(node('span', 'amx-counter-unit', unit));
        }
        box.append(figureEl);
        return box;
    }

    /*
     * SYSTEM QUANTITIES. Frequency, neutral current and the meter's own asymmetry
     * figures -- the ones that have no per-phase form.
     *
     * Asymmetry earns its place next to the phase matrix rather than being an
     * afterthought: an unbalanced three-phase load is precisely why an export
     * limit enforced on the total can be satisfied while one phase is exporting,
     * and this is the meter's own number for it rather than one derived here.
     */
    function systemRow(m) {
        const grid = node('div', 'amx-counters');
        grid.append(
            counter('Frequency', m.frequency_hz, 'Hz', 'hz'),
            counter('Neutral current', m.neutral_current_a, 'A', 'a'),
            counter('Voltage asymmetry L-L', m.voltage_asymmetry_line_percent, '%', 'pct'),
            counter('Voltage asymmetry L-N', m.voltage_asymmetry_phase_percent, '%', 'pct'),
            counter('Current asymmetry', m.current_asymmetry_percent, '%', 'pct')
        );
        return grid;
    }

    function energyBlock(e) {
        const grid = node('div', 'amx-counters');
        grid.append(
            counter('Imported', e.total_import_active_kwh, 'kWh'),
            counter('Exported', e.total_export_active_kwh, 'kWh'),
            counter('Reactive in', e.total_import_reactive_kvarh, 'kvarh'),
            counter('Reactive out', e.total_export_reactive_kvarh, 'kvarh'),
            counter('Apparent', e.total_apparent_kvah, 'kVAh')
        );
        return grid;
    }

    /* The meter's own resettable counters, kept apart from the lifetime ones.
     * Two numbers labelled "imported" that differ by a factor of a thousand,
     * side by side with nothing to distinguish them, is how a reader concludes
     * the page is broken. */
    function partialBlock(e) {
        const grid = node('div', 'amx-counters');
        grid.append(
            counter('Imported', e.partial_import_active_kwh, 'kWh'),
            counter('Exported', e.partial_export_active_kwh, 'kWh'),
            counter('Reactive in', e.partial_import_reactive_kvarh, 'kvarh'),
            counter('Reactive out', e.partial_export_reactive_kvarh, 'kvarh'),
            counter('Apparent', e.partial_apparent_kvah, 'kVAh')
        );
        return grid;
    }

    function ageWords(ms) {
        if (!finite(ms)) return 'age unknown';
        if (ms < 1500) return 'just now';
        if (ms < 60000) return `${Math.round(ms / 1000)} s ago`;
        if (ms < 3600000) return `${Math.round(ms / 60000)} min ago`;
        return `${Math.round(ms / 3600000)} h ago`;
    }

    function section(title, ageMs, body) {
        const wrap = node('section', 'amx-section');
        const head = node('div', 'amx-section-head');
        head.append(node('h4', 'amx-section-title', title));
        if (ageMs !== undefined) head.append(node('span', 'amx-measure-age', `read ${ageWords(ageMs)}`));
        wrap.append(head, body);
        return wrap;
    }

    /*
     * Renders the measurement detail for one meter, or null when this meter has
     * none.
     *
     * NULL, not an empty panel. A family whose manual this firmware has not
     * transcribed has no measurement block at all, and drawing an empty matrix
     * for it would say the instrument answered with nothing -- when in truth it
     * was never asked. The caller shows the card without this section.
     */
    function render(meter) {
        const measurements = meter && meter.measurements;
        const energy = meter && meter.energy;
        const hasMeasurements = Boolean(measurements && measurements.available);
        const hasEnergy = Boolean(energy && energy.available);
        if (!hasMeasurements && !hasEnergy) return null;

        const container = node('div', 'amx-measure');
        if (hasMeasurements) {
            container.append(section('Measured', measurements.age_ms, phaseMatrix(measurements)));
            container.append(section('System', measurements.age_ms, systemRow(measurements)));
        }
        if (hasEnergy) {
            container.append(section('Energy, since installation', energy.age_ms, energyBlock(energy)));
            container.append(section('Energy, since last reset', energy.age_ms, partialBlock(energy)));
        }
        return container;
    }

    window.AutomatrixMeterDetail = { render };
}());
