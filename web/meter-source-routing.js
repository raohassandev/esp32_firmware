/* meter-source-routing.js — which page a meter's readings belong on.
 *
 * THE RULE THE PLANT OWNER STATED.
 *
 *   A grid meter's readings appear on the Grid power page. A generator meter's
 *   readings appear on the Generator power page. And on a single-meter tariff
 *   plant, where one EM500 measures whichever supply is live, its readings
 *   appear on the GRID page while the meter reports tariff 1 and on the
 *   GENERATOR page while it reports tariff 2.
 *
 * WHY THIS IS NOT COSMETIC. Every page in this product is read as a claim about
 * a physical thing. A generator's 280 kW drawn under a "Grid power" heading is
 * not a layout mistake; it is the screen stating that the plant is importing
 * from the utility when it is burning diesel. The owner found exactly that, and
 * the fix is not to rename the card -- it is to put the reading on the page
 * whose heading is true.
 *
 * WHICH RULE APPLIES DEPENDS ON HOW MANY SUPPLY METERS THE PLANT HAS.
 *
 *   ONE supply meter -- the tariff installation. A single instrument on the
 *   changeover, measuring whichever supply is live, so the CONTROLLER'S
 *   RESOLVED SOURCE decides and it overrides whatever role was commissioned.
 *   With one instrument the role label cannot be true of both halves of the
 *   day: a meter commissioned "grid" that is reading tariff 2 is measuring the
 *   generator, and no label makes that reading a grid reading.
 *
 *   TWO OR MORE -- separate instruments on separate supplies. The COMMISSIONED
 *   ROLE decides and no runtime signal moves a reading off the meter the
 *   installer wired it to. The controller resolves one live source for the
 *   site; applying that to two instruments would file one supply's reading
 *   against a meter that is not measuring it.
 *
 * FAIL-CLOSED. When neither settles it, the meter belongs to NO page. It is not
 * quietly filed under grid because grid is the common case -- a reading on the
 * wrong page is worse than a reading the operator has to go and find, and the
 * pages say so in words rather than showing an empty frame.
 */
(function () {
    'use strict';

    /* Mirrors meter_role_t in components/config_manager/include/config_types.h.
     * The numbers are the wire format; the names here are only for reading. */
    const ROLE_UNASSIGNED = 0;
    const ROLE_GRID = 1;
    const ROLE_GENERATOR = 2;

    /* The pages this module routes to. LOAD and PV meters have no page of their
     * own yet and are attributed to neither, which is honest: this module says
     * where a reading belongs, not where it could be squeezed in. */
    const GRID = 'grid';
    const GENERATOR = 'generator';

    function attributionOf(status) {
        const source = status && status.source;
        if (!source) return null;
        /* The firmware's own verdict, and only when it is trustworthy. The same
         * test web/source-attribution.js applies: configured, fresh evidence,
         * no conflict, and actually resolved to one of the two supplies. */
        if (source.configured !== true) return null;
        if (source.evidence_fresh !== true) return null;
        if (source.conflict === true) return null;
        if (source.attributed_to === GRID || source.attributed_to === GENERATOR) {
            return source.attributed_to;
        }
        return null;
    }

    /* The meters that could be measuring a supply: the two supply roles, and an
     * unassigned meter, which on this product is the tariff installation. A LOAD
     * or PV meter is a declared thing that is not a supply and is never counted
     * here. */
    function supplyMeters(list) {
        return (Array.isArray(list) ? list : []).filter((meter) => {
            const role = Number(meter && meter.role);
            return !Number.isFinite(role) || role === ROLE_UNASSIGNED
                || role === ROLE_GRID || role === ROLE_GENERATOR;
        });
    }

    /*
     * fleet is the full meter list, and it decides which rule applies.
     *
     * WITH ONE SUPPLY METER the installation is the tariff one: a single
     * instrument on the changeover, measuring whichever supply is live. The
     * controller's resolved attribution decides, and it OVERRIDES the
     * commissioned role -- because with one instrument the role label cannot be
     * true of both halves of the day. A meter commissioned "grid" that is
     * reading tariff 2 is measuring the generator, and drawing that under a
     * "Grid power" heading is the exact defect this module exists to stop.
     *
     * WITH TWO the roles are real and permanent: separate instruments on
     * separate supplies, and no runtime signal may move a reading off the meter
     * the installer wired it to.
     */
    /*
     * Where this meter's readings belong, and why.
     *
     * Returns { page, reason, byRole }, page being 'grid', 'generator' or null.
     * The reason is written for a person: a page that shows nothing must be
     * able to say what it is waiting for.
     */
    function attribute(meter, status, fleet) {
        const role = Number(meter && meter.role);
        const supplies = supplyMeters(fleet);
        const singleInstrument = supplies.length === 1
            && (role === ROLE_UNASSIGNED || role === ROLE_GRID || role === ROLE_GENERATOR
                || !Number.isFinite(role));

        if (singleInstrument) {
            const live = attributionOf(status);
            if (live === GRID) {
                return { page: GRID, byRole: false,
                    reason: 'the only supply meter, and it is measuring the '
                        + 'grid right now' };
            }
            if (live === GENERATOR) {
                return { page: GENERATOR, byRole: false,
                    reason: 'the only supply meter, and it is measuring the '
                        + 'generator right now' };
            }
            return { page: null, byRole: false,
                reason: 'the controller has not resolved which supply this meter '
                    + 'is measuring, so its readings are not attributed to '
                    + 'either page' };
        }

        if (role === ROLE_GRID) {
            return { page: GRID, byRole: true,
                reason: 'commissioned as the grid meter' };
        }
        if (role === ROLE_GENERATOR) {
            return { page: GENERATOR, byRole: true,
                reason: 'commissioned as the generator meter' };
        }
        if (Number.isFinite(role) && role !== ROLE_UNASSIGNED) {
            /* LOAD or PV. Declared, and not a supply. */
            return { page: null, byRole: true,
                reason: `commissioned as ${meter.role_name || 'another role'}, `
                    + 'which is not a supply meter' };
        }

        /* Unassigned, on a plant with more than one supply meter. There is
         * nothing to decide it with: the controller resolves ONE live source
         * for the site, and applying that to two instruments would put the same
         * supply's reading on a meter that is not measuring it. Fail closed and
         * say what is missing. */
        return { page: null, byRole: false,
            reason: 'this meter has not been commissioned as the grid or the '
                + 'generator meter, and this plant has more than one supply '
                + 'meter, so the controller cannot tell which supply it is on' };
    }

    /* The meters whose readings belong on one page, with the reason attached. */
    function metersFor(page, meters, status) {
        return (Array.isArray(meters) ? meters : [])
            .map((meter) => ({ meter, ...attribute(meter, status, meters) }))
            .filter((entry) => entry.page === page);
    }

    /*
     * What a page must say when it has nothing to show.
     *
     * Never an empty frame and never a zero. "No meter is attributed here" and
     * "the meter is attributed to the other page right now" are completely
     * different facts, and on a tariff plant the second one is the normal state
     * for half the day.
     */
    function absence(page, meters, status) {
        const list = Array.isArray(meters) ? meters : [];
        if (!list.length) return 'No meter has been commissioned on this controller.';

        const elsewhere = list
            .map((meter) => ({ meter, ...attribute(meter, status, list) }))
            .filter((entry) => entry.page && entry.page !== page);
        if (elsewhere.length) {
            const other = elsewhere[0];
            const otherName = other.page === GRID ? 'Grid power' : 'Generator power';
            /* Every reason that can reach here is a PREDICATE, so it composes
             * after "<name> is". A reason written as a full sentence produced
             * "Automatrix EM500 is this is the only supply meter...". */
            return `${other.meter.name || 'The meter'} is ${other.reason}. `
                + `Its readings are on the ${otherName} page.`;
        }

        const unattributed = list
            .map((meter) => ({ meter, ...attribute(meter, status, list) }))
            .filter((entry) => !entry.page);
        if (unattributed.length) return unattributed[0].reason;
        return 'No meter is attributed to this page.';
    }

    window.AutomatrixMeterRouting = Object.freeze({
        ROLE_UNASSIGNED, ROLE_GRID, ROLE_GENERATOR, GRID, GENERATOR,
        attribute, metersFor, absence, supplyMeters
    });
}());
