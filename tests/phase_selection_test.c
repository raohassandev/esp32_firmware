/*
 * Which phase the grid policy is enforced on.
 *
 * A three-phase site is rarely balanced. Enforce zero export on the TOTAL and
 * one phase can import while the other two export: the total reads zero, the
 * utility's per-phase meter does not, and the customer who bought this
 * controller specifically to guarantee zero export is exporting.
 *
 * The reason all three policies converge on min() is worth executing rather than
 * asserting, because it is easy to get backwards: the owner described the
 * limited-export case as controlling the "highest" phase, meaning the highest
 * EXPORT, which under this firmware's import-positive sign convention is the
 * LOWEST number.
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "phase_selection.h"

static phase_selection_input_t balanced(uint8_t basis)
{
    phase_selection_input_t input = {
        .phase_kw = { 30.0f, 30.0f, 30.0f },
        .phase_valid = { true, true, true },
        .total_kw = 90.0f,
        .total_valid = true,
        .basis = basis,
    };
    return input;
}

static void test_total_basis_uses_the_total(void)
{
    phase_selection_input_t input = balanced(PHASE_BASIS_TOTAL);
    input.phase_kw[1] = -50.0f;   /* one phase exporting hard */
    phase_selection_t out = phase_selection_evaluate(&input);
    assert(out.valid);
    assert(!out.per_phase_applied);
    assert(out.phase_index == PHASE_SELECTION_TOTAL);
    assert(out.controlling_kw == 90.0f);
}

/*
 * THE CASE THE WHOLE FEATURE EXISTS FOR. The total is a comfortable import while
 * one phase is exporting. On the total basis the controller sees no problem at
 * all; on the per-phase basis it regulates against the exporting conductor.
 */
static void test_unbalanced_site_selects_the_exporting_phase(void)
{
    phase_selection_input_t input = {
        .phase_kw = { 60.0f, -20.0f, 50.0f },
        .phase_valid = { true, true, true },
        .total_kw = 90.0f,
        .total_valid = true,
        .basis = PHASE_BASIS_PER_PHASE,
    };
    phase_selection_t out = phase_selection_evaluate(&input);
    assert(out.valid && out.per_phase_applied);
    assert(out.phase_index == 1);
    assert(out.controlling_kw == -20.0f);

    /* And the same site on the total basis sees 90 kW of import: nothing to do. */
    input.basis = PHASE_BASIS_TOTAL;
    out = phase_selection_evaluate(&input);
    assert(out.controlling_kw == 90.0f);
}

/* Closest to exporting, when none is exporting yet: still the minimum. */
static void test_selects_the_phase_closest_to_exporting(void)
{
    phase_selection_input_t input = balanced(PHASE_BASIS_PER_PHASE);
    input.phase_kw[0] = 40.0f;
    input.phase_kw[1] = 35.0f;
    input.phase_kw[2] = 2.0f;
    phase_selection_t out = phase_selection_evaluate(&input);
    assert(out.phase_index == 2 && out.controlling_kw == 2.0f);
}

/* Exporting most: the largest export is the most negative number, so min()
 * serves the limited-export policy too. */
static void test_selects_the_phase_exporting_most(void)
{
    phase_selection_input_t input = balanced(PHASE_BASIS_PER_PHASE);
    input.phase_kw[0] = -5.0f;
    input.phase_kw[1] = -40.0f;
    input.phase_kw[2] = -12.0f;
    phase_selection_t out = phase_selection_evaluate(&input);
    assert(out.phase_index == 1 && out.controlling_kw == -40.0f);
}

/*
 * FAILS TOWARD THE TOTAL, NOT TOWARD A GUESS. With one phase missing, the worst
 * phase cannot be identified -- the missing one might be it. Regulating against
 * the worst of the remaining two would enforce a limit on the wrong conductor
 * AND report that per-phase control was in force.
 */
static void test_one_missing_phase_falls_back_to_the_total(void)
{
    phase_selection_input_t input = balanced(PHASE_BASIS_PER_PHASE);
    input.phase_kw[0] = -80.0f;      /* would have been chosen */
    input.phase_valid[1] = false;    /* but a different phase is missing */
    phase_selection_t out = phase_selection_evaluate(&input);
    assert(out.valid);
    assert(!out.per_phase_applied);
    assert(out.phase_index == PHASE_SELECTION_TOTAL);
    assert(out.controlling_kw == 90.0f);
}

static void test_non_finite_phase_is_treated_as_missing(void)
{
    phase_selection_input_t input = balanced(PHASE_BASIS_PER_PHASE);
    input.phase_kw[2] = NAN;
    phase_selection_t out = phase_selection_evaluate(&input);
    assert(!out.per_phase_applied && out.phase_index == PHASE_SELECTION_TOTAL);
}

/* No usable measurement at all is invalid, not zero. The caller's freshness gate
 * then holds PV down; reporting 0 kW would read as a balanced site. */
static void test_no_usable_measurement_is_invalid(void)
{
    phase_selection_input_t input = balanced(PHASE_BASIS_PER_PHASE);
    input.phase_valid[0] = false;
    input.total_valid = false;
    phase_selection_t out = phase_selection_evaluate(&input);
    assert(!out.valid);

    input.total_valid = true;
    input.total_kw = NAN;
    out = phase_selection_evaluate(&input);
    assert(!out.valid);
}

static void test_null_input_is_invalid(void)
{
    phase_selection_t out = phase_selection_evaluate(NULL);
    assert(!out.valid && !out.per_phase_applied);
}

/* Ties pick a deterministic phase rather than depending on iteration order. */
static void test_tie_is_deterministic(void)
{
    phase_selection_input_t input = balanced(PHASE_BASIS_PER_PHASE);
    input.phase_kw[0] = -10.0f;
    input.phase_kw[1] = -10.0f;
    input.phase_kw[2] = 5.0f;
    assert(phase_selection_evaluate(&input).phase_index == 0);
}

int main(void)
{
    test_total_basis_uses_the_total();
    test_unbalanced_site_selects_the_exporting_phase();
    test_selects_the_phase_closest_to_exporting();
    test_selects_the_phase_exporting_most();
    test_one_missing_phase_falls_back_to_the_total();
    test_non_finite_phase_is_treated_as_missing();
    test_no_usable_measurement_is_invalid();
    test_null_input_is_invalid();
    test_tie_is_deterministic();
    printf("phase selection tests passed\n");
    return 0;
}
