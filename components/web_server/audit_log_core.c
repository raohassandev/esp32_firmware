#include "audit_log_core.h"

/* Fixed vocabulary. The exported payload is assembled exclusively from these
 * strings, so no operator-supplied byte can ever reach an audit line. */
static const char *const CATEGORY_NAMES[AUDIT_CATEGORY_COUNT] = {
    "control",
    "configuration",
    "authentication",
};

static const char *const ACTION_NAMES[AUDIT_ACTION_COUNT] = {
    "control_enabled",
    "control_disabled",
    "control_setpoint_changed",
    "control_tuning_changed",
    "control_ramp_changed",
    "control_source_mode_changed",
    "configuration_persisted",
    "source_model_persisted",
    "login",
    "logout",
    "password_change",
    "audit_log_read",
};

static const char *const OUTCOME_NAMES[AUDIT_OUTCOME_COUNT] = {
    "success",
    "failure",
    "denied",
    "locked_out",
};

void audit_log_core_reset(audit_ring_t *ring)
{
    if (!ring) return;
    ring->next_sequence = 0;
    ring->overwritten = 0;
    ring->count = 0;
    ring->oldest = 0;
    for (uint16_t index = 0; index < AUDIT_LOG_CAPACITY; ++index) {
        ring->entries[index].sequence = 0;
        ring->entries[index].uptime_ms = 0;
        ring->entries[index].value = 0.0f;
        ring->entries[index].category = 0;
        ring->entries[index].action = 0;
        ring->entries[index].outcome = 0;
        ring->entries[index].has_value = false;
    }
}

uint32_t audit_log_core_append(audit_ring_t *ring,
                               uint64_t uptime_ms,
                               uint8_t category,
                               uint8_t action,
                               uint8_t outcome,
                               bool has_value,
                               float value)
{
    if (!ring) return 0;
    if (category >= (uint8_t)AUDIT_CATEGORY_COUNT) return 0;
    if (action >= (uint8_t)AUDIT_ACTION_COUNT) return 0;
    if (outcome >= (uint8_t)AUDIT_OUTCOME_COUNT) return 0;

    uint16_t slot;
    if (ring->count < AUDIT_LOG_CAPACITY) {
        slot = (uint16_t)((ring->oldest + ring->count) % AUDIT_LOG_CAPACITY);
        ring->count = (uint16_t)(ring->count + 1u);
    } else {
        /* The ring is full: the oldest record is dropped, and the drop is
         * counted so an investigator can see that evidence was lost rather than
         * silently reading a truncated trail as complete. */
        slot = ring->oldest;
        ring->oldest = (uint16_t)((ring->oldest + 1u) % AUDIT_LOG_CAPACITY);
        if (ring->overwritten != UINT32_MAX) ring->overwritten++;
    }

    /* Sequence numbering is independent of the ring, so a wrap is visible as a
     * gap between the reported oldest sequence and 1. */
    ring->next_sequence++;

    audit_entry_t *entry = &ring->entries[slot];
    entry->sequence = ring->next_sequence;
    entry->uptime_ms = uptime_ms;
    entry->value = has_value ? value : 0.0f;
    entry->category = category;
    entry->action = action;
    entry->outcome = outcome;
    entry->has_value = has_value;
    return entry->sequence;
}

uint16_t audit_log_core_count(const audit_ring_t *ring)
{
    return ring ? ring->count : 0u;
}

uint32_t audit_log_core_overwritten(const audit_ring_t *ring)
{
    return ring ? ring->overwritten : 0u;
}

uint32_t audit_log_core_last_sequence(const audit_ring_t *ring)
{
    return ring ? ring->next_sequence : 0u;
}

bool audit_log_core_get(const audit_ring_t *ring, uint16_t index, audit_entry_t *out_entry)
{
    if (!ring || !out_entry || index >= ring->count) return false;
    *out_entry = ring->entries[(ring->oldest + index) % AUDIT_LOG_CAPACITY];
    return true;
}

const char *audit_log_core_category_name(uint8_t category)
{
    return category < (uint8_t)AUDIT_CATEGORY_COUNT ? CATEGORY_NAMES[category] : "unknown";
}

const char *audit_log_core_action_name(uint8_t action)
{
    return action < (uint8_t)AUDIT_ACTION_COUNT ? ACTION_NAMES[action] : "unknown";
}

const char *audit_log_core_outcome_name(uint8_t outcome)
{
    return outcome < (uint8_t)AUDIT_OUTCOME_COUNT ? OUTCOME_NAMES[outcome] : "unknown";
}
