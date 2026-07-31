/* source-detection.js - EM500 source detection: presentation + engineering page.
 *
 * Two blocks live in this file.
 *
 *   1. PvdgSourceDetectionUtils - pure functions with no DOM and no fetch, so
 *      they can be required from Node and property-tested
 *      (web/tests/source-detection-utils.test.js). They turn the firmware's
 *      /api/source-detection status object into the four things an operator
 *      has to be able to read without interpreting anything: which supply is
 *      carrying the plant, how good that answer is, what the controller is
 *      doing about it, and the controller's own sentence explaining why.
 *
 *   2. The engineering page itself, registered as an EM500 tab.
 *
 * Rules this file is built on:
 *
 *   VERBATIM. status.reason and status.mode_a_limitation are the firmware's own
 *   sentences about a safety decision and are rendered exactly as received.
 *
 *   AMBIGUITY IS A FAULT. A confident source identity - GRID or GENERATOR - is
 *   rendered only when the controller has actually resolved one: configured,
 *   fresh evidence, no conflict, not debouncing and not fail-closed. In every
 *   other case the screen says UNKNOWN and says why. Showing a plausible source
 *   while the controller is fail-closed is the bug this file exists to prevent.
 *
 *   ONE STATE VOCABULARY. Quality/control wording comes from
 *   AutomatrixUi.STATES in web/app.js, resolved the same way operator-view.js
 *   resolves it. GRID and GENERATOR are source identities, not state words.
 */
((root, factory) => {
    const api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    if (root) root.PvdgSourceDetectionUtils = api;
})(typeof window !== 'undefined' ? window : globalThis, () => {
    'use strict';

    /* The three strings source_detection_state_name() can publish. Anything
     * else is treated as unknown rather than shown to an operator. */
    const FIRMWARE_STATES = Object.freeze(['grid', 'generator', 'unknown']);

    /* Exactly one operator label per firmware state. */
    const SOURCE_LABELS = Object.freeze({
        grid: 'GRID',
        generator: 'GENERATOR',
        unknown: 'UNKNOWN'
    });

    const UNKNOWN_LABEL = SOURCE_LABELS.unknown;

    /* Same resolution order as operator-view.js: the shared vocabulary when the
     * bundle is loaded, the identical literal when this module is required on
     * its own. The literals are not a second vocabulary; the test asserts every
     * one of them is a value in AutomatrixUi.STATES. */
    function stateWord(family, key, fallback) {
        const states = (typeof window !== 'undefined' && window.AutomatrixUi &&
            window.AutomatrixUi.STATES) || null;
        return (states && states[family] && states[family][key]) || fallback;
    }

    function text(value) {
        return typeof value === 'string' ? value.trim() : '';
    }

    function statusOf(payload) {
        if (!payload || typeof payload !== 'object') return null;
        if (payload.status && typeof payload.status === 'object') return payload.status;
        /* Also accept the status object on its own so callers that already
         * unwrapped the response are not forced to re-wrap it. */
        if (typeof payload.state === 'string' || typeof payload.fail_closed === 'boolean') {
            return payload;
        }
        return null;
    }

    /* Deliberately exact. source_detection_state_name() emits "grid",
     * "generator" or "unknown" and nothing else, so a string that merely looks
     * like one of them - different case, padded, translated - is evidence that
     * something other than this firmware produced it. Such a string is treated
     * as unknown rather than accepted as a source identity. */
    function normalizeState(value) {
        return FIRMWARE_STATES.indexOf(value) >= 0 ? value : 'unknown';
    }

    function sourceLabelFor(state) {
        return SOURCE_LABELS[normalizeState(state)] || UNKNOWN_LABEL;
    }

    /* The control consequence of the resolved source.
     *
     * WHAT THIS MAY AND MAY NOT CLAIM. Source detection decides WHICH SUPPLY is
     * carrying the plant. It does not by itself decide whether a curtailment
     * command is issued: that additionally requires the commissioning gate to be
     * satisfied and automatic control to be enabled, both of which are reported
     * by /api/status and shown on the automatic-control panel, not here.
     *
     * So neither direction may be asserted from this endpoint alone. An earlier
     * revision keyed the sentence off detection_only and told the operator "PV is
     * NOT being curtailed to protect the generator" -- a flat statement about the
     * control loop, made from a flag that gates nothing in the firmware
     * (components/source_detection/source_detection.c sets detection_only
     * unconditionally, and no component reads it; the control loop consumes
     * detection.state at components/control_engine/control_engine.c:487). The
     * opposite branch was the same overclaim in the dangerous direction: telling
     * an operator the genset IS protected when the gate may be closed.
     *
     * What is said instead is the part this endpoint actually knows -- whether a
     * generator is carrying the plant, and therefore whether curtailment is
     * REQUIRED -- and it points at the panel that states whether it is happening.
     * detection_only is still surfaced verbatim as a qualifier so the phase is
     * visible without being turned into a claim about the inverters. */
    function consequenceFor(confident, state, detectionOnly) {
        if (!confident) {
            return {
                label: 'No source-based curtailment decision',
                detail: 'The controller has not resolved which supply is carrying the plant, so it is not acting on one. Source-based behaviour stays fail-closed until the evidence resolves.'
            };
        }
        if (state === 'grid') {
            return {
                label: 'Source-based curtailment not required',
                detail: 'The plant is on grid, so source detection is not calling for PV to be restricted.'
            };
        }
        const detail = 'The plant is on generator, so PV must be curtailed to keep load on the machine and stay clear of reverse power. Whether the command is being issued depends on the commissioning gate and the automatic-control enable, which are stated on the automatic-control panel.';
        return {
            label: 'Source-based curtailment required · generator',
            detail: detectionOnly
                ? detail + ' Source detection reports only in this phase and issues no inverter commands itself.'
                : detail
        };
    }

    function absentView() {
        const consequence = consequenceFor(false, 'unknown', false);
        return Object.freeze({
            present: false,
            confident: false,
            state: 'unknown',
            sourceLabel: UNKNOWN_LABEL,
            sourceTone: 'bad',
            qualityKey: 'dataQuality.unavailable',
            qualityLabel: stateWord('dataQuality', 'unavailable', 'Unavailable'),
            detail: 'The controller has not reported a source decision. This screen is not showing grid or generator.',
            controlConsequence: consequence.label,
            controlConsequenceDetail: consequence.detail,
            reasonText: '',
            limitationText: '',
            candidateLabel: '',
            candidateNote: '',
            failClosed: false,
            transitionPending: false,
            conflict: false,
            evidenceFresh: false,
            configured: false,
            detectionOnly: false,
            tariffLabel: 'None',
            readCounts: 'Unavailable'
        });
    }

    function tariffLabel(tariff) {
        const value = Number(tariff);
        if (value === 1) return 'Tariff 1';
        if (value === 2) return 'Tariff 2';
        return 'None';
    }

    function readCounts(status) {
        const good = Number(status.successful_reads);
        const bad = Number(status.failed_reads);
        if (!Number.isFinite(good) && !Number.isFinite(bad)) return 'Unavailable';
        return `${Number.isFinite(good) ? good : 0} successful · ${Number.isFinite(bad) ? bad : 0} failed`;
    }

    /* One object, everything the screens render. Every boolean is compared
     * against true: an absent flag is never read as a reassuring value. */
    function describeSourceDetection(payload) {
        const status = statusOf(payload);
        if (!status) return absentView();

        const state = normalizeState(status.state);
        const configured = status.configured === true;
        const failClosed = status.fail_closed === true;
        const transitionPending = status.transition_pending === true;
        const conflict = status.conflict === true;
        const evidenceFresh = status.evidence_fresh === true;
        const detectionOnly = status.detection_only === true;

        const confident = configured && evidenceFresh && !failClosed &&
            !transitionPending && !conflict && state !== 'unknown';

        const quality = !configured ? ['commissioning', 'notConfigured', 'Not configured']
            : failClosed ? ['control', 'inhibited', 'Inhibited']
            : conflict ? ['dataQuality', 'invalid', 'Invalid']
            : !evidenceFresh ? ['dataQuality', 'stale', 'Stale']
            : transitionPending ? ['workflow', 'inProgress', 'In progress']
            : confident ? ['dataQuality', 'good', 'Good']
            : ['dataQuality', 'unavailable', 'Unavailable'];

        const detail = !configured
            ? 'Source detection is not commissioned on this controller, so no source is being reported.'
            : failClosed
                ? 'The controller is fail-closed on source and will not state which supply is carrying the plant.'
                : conflict
                    ? 'The evidence disagrees with itself, so the controller is not resolving a source.'
                    : !evidenceFresh
                        ? 'The source evidence is not currently usable, so no source is being reported.'
                        : transitionPending
                            ? 'A source change is debouncing. The controller has not committed to the new source.'
                            : confident
                                ? `The controller reports the plant is being carried by ${SOURCE_LABELS[state]}.`
                                : 'The controller has not resolved a source.';

        const candidateLabel = sourceLabelFor(status.candidate_state);
        const consequence = consequenceFor(confident, state, detectionOnly);

        return Object.freeze({
            present: true,
            confident,
            state,
            /* Never a confident identity unless `confident` is true. */
            sourceLabel: confident ? SOURCE_LABELS[state] : UNKNOWN_LABEL,
            sourceTone: confident ? (state === 'grid' ? 'good' : 'warning') : 'bad',
            qualityKey: `${quality[0]}.${quality[1]}`,
            qualityLabel: stateWord(quality[0], quality[1], quality[2]),
            detail,
            controlConsequence: consequence.label,
            controlConsequenceDetail: consequence.detail,
            /* The firmware's sentences, untouched. */
            reasonText: text(status.reason),
            limitationText: text(status.mode_a_limitation),
            candidateLabel,
            candidateNote: transitionPending
                ? `Debouncing towards ${candidateLabel}. This is not the active source yet.`
                : '',
            failClosed,
            transitionPending,
            conflict,
            evidenceFresh,
            configured,
            detectionOnly,
            tariffLabel: tariffLabel(status.tariff),
            readCounts: readCounts(status)
        });
    }

    return Object.freeze({
        FIRMWARE_STATES,
        SOURCE_LABELS,
        UNKNOWN_LABEL,
        describeSourceDetection,
        sourceLabelFor,
        tariffLabel
    });
});

(() => {
    'use strict';

    const app = typeof window !== 'undefined' ? window.PvdgEm500App : null;
    if (!app) return;

    const sourceUtils = window.PvdgSourceDetectionUtils;

    const { api, byId, node, button, option, field, panel,
        summaryCard, setMessage, setBusy, setContent, registerTab } = app;

    const MODE_DISABLED = 0;
    const MODE_SINGLE = 1;
    const MODE_DUAL = 2;
    const UNCONFIGURED = 255;

    function numericInput(id, label, value, minimum, maximum, step = '1') {
        const input = node('input');
        input.id = id;
        input.type = 'number';
        input.min = String(minimum);
        input.max = String(maximum);
        input.step = String(step);
        input.value = value == null || value === 0 ? '' : String(value);
        return field(label, input);
    }

    function meterSelect(id, label, meters, selected) {
        const select = node('select');
        select.id = id;
        select.append(option(UNCONFIGURED, 'Not configured'));
        (meters || []).forEach((meter) => {
            const suffix = meter.enabled ? '' : ' · disabled';
            select.append(option(meter.index, `${Number(meter.index) + 1}. ${meter.name || 'Meter'}${suffix}`));
        });
        select.value = String(selected ?? UNCONFIGURED);
        return field(label, select);
    }

    function formatPower(value) {
        return Number.isFinite(Number(value)) ? `${Number(value).toFixed(2)} kW` : 'Unavailable';
    }

    function modeLabel(mode) {
        if (Number(mode) === MODE_SINGLE) return 'Mode A · single input';
        if (Number(mode) === MODE_DUAL) return 'Mode B · dual meter';
        return 'Disabled';
    }

    function toneTextClass(tone) {
        return tone === 'good' ? 'good-text' : tone === 'warning' ? 'warning-text' : 'bad-text';
    }

    /* The headline. One word, the size of a metric value, that an operator can
     * read from across a switch room - and it says UNKNOWN whenever the
     * controller has not resolved a source, rather than showing the last source
     * it happened to see. */
    function sourceHeadline(view) {
        const section = panel('Active power source', 'Which supply is carrying the plant', false);

        const headline = node('div', `metric-value ${toneTextClass(view.sourceTone)}`, view.sourceLabel);
        headline.setAttribute('role', 'status');
        section.append(headline, node('div', 'metric-foot', view.detail));

        if (view.candidateNote) {
            section.append(node('div', 'notice warning', view.candidateNote));
        }

        const rows = node('div', 'health-list');
        [
            ['Evidence', view.qualityLabel],
            ['PV curtailment', view.controlConsequence],
            ['Energy classification', view.tariffLabel],
            ['Source reads', view.readCounts]
        ].forEach(([label, value]) => {
            const row = node('div', 'health-row');
            row.append(node('span', '', label), node('strong', '', value));
            rows.append(row);
        });
        section.append(rows);
        section.append(node('div', view.confident && view.state === 'grid' ? 'notice safe' : 'notice warning',
            view.controlConsequenceDetail));
        return section;
    }

    function statusPanels(data) {
        const status = data.status || {};
        const config = data.config || {};
        const view = sourceUtils.describeSourceDetection(data);

        const summary = node('div', 'em500-summary');
        summary.append(
            summaryCard('Resolved source', view.sourceLabel,
                view.transitionPending ? `Candidate: ${view.candidateLabel}` : modeLabel(config.mode),
                view.sourceTone),
            summaryCard('Energy classification', view.tariffLabel,
                'Grid uses Tariff 1 · generator uses Tariff 2'),
            summaryCard('Evidence', view.qualityLabel,
                view.failClosed ? 'Fail-closed' : 'Resolved for reporting',
                view.evidenceFresh && !view.failClosed ? 'good' : 'bad'),
            summaryCard('Phase scope', 'Detection only',
                'Automatic control is not enabled by this phase', 'warning')
        );

        const reason = panel('Source decision', 'Firmware reason · shown verbatim');
        reason.append(node('div', status.fail_closed ? 'notice warning' : 'notice safe',
            status.reason || 'Source status unavailable.'));
        if (status.mode_a_limitation) {
            reason.append(node('div', 'notice warning', status.mode_a_limitation));
        }

        const evidence = panel('Current evidence', 'Cached/background acquisition only');
        const evidenceSummary = node('div', 'em500-summary');
        if (Number(config.mode) === MODE_SINGLE) {
            const item = status.single_input_evidence || {};
            evidenceSummary.append(
                summaryCard('Raw source input', item.has_sample ? item.raw_value : 'Unavailable',
                    item.has_sample ? `Age ${item.age_ms} ms` : 'No successful sample'),
                summaryCard('Configured values',
                    `${config.single_input?.grid_value ?? '--'} / ${config.single_input?.generator_value ?? '--'}`,
                    'Grid / generator')
            );
        } else if (Number(config.mode) === MODE_DUAL) {
            const item = status.dual_meter_evidence || {};
            evidenceSummary.append(
                summaryCard('Grid meter', formatPower(item.grid_power_kw),
                    item.grid_has_sample ? `Age ${item.grid_age_ms} ms` : 'No usable sample'),
                summaryCard('Generator meter', formatPower(item.generator_power_kw),
                    item.generator_has_sample ? `Age ${item.generator_age_ms} ms` : 'No usable sample')
            );
        } else {
            evidenceSummary.append(summaryCard('Evidence', 'Disabled', 'Commission a topology before detection starts'));
        }
        evidence.append(evidenceSummary);
        return [sourceHeadline(view), summary, reason, evidence];
    }

    function buildConfiguration(data) {
        const config = data.config || {};
        const single = config.single_input || {};
        const dual = config.dual_meter || {};
        const meters = data.meters || [];

        const section = panel('EM500 source-detection configuration',
            'Persisted separately · commissioned Wi-Fi configuration is not modified');
        const common = node('div', 'field-grid em500-plan-grid');

        const mode = node('select');
        mode.id = 'sourceDetectionMode';
        mode.append(
            option(MODE_DISABLED, 'Disabled · fail closed'),
            option(MODE_SINGLE, 'Mode A · one EM500 with clone-specific digital input'),
            option(MODE_DUAL, 'Mode B · separate grid and generator EM500 meters')
        );
        mode.value = String(config.mode ?? MODE_DISABLED);
        common.append(
            field('Topology', mode),
            numericInput('sourceDebounceMs', 'Debounce (ms)', config.debounce_ms, 1, 4294967295),
            numericInput('sourceStaleMs', 'Stale timeout (ms)', config.stale_timeout_ms, 1, 4294967295)
        );

        const singleFields = node('div', 'field-grid em500-plan-grid');
        singleFields.id = 'sourceSingleFields';
        const functionSelect = node('select');
        functionSelect.id = 'sourceSingleFunction';
        functionSelect.append(option(0, 'Not configured'), option(3, 'FC03'), option(4, 'FC04'));
        functionSelect.value = String(single.function ?? 0);
        const baseSelect = node('select');
        baseSelect.id = 'sourceSingleBase';
        baseSelect.append(
            option(UNCONFIGURED, 'Not configured'),
            option(0, 'PDU address · zero based'),
            option(1, 'Display/table address · subtract one')
        );
        baseSelect.value = String(single.address_base ?? UNCONFIGURED);
        singleFields.append(
            meterSelect('sourceSingleMeter', 'EM500 meter', meters, single.meter_index),
            field('Read function · verify on site', functionSelect),
            field('Address convention · verify on site', baseSelect),
            numericInput('sourceSingleRegister', 'Source register', single.register ?? 8544, 0, 65535),
            numericInput('sourceGridValue', 'Grid value', single.grid_value ?? 0, 0, 65535),
            numericInput('sourceGeneratorValue', 'Generator value', single.generator_value ?? 1, 0, 65535)
        );

        const dualFields = node('div', 'field-grid em500-plan-grid');
        dualFields.id = 'sourceDualFields';
        dualFields.append(
            meterSelect('sourceGridMeter', 'Grid EM500', meters, dual.grid_meter_index),
            meterSelect('sourceGeneratorMeter', 'Generator EM500', meters, dual.generator_meter_index),
            numericInput('sourceGridThreshold', 'Grid threshold (kW)', dual.grid_threshold_kw ?? 1, 0.000001, 1000000, '0.1'),
            numericInput('sourceGeneratorThreshold', 'Generator threshold (kW)', dual.generator_threshold_kw ?? 1, 0.000001, 1000000, '0.1')
        );

        const safety = node('div', 'notice warning',
            'Unknown, conflicting, stale, non-finite, or transitioning evidence remains fail-closed. Saving this page does not enable automatic control.');
        const preferred = node('div', 'notice safe',
            'Mode B is preferred because separate grid and generator measurements corroborate the source decision.');
        const save = button('Save source-detection configuration', 'button primary');
        save.id = 'sourceDetectionSave';
        save.addEventListener('click', saveConfiguration);
        const actions = node('div', 'panel-actions');
        actions.append(save);

        function updateVisibility() {
            const selected = Number(mode.value);
            singleFields.hidden = selected !== MODE_SINGLE;
            dualFields.hidden = selected !== MODE_DUAL;
        }
        mode.addEventListener('change', updateVisibility);
        updateVisibility();

        section.append(common, singleFields, dualFields, preferred, safety, actions);
        return section;
    }

    function numberValue(id) {
        const value = Number(byId(id)?.value);
        return Number.isFinite(value) ? value : 0;
    }

    async function saveConfiguration() {
        const payload = {
            mode: numberValue('sourceDetectionMode'),
            debounce_ms: numberValue('sourceDebounceMs'),
            stale_timeout_ms: numberValue('sourceStaleMs'),
            single_input: {
                meter_index: numberValue('sourceSingleMeter'),
                function: numberValue('sourceSingleFunction'),
                address_base: numberValue('sourceSingleBase'),
                register: numberValue('sourceSingleRegister'),
                grid_value: numberValue('sourceGridValue'),
                generator_value: numberValue('sourceGeneratorValue')
            },
            dual_meter: {
                grid_meter_index: numberValue('sourceGridMeter'),
                generator_meter_index: numberValue('sourceGeneratorMeter'),
                grid_threshold_kw: numberValue('sourceGridThreshold'),
                generator_threshold_kw: numberValue('sourceGeneratorThreshold')
            }
        };

        try {
            setBusy(true);
            await api('/api/source-detection', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });
            setMessage('Source-detection configuration persisted. Automatic control remains disabled.', 'good');
            await renderSource();
        } catch (error) {
            setMessage(`Source-detection save failed: ${error.message}`, 'bad');
        } finally {
            setBusy(false);
        }
    }

    async function renderSource(signal) {
        const data = await api('/api/source-detection', { signal, timeoutMs: 5000 });
        setContent(...statusPanels(data), buildConfiguration(data));
    }

    registerTab('source', 'Source detection', renderSource);
})();
