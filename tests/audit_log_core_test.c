/* Host unit test for the controller audit trail storage core.
 *
 * Runs on the build machine with the same warning level as CI. It proves the
 * ordering, sequencing and eviction-accounting behaviour that an incident
 * investigation depends on, none of which can be checked by reading source. */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "audit_log_core.h"

static audit_ring_t ring;

static void test_empty_ring_is_valid_without_initialisation(void)
{
    /* The firmware relies on the ring being a static in BSS: no start-up call
     * exists, so a zeroed ring must already behave as an empty one. */
    static audit_ring_t zeroed;
    audit_entry_t entry;
    assert(audit_log_core_count(&zeroed) == 0);
    assert(audit_log_core_last_sequence(&zeroed) == 0);
    assert(audit_log_core_overwritten(&zeroed) == 0);
    assert(!audit_log_core_get(&zeroed, 0, &entry));
    assert(audit_log_core_append(&zeroed, 1, AUDIT_CATEGORY_AUTHENTICATION,
                                 AUDIT_ACTION_LOGIN, AUDIT_OUTCOME_FAILURE, false, 0.0f) == 1);
}

static void test_sequence_is_monotonic_from_one(void)
{
    audit_log_core_reset(&ring);
    assert(audit_log_core_append(&ring, 10, AUDIT_CATEGORY_CONTROL,
                                 AUDIT_ACTION_CONTROL_ENABLED, AUDIT_OUTCOME_SUCCESS,
                                 false, 0.0f) == 1);
    assert(audit_log_core_append(&ring, 20, AUDIT_CATEGORY_CONFIGURATION,
                                 AUDIT_ACTION_CONFIGURATION_PERSISTED, AUDIT_OUTCOME_SUCCESS,
                                 false, 0.0f) == 2);
    assert(audit_log_core_append(&ring, 30, AUDIT_CATEGORY_AUTHENTICATION,
                                 AUDIT_ACTION_LOGIN, AUDIT_OUTCOME_FAILURE, false, 0.0f) == 3);
    assert(audit_log_core_count(&ring) == 3);
    assert(audit_log_core_last_sequence(&ring) == 3);

    audit_entry_t entry;
    assert(audit_log_core_get(&ring, 0, &entry));
    assert(entry.sequence == 1 && entry.uptime_ms == 10);
    assert(audit_log_core_get(&ring, 2, &entry));
    assert(entry.sequence == 3 && entry.uptime_ms == 30);
    assert(!audit_log_core_get(&ring, 3, &entry));
}

static void test_all_three_categories_round_trip(void)
{
    audit_log_core_reset(&ring);
    audit_log_core_append(&ring, 1, AUDIT_CATEGORY_CONTROL,
                          AUDIT_ACTION_CONTROL_SETPOINT_CHANGED, AUDIT_OUTCOME_SUCCESS,
                          true, -12.5f);
    audit_log_core_append(&ring, 2, AUDIT_CATEGORY_CONFIGURATION,
                          AUDIT_ACTION_CONFIGURATION_PERSISTED, AUDIT_OUTCOME_FAILURE,
                          false, 0.0f);
    audit_log_core_append(&ring, 3, AUDIT_CATEGORY_AUTHENTICATION,
                          AUDIT_ACTION_LOGIN, AUDIT_OUTCOME_LOCKED_OUT, false, 0.0f);

    audit_entry_t entry;
    assert(audit_log_core_get(&ring, 0, &entry));
    assert(strcmp(audit_log_core_category_name(entry.category), "control") == 0);
    assert(strcmp(audit_log_core_action_name(entry.action), "control_setpoint_changed") == 0);
    assert(entry.has_value && entry.value < -12.0f && entry.value > -13.0f);

    assert(audit_log_core_get(&ring, 1, &entry));
    assert(strcmp(audit_log_core_category_name(entry.category), "configuration") == 0);
    assert(strcmp(audit_log_core_outcome_name(entry.outcome), "failure") == 0);
    assert(!entry.has_value);

    assert(audit_log_core_get(&ring, 2, &entry));
    assert(strcmp(audit_log_core_category_name(entry.category), "authentication") == 0);
    assert(strcmp(audit_log_core_action_name(entry.action), "login") == 0);
    assert(strcmp(audit_log_core_outcome_name(entry.outcome), "locked_out") == 0);
}

static void test_wrap_reports_lost_evidence(void)
{
    audit_log_core_reset(&ring);
    const uint32_t total = AUDIT_LOG_CAPACITY + 7u;
    for (uint32_t index = 0; index < total; ++index) {
        const uint32_t sequence =
            audit_log_core_append(&ring, index, AUDIT_CATEGORY_CONFIGURATION,
                                  AUDIT_ACTION_CONFIGURATION_PERSISTED, AUDIT_OUTCOME_SUCCESS,
                                  false, 0.0f);
        assert(sequence == index + 1u);
    }
    assert(audit_log_core_count(&ring) == AUDIT_LOG_CAPACITY);
    assert(audit_log_core_last_sequence(&ring) == total);

    /* A wrapped trail must be visibly incomplete rather than look whole. */
    assert(audit_log_core_overwritten(&ring) == 7u);

    audit_entry_t oldest;
    audit_entry_t newest;
    assert(audit_log_core_get(&ring, 0, &oldest));
    assert(audit_log_core_get(&ring, (uint16_t)(AUDIT_LOG_CAPACITY - 1u), &newest));
    assert(oldest.sequence == 8u);
    assert(newest.sequence == total);
    assert(newest.sequence - oldest.sequence == AUDIT_LOG_CAPACITY - 1u);
}

static void test_out_of_range_enumerators_are_refused(void)
{
    audit_log_core_reset(&ring);
    assert(audit_log_core_append(&ring, 1, (uint8_t)AUDIT_CATEGORY_COUNT,
                                 AUDIT_ACTION_LOGIN, AUDIT_OUTCOME_SUCCESS, false, 0.0f) == 0);
    assert(audit_log_core_append(&ring, 1, AUDIT_CATEGORY_CONTROL,
                                 (uint8_t)AUDIT_ACTION_COUNT, AUDIT_OUTCOME_SUCCESS,
                                 false, 0.0f) == 0);
    assert(audit_log_core_append(&ring, 1, AUDIT_CATEGORY_CONTROL, AUDIT_ACTION_LOGIN,
                                 (uint8_t)AUDIT_OUTCOME_COUNT, false, 0.0f) == 0);
    assert(audit_log_core_count(&ring) == 0);
    assert(audit_log_core_last_sequence(&ring) == 0);

    /* An unknown enumerator never yields caller text; it yields "unknown". */
    assert(strcmp(audit_log_core_category_name(200), "unknown") == 0);
    assert(strcmp(audit_log_core_action_name(200), "unknown") == 0);
    assert(strcmp(audit_log_core_outcome_name(200), "unknown") == 0);
}

static void test_null_ring_is_survivable(void)
{
    audit_entry_t entry;
    audit_log_core_reset(NULL);
    assert(audit_log_core_append(NULL, 1, AUDIT_CATEGORY_CONTROL, AUDIT_ACTION_LOGIN,
                                 AUDIT_OUTCOME_SUCCESS, false, 0.0f) == 0);
    assert(audit_log_core_count(NULL) == 0);
    assert(audit_log_core_overwritten(NULL) == 0);
    assert(audit_log_core_last_sequence(NULL) == 0);
    assert(!audit_log_core_get(NULL, 0, &entry));
    assert(!audit_log_core_get(&ring, 0, NULL));
}

int main(void)
{
    test_empty_ring_is_valid_without_initialisation();
    test_sequence_is_monotonic_from_one();
    test_all_three_categories_round_trip();
    test_wrap_reports_lost_evidence();
    test_out_of_range_enumerators_are_refused();
    test_null_ring_is_survivable();
    printf("audit log core unit tests passed\n");
    return 0;
}
