/* inverter-detail.js — what the machine reports, kept apart from what we told it.
 *
 * THE ONE DISTINCTION THIS PAGE EXISTS TO PROTECT.
 *
 *   COMMANDED is this firmware's own belief. A percentage it calculated and
 *   wrote. It is true that we sent it. It says nothing whatsoever about whether
 *   the inverter acted on it.
 *
 *   MEASURED is what the machine reported back. It is the only evidence on the
 *   page.
 *
 * Rendered in the same style, side by side, with the same weight, those two
 * become one impression: "the plant is at 45%". That impression is how a screen
 * convinces a room full of people that a site is curtailed when nothing was
 * curtailed -- and the controller has no way to know it is doing so, because the
 * write succeeded and the number is real. So they are drawn differently here,
 * deliberately, and the commanded figure is labelled as an instruction rather
 * than as a reading.
 *
 * EVERYTHING ELSE IS THE MACHINE'S. DC strings, AC per phase, yield,
 * temperature, insulation, device status. All read-only, all transcribed from
 * the manufacturer's manual, none of it inferred and none of it recomputed here.
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

    /* Decimals per quantity, from what the register can resolve rather than what
     * looks tidy. Huawei reports string voltage at gain 10 and current at gain
     * 100; printing volts to two places would imply a precision the register
     * does not carry. */
    const DECIMALS = { v: 1, a: 2, kw: 2, pf: 3, hz: 2, pct: 1, c: 1, mohm: 1, kwh: 1 };

    function figure(value, kind) {
        if (!finite(value)) return null;
        return value.toFixed(DECIMALS[kind] ?? 2);
    }

    function cell(value, kind, signed) {
        const text = figure(value, kind);
        const td = node('td');
        if (text === null) {
            td.classList.add('amx-absent');
            td.textContent = '—';
            td.title = 'Not measured';
            return td;
        }
        td.textContent = text;
        if (signed && value < 0) {
            td.classList.add('amx-exporting');
            td.title = 'Negative';
        }
        return td;
    }

    function matrix(caption, columns, rows) {
        const table = node('table', 'amx-matrix');
        table.append(node('caption', '', caption));

        const head = node('thead');
        const headRow = node('tr');
        ['Quantity'].concat(columns).forEach((title) => {
            const th = node('th', '', title);
            th.scope = 'col';
            headRow.append(th);
        });
        head.append(headRow);
        table.append(head);

        const body = node('tbody');
        rows.forEach((row) => {
            const tr = node('tr');
            const th = node('th');
            th.scope = 'row';
            th.append(document.createTextNode(row.label));
            if (row.unit) th.append(node('span', 'amx-matrix-unit', row.unit));
            tr.append(th);
            const values = Array.isArray(row.values) ? row.values : [];
            for (let index = 0; index < columns.length; index += 1) {
                tr.append(cell(values[index], row.kind, row.signed));
            }
            body.append(tr);
        });
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
            const [whole, fraction] = text.split('.');
            const grouped = Number(whole).toLocaleString('en-US');
            figureEl.append(document.createTextNode(fraction ? `${grouped}.${fraction}` : grouped));
            figureEl.append(node('span', 'amx-counter-unit', unit));
        }
        box.append(figureEl);
        return box;
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
     * COMMANDED VERSUS MEASURED, drawn as two different kinds of thing.
     *
     * The instruction is set in the muted, smaller "instruction" treatment and
     * says WE SENT. The measurement is the prominent figure and says the machine
     * reported. When the two disagree that is stated in words, because a reader
     * comparing two numbers in different boxes will not do the subtraction, and
     * the disagreement is the single most important fact an inverter page can
     * carry: it means the command is not in force.
     */
    function commandVersusMeasured(inverter) {
        const runtime = inverter.runtime || {};
        const rated = finite(inverter.rated_kw) ? inverter.rated_kw : null;
        const measuredKw = finite(inverter.measured_power_kw) ? inverter.measured_power_kw : null;
        const commandedPercent = finite(runtime.commanded_percent) ? runtime.commanded_percent : null;
        const commandedKw = finite(runtime.commanded_power_kw) ? runtime.commanded_power_kw : null;

        const grid = node('div', 'amx-counters');

        const measuredBox = node('div', 'amx-counter');
        measuredBox.append(node('span', 'amx-counter-label', 'Measured output'));
        const measuredValue = node('span', 'amx-counter-value');
        if (measuredKw === null) {
            measuredValue.textContent = '—';
            measuredValue.classList.add('amx-absent');
        } else {
            measuredValue.append(document.createTextNode(measuredKw.toFixed(2)));
            measuredValue.append(node('span', 'amx-counter-unit', 'kW'));
        }
        measuredBox.append(measuredValue);
        measuredBox.append(node('span', 'amx-counter-note',
            measuredKw === null
                ? 'the machine has not reported'
                : `reported by the inverter · ${ageWords(inverter.measured_age_ms)}`));
        grid.append(measuredBox);

        const commandBox = node('div', 'amx-counter is-instruction');
        commandBox.append(node('span', 'amx-counter-label', 'Limit we sent'));
        const commandValue = node('span', 'amx-counter-value');
        if (commandedPercent === null) {
            commandValue.textContent = 'never commanded';
            commandValue.classList.add('amx-absent');
        } else {
            commandValue.append(document.createTextNode(commandedPercent.toFixed(0)));
            commandValue.append(node('span', 'amx-counter-unit', '%'));
        }
        commandBox.append(commandValue);
        /* Said in words, every time. This is an instruction this controller
         * issued, not a property of the plant, and a reader who forgets that
         * will read the whole page as measurement. */
        commandBox.append(node('span', 'amx-counter-note',
            commandedKw === null
                ? 'an instruction, not a reading'
                : `an instruction, not a reading · ${commandedKw.toFixed(2)} kW of ${rated === null ? '—' : rated.toFixed(0)} kW rated`));
        grid.append(commandBox);

        /*
         * WHAT THE CONTROLLER WOULD SEND, whether or not it is allowed to.
         *
         * A profile stays at LAB_ONLY until a readback on real hardware confirms
         * the register, the scale and the settle time -- and there was no way to
         * see what would be sent, so nothing could be checked against the manual
         * before enabling anything.
         *
         * The register WORD is shown, not just the percentage, because the scale
         * is the error nothing downstream can catch: 45% on a x10 register is
         * the word 450, and sending 45 commands 4.5% while the readback echoes
         * 45, decodes with the same wrong scale, agrees with the request and
         * reports CONFIRMED. Only the word shows that, and only before it goes.
         */
        const preview = inverter.command_preview;
        if (preview && preview.available) {
            /* Its own treatment. Not a measurement and not a commanded state
             * either: a COMPUTATION of what would go on the wire. Three
             * different kinds of claim must not share one style. */
            const box = node('div', 'amx-counter is-preview');
            box.append(node('span', 'amx-counter-label', 'Would send'));
            const value = node('span', 'amx-counter-value');
            value.append(document.createTextNode(preview.percent.toFixed(0)));
            value.append(node('span', 'amx-counter-unit', '%'));
            box.append(value);
            const words = Array.isArray(preview.words) ? preview.words : [];
            /* The register and the word are engineering detail and are absent
             * from the operator view by design -- that is how the firmware
             * talks to the machine. The percentage is the decision, and it is
             * shown to both. */
            if (Number.isFinite(preview.register) && words.length) {
                box.append(node('span', 'amx-counter-note',
                    `Register ${preview.register} (FC${preview.function}) = ${words.join(', ')}`
                    + ` · ${preview.percent} % x ${preview.raw_units_per_percent}`));
            } else if (Number.isFinite(preview.share_kw)) {
                box.append(node('span', 'amx-counter-note',
                    `${preview.share_kw.toFixed(2)} kW of this machine's share`));
            }
            /* Whether it would actually go, and the first gate that stops it --
             * which is the one an engineer must clear first. */
            box.append(node('span', 'amx-counter-note',
                preview.would_write
                    ? 'This would be written on the next control cycle.'
                    : `Not written: ${preview.blocked_by || 'the write gate refuses it'}.`));
            grid.append(box);
        } else if (preview && preview.blocked_by) {
            const box = node('div', 'amx-counter is-preview');
            box.append(node('span', 'amx-counter-label', 'Would send'));
            box.append(node('span', 'amx-counter-value amx-absent', 'nothing'));
            box.append(node('span', 'amx-counter-note', preview.blocked_by));
            grid.append(box);
        }

        /* The disagreement, stated. Only when there is enough to compare: a
         * machine producing less than its limit is equally consistent with the
         * limit being honoured and with the sun going in, so this reports the
         * one direction that is unambiguous -- output ABOVE the limit means the
         * limit is not in force. */
        if (measuredKw !== null && commandedKw !== null && rated !== null) {
            const overshoot = measuredKw - commandedKw;
            const band = Math.max(0.5, rated * 0.02);
            if (overshoot > band) {
                const warn = node('div', 'amx-counter is-attention');
                warn.append(node('span', 'amx-counter-label', 'Above the limit'));
                const value = node('span', 'amx-counter-value');
                value.append(document.createTextNode(`+${overshoot.toFixed(2)}`));
                value.append(node('span', 'amx-counter-unit', 'kW'));
                warn.append(value);
                warn.append(node('span', 'amx-counter-note',
                    'the machine is producing more than the limit we sent'));
                grid.append(warn);
            }
        }
        return grid;
    }

    /*
     * Renders the measurement detail for one inverter, or null when this
     * inverter has none.
     *
     * Null, not an empty panel: a family whose manual this firmware has not
     * transcribed was never asked for these registers, and an empty matrix would
     * say the machine answered with nothing.
     */
    function render(inverter) {
        const measurements = inverter && inverter.measurements;
        if (!measurements || !measurements.available) return null;

        const dc = measurements.dc || {};
        const ac = measurements.ac || {};
        const device = measurements.device || {};
        const energy = measurements.energy || {};
        const age = measurements.age_ms;

        const container = node('div', 'amx-measure');

        container.append(section('Output', age, commandVersusMeasured(inverter)));

        const strings = Array.isArray(dc.string_voltage_v) ? dc.string_voltage_v.length : 0;
        if (strings) {
            const labels = [];
            for (let index = 0; index < strings; index += 1) labels.push(`PV${index + 1}`);
            container.append(section('DC strings', age, matrix('Per string, as measured', labels, [
                { label: 'Voltage', unit: 'V', values: dc.string_voltage_v, kind: 'v', signed: true },
                { label: 'Current', unit: 'A', values: dc.string_current_a, kind: 'a', signed: true }
            ])));
        }

        container.append(section('AC side', age, matrix('Per phase, as measured', ['L1', 'L2', 'L3'], [
            { label: 'Phase voltage', unit: 'V phase-neutral', values: ac.phase_voltage_v, kind: 'v' },
            { label: 'Line voltage', unit: 'V L1-L2, L2-L3, L3-L1', values: ac.line_voltage_v, kind: 'v' },
            { label: 'Current', unit: 'A', values: ac.phase_current_a, kind: 'a', signed: true }
        ])));

        const system = node('div', 'amx-counters');
        system.append(
            counter('DC power', dc.power_kw, 'kW', 'kw'),
            counter('AC active power', ac.active_power_kw, 'kW', 'kw'),
            counter('Reactive power', ac.reactive_power_kvar, 'kvar', 'kw'),
            counter('Power factor', ac.power_factor, '', 'pf'),
            counter('Frequency', ac.frequency_hz, 'Hz', 'hz'),
            counter('Peak today', ac.peak_active_power_today_kw, 'kW', 'kw'),
            counter('Efficiency', device.efficiency_percent, '%', 'pct'),
            counter('Internal temperature', device.internal_temperature_c, '°C', 'c'),
            counter('Insulation resistance', device.insulation_resistance_mohm, 'MΩ', 'mohm')
        );
        container.append(section('Machine', age, system));

        const yields = node('div', 'amx-counters');
        yields.append(
            counter('Today', energy.today_kwh, 'kWh'),
            counter('This month', energy.month_kwh, 'kWh'),
            counter('Lifetime', energy.total_kwh, 'kWh'),
            counter('Lifetime DC input', energy.total_dc_input_kwh, 'kWh')
        );
        container.append(section('Yield', age, yields));

        /*
         * STATUS AND FAULT, AS CODES.
         *
         * The manufacturer defers both code tables to a document this project
         * does not hold. A word invented for a code would be a guess wearing the
         * clothes of a diagnosis, and a wrong diagnosis is worse than none
         * because it ends the search. The number is what a person quotes to the
         * support line, so it is shown as a number and said to be one.
         */
        if (finite(device.status_raw) || finite(device.fault_code_raw)) {
            const codes = node('div', 'amx-counters');
            const status = node('div', 'amx-counter');
            status.append(node('span', 'amx-counter-label', 'Device status code'));
            status.append(node('span', 'amx-counter-value',
                finite(device.status_raw) ? String(device.status_raw) : '—'));
            status.append(node('span', 'amx-counter-note',
                'the manufacturer code table is not held by this controller'));
            codes.append(status);

            const fault = node('div', finite(device.fault_code_raw) && device.fault_code_raw !== 0
                ? 'amx-counter is-attention'
                : 'amx-counter');
            fault.append(node('span', 'amx-counter-label', 'Fault code'));
            fault.append(node('span', 'amx-counter-value',
                finite(device.fault_code_raw)
                    ? (device.fault_code_raw === 0 ? 'none' : String(device.fault_code_raw))
                    : '—'));
            if (finite(device.fault_code_raw) && device.fault_code_raw !== 0) {
                fault.append(node('span', 'amx-counter-note',
                    'quote this code to the manufacturer'));
            }
            codes.append(fault);
            container.append(section('Reported by the machine', age, codes));
        }

        return container;
    }

    window.AutomatrixInverterDetail = { render };
}());
