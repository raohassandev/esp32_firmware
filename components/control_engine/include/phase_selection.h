#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * WHICH PHASE THE GRID POLICY IS ENFORCED ON.
 *
 * A three-phase site is rarely balanced. Enforce zero export on the TOTAL and
 * one phase can be importing while the other two export: the total reads zero,
 * the utility's per-phase meter does not, and the customer who bought this
 * controller to guarantee zero export is exporting.
 *
 * The correct phase to control is not a single choice, because the three
 * policies protect against opposite failures:
 *
 *   ZERO EXPORT     no phase may export, so control the phase closest to
 *                   exporting -- the LOWEST reading under import-positive sign.
 *   MINIMUM IMPORT  every phase must keep importing, so control the phase
 *                   closest to losing its import -- again the LOWEST.
 *   LIMITED EXPORT  no phase may exceed the approved export limit, so control
 *                   the phase exporting MOST -- the lowest signed value is the
 *                   largest export, so it is again the minimum. But the limit
 *                   is applied to that phase's own allowance, not to a third of
 *                   the site total.
 *
 * In this firmware's sign convention active power is IMPORT-POSITIVE, so
 * "closest to exporting" and "exporting most" are both the minimum signed
 * value. The three cases converge on min() for that reason and not by accident,
 * which is worth stating because the product owner described the limited-export
 * case as "highest" -- highest EXPORT, which under import-positive sign is the
 * lowest number.
 *
 * Selection is a pure function so the reasoning above is executed by a test
 * rather than asserted in a comment.
 */

typedef enum {
    /* Regulate on the sum of the three phases, which is what a single total
     * register gives. The only correct choice on a genuinely balanced site, and
     * the only possible one when the meter's per-phase registers are unknown. */
    PHASE_BASIS_TOTAL = 0,
    /* Regulate on the worst phase. */
    PHASE_BASIS_PER_PHASE = 1
} phase_basis_t;

typedef struct {
    /* Import-positive kW per phase. */
    float phase_kw[3];
    /* False for any phase whose measurement is missing, stale or non-finite. */
    bool phase_valid[3];
    /* The total, used whenever the basis is TOTAL or per-phase evidence is
     * incomplete. */
    float total_kw;
    bool total_valid;
    uint8_t basis; /* phase_basis_t */
} phase_selection_input_t;

typedef struct {
    /* The kW figure the policy should regulate against. Meaningless when
     * valid is false. */
    float controlling_kw;
    bool valid;
    /* Which phase was chosen, 0..2, or 0xFF when the total was used. */
    uint8_t phase_index;
    /* True when per-phase was requested and actually applied. False means the
     * total was used, and the reason is worth showing: an engineer who
     * commissioned per-phase control needs to know it is not in force. */
    bool per_phase_applied;
} phase_selection_t;

#define PHASE_SELECTION_TOTAL 0xFFu

/*
 * Picks the phase the grid policy is enforced on.
 *
 * FAILS TOWARD THE TOTAL, NOT TOWARD A GUESS. If per-phase control is
 * commissioned but any one phase is missing or stale, the worst phase cannot be
 * identified -- the missing one might be it. Selecting the worst of the
 * remaining two would silently regulate against a phase that is not the worst
 * and would report success while doing it. So the total is used instead and
 * per_phase_applied is false, which is a weaker guarantee reported honestly
 * rather than a stronger one asserted falsely.
 *
 * If the total is also invalid the result is invalid, and the caller's existing
 * freshness gate holds PV at zero.
 */
phase_selection_t phase_selection_evaluate(const phase_selection_input_t *input);

#ifdef __cplusplus
}
#endif
