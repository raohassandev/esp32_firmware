/* source-attribution.js — whose power is this?
 *
 * WHY THIS MODULE EXISTS, STATED PLAINLY.
 *
 * The controller resolved GENERATOR from the EM-500 tariff input, and the home
 * page drew the measured 347.3 kW under GRID with the generator dimmed and
 * captioned "not running", while the site was running on the generator. Nine
 * separate modules label that measurement, and every one of them called it
 * "Grid" unconditionally.
 *
 * The measurement was never wrong. Its NAME was. On a single-meter tariff plant
 * one meter measures whichever source is live: the number is the same and what
 * it means changes with the tariff input. Source detection knew. Nothing else
 * asked.
 *
 * That is what this module is for, and the reason it is a module rather than a
 * fix in nine places: nine copies of a rule is nine chances to derive it
 * differently, and a tenth screen added next month would have had no reason to
 * consult any of them. One answer, and a source contract that fails the build if
 * a screen names this measurement without asking.
 *
 * THE HONEST THIRD ANSWER. A reader must be able to tell "the controller knows
 * this is the generator" from "the controller cannot establish the source". The
 * second is not a reason to guess "grid" -- it is a reason to say so. The
 * firmware settles this in /api/status: `source.attributed_to` is "grid",
 * "generator" or "unknown", and "unknown" is returned whenever the source is
 * unconfigured, stale, conflicting or fail-closed.
 *
 * The rule is NOT re-derived here. This reads the firmware's verdict, because
 * the firmware is what the control loop acts on, and a screen that reached a
 * different conclusion from the controller regulating the plant would be the
 * more convincing of the two.
 */
(function () {
    'use strict';

    /* What each answer is called on screen, and what may be said about it. */
    const NAMES = {
        grid: {
            node: 'grid',
            label: 'Grid',
            /* Import-positive: above zero the utility supplies the site. */
            importing: 'importing',
            exporting: 'exporting',
            idle: 'balanced'
        },
        generator: {
            node: 'generator',
            label: 'Generator',
            importing: 'supplying the plant',
            exporting: 'reverse power',
            idle: 'running, no load'
        },
        unknown: {
            node: 'unknown',
            label: 'Live source',
            importing: 'supplying the plant',
            exporting: 'reverse flow',
            idle: 'no flow'
        }
    };

    function attributedTo(status) {
        const value = status?.source?.attributed_to;
        return value === 'grid' || value === 'generator' ? value : 'unknown';
    }

    /*
     * The one call every screen makes.
     *
     * Returns what the measurement should be CALLED, whether that name may be
     * trusted, and why not when it may not. `known` false does not mean the
     * measurement is bad -- the meter may be answering perfectly. It means the
     * controller cannot say which source the meter is measuring, and a screen
     * must not decide that on its own.
     */
    function attribution(status) {
        const to = attributedTo(status);
        const names = NAMES[to];
        const source = status?.source || {};
        return {
            node: to,
            label: names.label,
            known: to !== 'unknown',
            /* Present only when the source is NOT established, so a caller can
             * say what is missing instead of showing a bare dash. */
            reason: to !== 'unknown' ? '' : (
                source.available === false ? 'Source detection is not running.'
                    : !source.configured ? 'Source detection is not commissioned, so the controller cannot say which supply is carrying the plant.'
                    : source.conflict ? 'Grid and generator both look live. The controller will not choose between them.'
                    : !source.evidence_fresh ? 'The source evidence is stale, so which supply is live is no longer established.'
                    : source.reason || 'The controller cannot establish which supply is carrying the plant.'),
            /* Direction in words, in the vocabulary of whichever source it is.
             * "exporting" is a normal grid state and a FAULT on a generator, so
             * the same sign must not read the same way on both. */
            direction(kw) {
                if (typeof kw !== 'number' || !Number.isFinite(kw)) return '';
                if (kw > 0.01) return names.importing;
                if (kw < -0.01) return names.exporting;
                return names.idle;
            }
        };
    }

    window.AutomatrixSource = { attribution };
}());
