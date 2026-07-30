#pragma once

/* Controller audit trail - storage core.
 *
 * This translation unit is deliberately free of ESP-IDF, FreeRTOS, heap and
 * logging dependencies so that it can be compiled and executed on a host by
 * tests/audit_log_core_test.c. The ESP-IDF glue lives in audit_log.c.
 *
 * Two properties are structural rather than review-enforced:
 *
 *  1. An entry carries NO character data. There is no field into which a
 *     password, a password length, a session token, a Wi-Fi PSK or any other
 *     free-form operator text could ever be copied. Every human-readable string
 *     in the exported payload is produced from a fixed compiled-in table keyed
 *     by an enumerator. This repository is public and the controller logs
 *     authentication failures, so "cannot hold a secret" is a stronger
 *     guarantee than "is not currently given one".
 *
 *  2. audit_log_core_append() performs no allocation, no logging and no
 *     formatting. It is a bounded set of scalar stores, which is what makes it
 *     legal to call while the caller holds a spinlock with interrupts disabled.
 *
 * The controller has no wall clock and no operator identity model. Timestamps
 * are therefore uptime-relative milliseconds since boot, and an entry records
 * only that an authenticated engineering session performed the action. Neither
 * a date nor a user name is invented anywhere in this module.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Sized to survive a commissioning session without evicting its own start.
 * Entries are 24 bytes, so the ring costs about 3 KB of internal RAM. */
#define AUDIT_LOG_CAPACITY 128u

typedef enum {
    AUDIT_CATEGORY_CONTROL = 0,
    AUDIT_CATEGORY_CONFIGURATION = 1,
    AUDIT_CATEGORY_AUTHENTICATION = 2,
    AUDIT_CATEGORY_COUNT
} audit_category_t;

typedef enum {
    /* Control */
    AUDIT_ACTION_CONTROL_ENABLED = 0,
    AUDIT_ACTION_CONTROL_DISABLED = 1,
    AUDIT_ACTION_CONTROL_SETPOINT_CHANGED = 2,
    AUDIT_ACTION_CONTROL_TUNING_CHANGED = 3,
    AUDIT_ACTION_CONTROL_RAMP_CHANGED = 4,
    AUDIT_ACTION_CONTROL_MODE_CHANGED = 5,
    /* Configuration */
    AUDIT_ACTION_CONFIGURATION_PERSISTED = 6,
    AUDIT_ACTION_CONFIGURATION_SOURCE_MODEL_PERSISTED = 7,
    /* Authentication */
    AUDIT_ACTION_LOGIN = 8,
    AUDIT_ACTION_LOGOUT = 9,
    AUDIT_ACTION_PASSWORD_CHANGE = 10,
    AUDIT_ACTION_AUDIT_LOG_READ = 11,
    AUDIT_ACTION_COUNT
} audit_action_t;

typedef enum {
    AUDIT_OUTCOME_SUCCESS = 0,
    AUDIT_OUTCOME_FAILURE = 1,
    AUDIT_OUTCOME_DENIED = 2,
    AUDIT_OUTCOME_LOCKED_OUT = 3,
    AUDIT_OUTCOME_COUNT
} audit_outcome_t;

/* No char member exists here, and none may be added. See property (1) above. */
typedef struct {
    uint32_t sequence;   /* strictly increasing, never reused, starts at 1 */
    uint64_t uptime_ms;  /* milliseconds since boot; there is no wall clock */
    float value;         /* only meaningful when has_value is true */
    uint8_t category;    /* audit_category_t */
    uint8_t action;      /* audit_action_t */
    uint8_t outcome;     /* audit_outcome_t */
    bool has_value;
} audit_entry_t;

typedef struct {
    audit_entry_t entries[AUDIT_LOG_CAPACITY];
    uint32_t next_sequence;  /* sequence of the entry appended next, minus one */
    uint32_t overwritten;    /* entries evicted because the ring wrapped */
    uint16_t count;
    uint16_t oldest;
} audit_ring_t;

/* All state is zero-initialisable: a static audit_ring_t in BSS is already a
 * valid empty ring, so no start-up call is required and no boot ordering
 * problem can leave the audit trail unarmed. */
void audit_log_core_reset(audit_ring_t *ring);

/* Appends one entry and returns its sequence number, or 0 if ring is NULL or an
 * enumerator is out of range. Allocation-free, log-free and format-free: safe to
 * call with interrupts disabled. */
uint32_t audit_log_core_append(audit_ring_t *ring,
                               uint64_t uptime_ms,
                               uint8_t category,
                               uint8_t action,
                               uint8_t outcome,
                               bool has_value,
                               float value);

uint16_t audit_log_core_count(const audit_ring_t *ring);
uint32_t audit_log_core_overwritten(const audit_ring_t *ring);
uint32_t audit_log_core_last_sequence(const audit_ring_t *ring);

/* index 0 is the oldest retained entry. */
bool audit_log_core_get(const audit_ring_t *ring, uint16_t index, audit_entry_t *out_entry);

/* Fixed compiled-in vocabulary. Never returns caller-supplied text. */
const char *audit_log_core_category_name(uint8_t category);
const char *audit_log_core_action_name(uint8_t action);
const char *audit_log_core_outcome_name(uint8_t outcome);
