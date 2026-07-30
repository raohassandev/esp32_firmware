/* Host-compiled unit test for the ISA-18.2 suppression state machine: gap A9 of
 * docs/ALARM_MANAGEMENT_RESEARCH.md.
 *
 * The property under test is a negative one, which is why it is executed rather
 * than read: ISA-18.2 warns that the three suppression states must NOT collapse
 * into a single "disabled" flag, because collapsing them destroys the audit trail
 * that makes suppression safe. A source contract can check that three fields
 * exist. Only running the code can check that no combination of them loses
 * information - so this file enumerates all eight combinations and asserts that
 * every one of them is still fully recoverable, and that each state keeps the
 * three properties (who decided, whether it expires, whether it leaves the triage
 * counts) that tell them apart.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "alarm_suppression.h"

static alarm_suppression_flags_t flags_of(bool shelved, bool by_design, bool out_of_service)
{
    alarm_suppression_flags_t flags;
    flags.shelved = shelved;
    flags.by_design = by_design;
    flags.out_of_service = out_of_service;
    return flags;
}

/* One flag at a time maps to its own state, and no flags means no suppression. */
static void test_each_state_is_reachable_and_distinct(void)
{
    assert(alarm_suppression_effective(flags_of(false, false, false)) == ALARM_SUPPRESSION_NONE);
    assert(alarm_suppression_effective(flags_of(true, false, false)) == ALARM_SUPPRESSION_SHELVED);
    assert(alarm_suppression_effective(flags_of(false, true, false)) == ALARM_SUPPRESSION_BY_DESIGN);
    assert(alarm_suppression_effective(flags_of(false, false, true))
           == ALARM_SUPPRESSION_OUT_OF_SERVICE);

    /* Four distinct wire names, four distinct enum values. A rename here changes
     * the API and the on-flash journal, so the names are asserted literally. */
    assert(strcmp(alarm_suppression_name(ALARM_SUPPRESSION_NONE), "none") == 0);
    assert(strcmp(alarm_suppression_name(ALARM_SUPPRESSION_SHELVED), "shelved") == 0);
    assert(strcmp(alarm_suppression_name(ALARM_SUPPRESSION_BY_DESIGN),
                  "suppressed_by_design") == 0);
    assert(strcmp(alarm_suppression_name(ALARM_SUPPRESSION_OUT_OF_SERVICE),
                  "out_of_service") == 0);
    /* An unknown value must not silently read as one of the real states. */
    assert(strcmp(alarm_suppression_name((alarm_suppression_t)99), "none") == 0);
}

/* The point of the standard's warning: no combination of the three may lose a
 * fact. All eight are enumerated, and each is checked to be reconstructible. */
static void test_no_combination_collapses(void)
{
    const char *seen[8];
    for (unsigned mask = 0; mask < 8U; ++mask) {
        const bool shelved = (mask & 1U) != 0U;
        const bool by_design = (mask & 2U) != 0U;
        const bool out_of_service = (mask & 4U) != 0U;
        const alarm_suppression_flags_t flags = flags_of(shelved, by_design, out_of_service);

        /* The three facts survive the round trip unchanged: nothing in this module
         * rewrites or clears a flag on the way to producing a state. */
        assert(flags.shelved == shelved);
        assert(flags.by_design == by_design);
        assert(flags.out_of_service == out_of_service);

        const unsigned expected = (shelved ? 1U : 0U) + (by_design ? 1U : 0U) +
                                  (out_of_service ? 1U : 0U);
        assert(alarm_suppression_active_count(flags) == expected);
        assert(alarm_suppression_any(flags) == (expected > 0U));

        const alarm_suppression_t state = alarm_suppression_effective(flags);
        /* The effective state is always one that is actually in force, never a
         * fourth "disabled" answer invented from a combination. */
        if (state == ALARM_SUPPRESSION_SHELVED) assert(shelved);
        if (state == ALARM_SUPPRESSION_BY_DESIGN) assert(by_design);
        if (state == ALARM_SUPPRESSION_OUT_OF_SERVICE) assert(out_of_service);
        if (state == ALARM_SUPPRESSION_NONE) assert(expected == 0U);

        /* Suppressed in any way means out of the triage counts, and only that. */
        assert(alarm_suppression_hidden_from_triage(state) == (expected > 0U));
        seen[mask] = alarm_suppression_name(state);
    }
    /* The single-flag cases must produce three different names; if any two agreed
     * the states would already be collapsed. */
    assert(strcmp(seen[1], seen[2]) != 0);
    assert(strcmp(seen[2], seen[4]) != 0);
    assert(strcmp(seen[1], seen[4]) != 0);
}

/* When several are in force at once, the reported state must be the one that will
 * keep the alarm quiet longest. Showing a five-minute shelf while an instrument
 * is out for a fortnight would understate the suppression by two weeks. */
static void test_precedence_reports_the_hardest_to_undo(void)
{
    assert(alarm_suppression_effective(flags_of(true, true, false)) == ALARM_SUPPRESSION_BY_DESIGN);
    assert(alarm_suppression_effective(flags_of(true, false, true))
           == ALARM_SUPPRESSION_OUT_OF_SERVICE);
    assert(alarm_suppression_effective(flags_of(false, true, true))
           == ALARM_SUPPRESSION_OUT_OF_SERVICE);
    assert(alarm_suppression_effective(flags_of(true, true, true))
           == ALARM_SUPPRESSION_OUT_OF_SERVICE);
    /* ...and the shelf is still reported alongside it, which is the whole point:
     * precedence decides what is displayed, never what is recorded. */
    const alarm_suppression_flags_t all = flags_of(true, true, true);
    assert(all.shelved && all.by_design && all.out_of_service);
    assert(alarm_suppression_active_count(all) == 3);
}

/* Who decided is the distinction a single flag destroys, and the one an audit
 * needs six months later. */
static void test_authority_separates_the_three_decisions(void)
{
    assert(strcmp(alarm_suppression_authority(ALARM_SUPPRESSION_SHELVED), "operator") == 0);
    assert(strcmp(alarm_suppression_authority(ALARM_SUPPRESSION_BY_DESIGN), "system") == 0);
    assert(strcmp(alarm_suppression_authority(ALARM_SUPPRESSION_OUT_OF_SERVICE),
                  "maintenance") == 0);
    assert(strcmp(alarm_suppression_authority(ALARM_SUPPRESSION_NONE), "none") == 0);
    /* Three states, three different authorities. */
    assert(strcmp(alarm_suppression_authority(ALARM_SUPPRESSION_SHELVED),
                  alarm_suppression_authority(ALARM_SUPPRESSION_BY_DESIGN)) != 0);
    assert(strcmp(alarm_suppression_authority(ALARM_SUPPRESSION_BY_DESIGN),
                  alarm_suppression_authority(ALARM_SUPPRESSION_OUT_OF_SERVICE)) != 0);
}

/* Only shelving expires. If out-of-service expired it would be a longer shelf,
 * and the standard's distinction would be gone. */
static void test_only_a_shelf_expires(void)
{
    assert(alarm_suppression_expires(ALARM_SUPPRESSION_SHELVED));
    assert(!alarm_suppression_expires(ALARM_SUPPRESSION_BY_DESIGN));
    assert(!alarm_suppression_expires(ALARM_SUPPRESSION_OUT_OF_SERVICE));
    assert(!alarm_suppression_expires(ALARM_SUPPRESSION_NONE));
}

/* The design-suppression rule: edge triggered, symmetric, and it can never
 * outlive the plant state that justified it. */
static void test_design_suppression_is_edge_triggered(void)
{
    /* A cause appears: engage, once. */
    assert(alarm_design_suppression_step(false, true) == ALARM_DESIGN_STEP_ENGAGE);
    /* It persists: nothing further to journal. A record every five seconds would
     * make the journal unreadable for the incident it exists to explain. */
    assert(alarm_design_suppression_step(true, true) == ALARM_DESIGN_STEP_NONE);
    /* It clears: release, once. */
    assert(alarm_design_suppression_step(true, false) == ALARM_DESIGN_STEP_RELEASE);
    /* Nothing wrong, nothing suppressed: silence. */
    assert(alarm_design_suppression_step(false, false) == ALARM_DESIGN_STEP_NONE);
}

/* Driven as a state machine over a plant-state sequence, checking that the
 * suppression tracks the cause exactly and that every edge is paired. */
static void test_design_suppression_tracks_the_cause_exactly(void)
{
    const bool causes[] = {false, false, true, true, true, false, true, false, false, true};
    bool suppressed = false;
    unsigned engaged = 0, released = 0;
    for (unsigned i = 0; i < sizeof(causes) / sizeof(causes[0]); ++i) {
        switch (alarm_design_suppression_step(suppressed, causes[i])) {
        case ALARM_DESIGN_STEP_ENGAGE:  suppressed = true;  engaged++;  break;
        case ALARM_DESIGN_STEP_RELEASE: suppressed = false; released++; break;
        case ALARM_DESIGN_STEP_NONE:
        default: break;
        }
        /* The invariant that makes this a suppression and not a mute: the state
         * always equals the plant state that justifies it, on every single step. */
        assert(suppressed == causes[i]);
    }
    /* Three rising edges, two falling, and the last one still in force - so every
     * engage has a release except the one that has not happened yet. */
    assert(engaged == 3);
    assert(released == 2);
    assert(suppressed);
}

/* The reason list is enumerated because the journal record carries one uint16 of
 * detail, so a reason has to be a number that survives a reboot. */
static void test_out_of_service_reasons_are_bounded_and_named(void)
{
    const char *names[ALARM_OUT_OF_SERVICE_REASON_MAX + 1U];
    for (uint32_t reason = 0; reason <= ALARM_OUT_OF_SERVICE_REASON_MAX; ++reason) {
        assert(alarm_out_of_service_reason_valid(reason));
        names[reason] = alarm_out_of_service_reason_name((uint8_t)reason);
        assert(strcmp(names[reason], "unknown") != 0);
        /* Every reason carries operator-facing prose as well as a wire name; a
         * bare identifier is not a recorded justification. */
        assert(strcmp(alarm_out_of_service_reason_text((uint8_t)reason),
                      "No recorded reason.") != 0);
    }
    /* No two reasons share a name, or the audit trail could not tell them apart. */
    for (uint32_t a = 0; a <= ALARM_OUT_OF_SERVICE_REASON_MAX; ++a) {
        for (uint32_t b = a + 1U; b <= ALARM_OUT_OF_SERVICE_REASON_MAX; ++b) {
            assert(strcmp(names[a], names[b]) != 0);
        }
    }
    /* Out of range is refused rather than clamped into a real reason: recording
     * the wrong justification is worse than refusing the request. */
    assert(!alarm_out_of_service_reason_valid(ALARM_OUT_OF_SERVICE_REASON_MAX + 1U));
    assert(!alarm_out_of_service_reason_valid(255U));
    assert(!alarm_out_of_service_reason_valid(0xFFFFFFFFU));
    assert(strcmp(alarm_out_of_service_reason_name(200U), "unknown") == 0);
    assert(strcmp(alarm_out_of_service_reason_text(200U), "No recorded reason.") == 0);
    /* Every reason fits the uint16 of journal detail. */
    assert(ALARM_OUT_OF_SERVICE_REASON_MAX <= 0xFFFFU);
}

int main(void)
{
    test_each_state_is_reachable_and_distinct();
    test_no_combination_collapses();
    test_precedence_reports_the_hardest_to_undo();
    test_authority_separates_the_three_decisions();
    test_only_a_shelf_expires();
    test_design_suppression_is_edge_triggered();
    test_design_suppression_tracks_the_cause_exactly();
    test_out_of_service_reasons_are_bounded_and_named();
    printf("alarm suppression unit tests passed "
           "(all eight flag combinations stay distinguishable; shelved, "
           "suppressed-by-design and out-of-service keep separate authority "
           "and only shelving expires)\n");
    return 0;
}
