'use strict';

/* Property tests for the source-detection presentation helpers.
 *
 * These are the assertions that stop the interface from telling an operator the
 * plant is on grid while the controller is fail-closed. They are written as
 * properties over generated inputs rather than as examples, because the failure
 * mode being guarded against is a combination nobody thought to hand-check. */

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const utils = require('../source-detection.js');

const FIRMWARE_STATES = utils.FIRMWARE_STATES;
const CONFIDENT_LABELS = ['GRID', 'GENERATOR'];

/* ------------------------------------------------------------------ vocabulary */

assert.deepStrictEqual(FIRMWARE_STATES.slice().sort(), ['generator', 'grid', 'unknown'],
    'the firmware publishes exactly grid / generator / unknown');

/* The anchor. Each firmware state means one specific supply, and the operator
 * label must be that supply and not the other one. Everything else in this file
 * is a property; this is the ground truth those properties are relative to. */
assert.strictEqual(utils.sourceLabelFor('grid'), 'GRID');
assert.strictEqual(utils.sourceLabelFor('generator'), 'GENERATOR');
assert.strictEqual(utils.sourceLabelFor('unknown'), 'UNKNOWN');
assert.strictEqual(utils.SOURCE_LABELS.grid, 'GRID');
assert.strictEqual(utils.SOURCE_LABELS.generator, 'GENERATOR');

/* Every firmware state maps to exactly one operator label, and no two states
 * share a label. */
const labels = FIRMWARE_STATES.map((state) => utils.sourceLabelFor(state));
assert.strictEqual(new Set(labels).size, FIRMWARE_STATES.length,
    'each firmware state must map to its own operator label');
labels.forEach((label) => assert.match(label, /^[A-Z]+$/, 'source labels are single upper-case identities'));

/* Anything the firmware could not have said resolves to UNKNOWN, never to a
 * confident identity. */
['', '  ', 'GRID ', 'utility', 'genset', null, undefined, 42, {}].forEach((value) => {
    const label = utils.sourceLabelFor(value);
    assert.ok(label === 'UNKNOWN' || CONFIDENT_LABELS.includes(label));
    if (!FIRMWARE_STATES.includes(value)) {
        assert.strictEqual(label, 'UNKNOWN', `unrecognised state ${JSON.stringify(value)} must read UNKNOWN`);
    }
});

/* ------------------------------------------------- absent input is never a source */

[null, undefined, {}, { status: null }, { status: 'nope' }, 7, 'grid'].forEach((payload) => {
    const view = utils.describeSourceDetection(payload);
    assert.strictEqual(view.confident, false, 'absent input is never confident');
    assert.strictEqual(view.sourceLabel, 'UNKNOWN', 'absent input never yields a confident source');
    assert.strictEqual(view.reasonText, '', 'no firmware sentence may be invented');
    assert.ok(!/curtailed to protect/i.test(view.controlConsequence),
        'absent input must not claim the generator is being protected');
});

/* -------------------------------------------------------- exhaustive combinations */

function* statuses() {
    const flags = [true, false];
    for (const state of FIRMWARE_STATES.concat(['', 'Grid', 'bogus'])) {
        for (const configured of flags) {
            for (const evidence_fresh of flags) {
                for (const fail_closed of flags) {
                    for (const transition_pending of flags) {
                        for (const conflict of flags) {
                            for (const detection_only of flags) {
                                yield {
                                    state,
                                    candidate_state: 'generator',
                                    configured,
                                    evidence_fresh,
                                    fail_closed,
                                    transition_pending,
                                    conflict,
                                    detection_only,
                                    tariff: 2,
                                    reason: 'Generator source resolved from dual-meter evidence',
                                    mode_a_limitation: '',
                                    successful_reads: 12,
                                    failed_reads: 3
                                };
                            }
                        }
                    }
                }
            }
        }
    }
}

const seen = { confident: 0, unknown: 0, curtailing: 0 };
const qualityLabels = new Set();

for (const status of statuses()) {
    const view = utils.describeSourceDetection({ status });
    qualityLabels.add(view.qualityLabel);

    /* PROPERTY 1. No non-empty confident source label is ever rendered while the
     * firmware reports fail_closed. */
    if (status.fail_closed) {
        assert.strictEqual(view.sourceLabel, 'UNKNOWN',
            `fail_closed must never render a confident source (state=${status.state})`);
        assert.strictEqual(view.confident, false, 'fail_closed is never confident');
    }

    /* PROPERTY 2. Debouncing, conflicting, stale, unconfigured and unknown
     * evidence are equally never a confident source. */
    if (status.transition_pending || status.conflict || !status.evidence_fresh ||
        !status.configured || !FIRMWARE_STATES.includes(status.state) || status.state === 'unknown') {
        assert.strictEqual(view.confident, false,
            `ambiguous evidence must not be confident: ${JSON.stringify(status)}`);
        assert.strictEqual(view.sourceLabel, 'UNKNOWN',
            `ambiguous evidence must read UNKNOWN: ${JSON.stringify(status)}`);
    }

    /* PROPERTY 3. A confident label may only be the label of the state the
     * firmware actually published. */
    if (view.confident) {
        seen.confident += 1;
        assert.strictEqual(view.sourceLabel, utils.SOURCE_LABELS[status.state],
            'a confident label must be the label of the published state');
        assert.ok(CONFIDENT_LABELS.includes(view.sourceLabel));
    } else {
        seen.unknown += 1;
    }

    /* PROPERTY 4. "PV curtailed to protect generator" is claimed only when the
     * plant is confidently on generator AND the firmware is not detection-only.
     * It is the one sentence on the screen that asserts the controller is
     * actively defending the genset. */
    const claimsCurtailment = /^PV curtailed to protect generator$/.test(view.controlConsequence);
    if (claimsCurtailment) {
        seen.curtailing += 1;
        assert.strictEqual(view.confident, true);
        assert.strictEqual(view.state, 'generator');
        assert.strictEqual(view.sourceLabel, 'GENERATOR',
            'the screen that says the generator is being protected must also say GENERATOR');
        assert.strictEqual(status.detection_only, false);
    }
    if (view.confident && status.state === 'generator' && !status.detection_only) {
        assert.ok(claimsCurtailment,
            'a confident generator with control enabled must state that PV is curtailed');
    }

    /* PROPERTY 5. On a confident grid the screen must say plainly that PV is not
     * being curtailed for source reasons - and must not say anything about
     * protecting the generator. */
    if (view.confident && status.state === 'grid') {
        assert.strictEqual(view.sourceLabel, 'GRID', 'a plant on grid must read GRID');
        assert.strictEqual(view.controlConsequence, 'PV not curtailed for source reasons');
        assert.ok(!/protect/i.test(view.controlConsequence + view.controlConsequenceDetail)
            || /NOT being curtailed/.test(view.controlConsequenceDetail));
    }

    /* PROPERTY 6. The control consequence is never empty. Silence about what the
     * controller is doing is the ambiguity being removed. */
    assert.ok(view.controlConsequence.length > 0, 'the control consequence is always stated');
    assert.ok(view.controlConsequenceDetail.length > 0, 'the control consequence is always explained');
    assert.ok(view.detail.length > 0, 'the source headline is always explained');

    /* PROPERTY 7. A tone of "good" is only ever attached to a confident grid. */
    if (view.sourceTone === 'good') {
        assert.strictEqual(view.confident, true);
        assert.strictEqual(view.state, 'grid');
    }
    if (!view.confident) assert.strictEqual(view.sourceTone, 'bad');

    /* PROPERTY 8. While debouncing, the candidate is named as a candidate and
     * never as the active source. */
    if (status.transition_pending) {
        assert.match(view.candidateNote, /not the active source/i);
    } else {
        assert.strictEqual(view.candidateNote, '');
    }
}

assert.ok(seen.confident > 0 && seen.unknown > 0 && seen.curtailing > 0,
    'the generated space must exercise confident, unknown and curtailing outcomes');

/* -------------------------------------------------------------- verbatim firmware */

const verbatimReason = 'Both meters are above threshold; source is ambiguous and control stays inhibited';
const withReason = utils.describeSourceDetection({
    status: {
        state: 'unknown', candidate_state: 'unknown', configured: true, evidence_fresh: true,
        fail_closed: true, transition_pending: false, conflict: true, detection_only: true,
        tariff: 0, reason: verbatimReason, mode_a_limitation: 'Mode A has no redundant evidence',
        successful_reads: 4, failed_reads: 9
    }
});
assert.strictEqual(withReason.reasonText, verbatimReason,
    'the firmware reason is rendered exactly as received');
assert.strictEqual(withReason.limitationText, 'Mode A has no redundant evidence');
assert.strictEqual(withReason.sourceLabel, 'UNKNOWN');
assert.strictEqual(withReason.readCounts, '4 successful · 9 failed');
assert.strictEqual(withReason.tariffLabel, 'None');

/* Tariff wording follows the firmware's own numbering and nothing else. */
assert.strictEqual(utils.tariffLabel(1), 'Tariff 1');
assert.strictEqual(utils.tariffLabel(2), 'Tariff 2');
[0, 3, null, undefined, 'grid'].forEach((value) => assert.strictEqual(utils.tariffLabel(value), 'None'));

/* ------------------------------------------------------------ shared vocabulary */

/* Every quality word this module can produce must already be a member of
 * AutomatrixUi.STATES in web/app.js. A second set of state words is the defect
 * that taxonomy exists to prevent, so the check is against app.js itself rather
 * than against a copy kept here. */
const appSource = fs.readFileSync(path.join(__dirname, '..', 'app.js'), 'utf8');
const statesBlock = appSource.slice(
    appSource.indexOf('const STATES = Object.freeze({'),
    appSource.indexOf('function verbatim(')
);
assert.ok(statesBlock.includes('dataQuality') && statesBlock.includes('workflow'),
    'the STATES vocabulary must be locatable in web/app.js');
qualityLabels.add(utils.describeSourceDetection(null).qualityLabel);
qualityLabels.forEach((label) => {
    assert.ok(statesBlock.includes(`'${label}'`),
        `"${label}" is not a word in AutomatrixUi.STATES; the interface must not invent a second vocabulary`);
});
assert.ok(qualityLabels.size >= 5, 'the quality families in use must be distinguishable');

console.log('Source detection presentation property tests passed');
