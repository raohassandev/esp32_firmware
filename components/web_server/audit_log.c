/* This translation unit owns the audit trail and must therefore see the REAL
 * persistence functions, not the audited wrappers. The build applies the
 * substitution only to the config-writing API sources; undefining here as well
 * keeps that guarantee local and obvious rather than dependent on reading
 * CMakeLists.txt. */
#ifdef config_manager_save
#undef config_manager_save
#endif
#ifdef solar_grid_config_save
#undef solar_grid_config_save
#endif

#include "audit_log.h"

#include <stdlib.h>
#include <string.h>

#include "config_manager.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

/* WHY RAM ONLY.
 *
 * The audit trail is held in internal RAM and is lost on reboot. That is a real
 * limitation and it is stated in the exported payload rather than left for a
 * field engineer to discover during an incident. It is the honest tradeoff
 * here: flash persistence for controller events is being added separately for
 * the alarm subsystem, and a second, uncoordinated writer on the same storage
 * partition would risk wear and layout conflicts with that work. Recording
 * every configuration write to flash would also mean the audit trail itself
 * generates flash wear proportional to commissioning activity.
 *
 * The ring is static and zero-initialised, so it is armed from the first
 * instruction of the application - there is no init call that a boot-order
 * change could skip and no window in which an early authentication failure
 * would go unrecorded. */
static audit_ring_t s_ring;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static uint64_t now_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000ULL;
}

static void record(audit_category_t category, audit_action_t action,
                   audit_outcome_t outcome, bool has_value, float value)
{
    /* now_ms() is read before taking the lock: esp_timer_get_time() must not be
     * called with interrupts disabled unnecessarily, and the critical section
     * below must contain nothing but scalar stores - no ESP_LOG, no malloc, no
     * cJSON. audit_log_core_append() is written to that rule. */
    const uint64_t uptime_ms = now_ms();
    portENTER_CRITICAL(&s_lock);
    (void)audit_log_core_append(&s_ring, uptime_ms, (uint8_t)category, (uint8_t)action,
                                (uint8_t)outcome, has_value, value);
    portEXIT_CRITICAL(&s_lock);
}

void audit_log_record(audit_category_t category, audit_action_t action, audit_outcome_t outcome)
{
    record(category, action, outcome, false, 0.0f);
}

void audit_log_record_value(audit_category_t category, audit_action_t action,
                            audit_outcome_t outcome, float value)
{
    record(category, action, outcome, true, value);
}

uint16_t audit_log_snapshot(audit_entry_t *out_entries, uint16_t max_entries,
                            uint32_t *out_overwritten, uint32_t *out_last_sequence)
{
    if (!out_entries || max_entries == 0u) return 0u;
    uint16_t copied = 0u;
    portENTER_CRITICAL(&s_lock);
    const uint16_t available = audit_log_core_count(&s_ring);
    /* Newest-last window: when the caller's buffer is smaller than the ring,
     * the most recent events are the ones an investigator needs. */
    const uint16_t first = available > max_entries ? (uint16_t)(available - max_entries) : 0u;
    for (uint16_t index = first; index < available && copied < max_entries; ++index) {
        if (audit_log_core_get(&s_ring, index, &out_entries[copied])) copied++;
    }
    if (out_overwritten) *out_overwritten = audit_log_core_overwritten(&s_ring);
    if (out_last_sequence) *out_last_sequence = audit_log_core_last_sequence(&s_ring);
    portEXIT_CRITICAL(&s_lock);
    return copied;
}

/* ------------------------------------------------------------------------- */
/* Persisted configuration and control changes                                */
/* ------------------------------------------------------------------------- */

static bool changed_f(float left, float right)
{
    const float difference = left > right ? left - right : right - left;
    return !(difference <= 0.0f);
}

static bool ramp_changed(const ramp_profile_t *left, const ramp_profile_t *right)
{
    return left->enabled != right->enabled ||
           changed_f(left->up_percent_per_second, right->up_percent_per_second) ||
           changed_f(left->down_percent_per_second, right->down_percent_per_second);
}

/* Classifies what a persisted write actually did to automatic control. The
 * distinction matters during an incident investigation: "someone saved the
 * configuration page" and "someone armed automatic control" are the same HTTP
 * request, and only the second one can move an inverter. */
static void record_control_changes(const control_config_t *previous, const control_config_t *next)
{
    if (previous->enabled != next->enabled) {
        audit_log_record(AUDIT_CATEGORY_CONTROL,
                         next->enabled ? AUDIT_ACTION_CONTROL_ENABLED : AUDIT_ACTION_CONTROL_DISABLED,
                         AUDIT_OUTCOME_SUCCESS);
    }
    if (changed_f(previous->grid_import_target_kw, next->grid_import_target_kw)) {
        audit_log_record_value(AUDIT_CATEGORY_CONTROL, AUDIT_ACTION_CONTROL_SETPOINT_CHANGED,
                               AUDIT_OUTCOME_SUCCESS, next->grid_import_target_kw);
    }
    if (changed_f(previous->deadband_kw, next->deadband_kw) ||
        changed_f(previous->kp, next->kp) || changed_f(previous->ki, next->ki) ||
        previous->interval_ms != next->interval_ms ||
        previous->meter_stale_timeout_ms != next->meter_stale_timeout_ms) {
        audit_log_record(AUDIT_CATEGORY_CONTROL, AUDIT_ACTION_CONTROL_TUNING_CHANGED,
                         AUDIT_OUTCOME_SUCCESS);
    }
    if (ramp_changed(&previous->grid_ramp, &next->grid_ramp) ||
        ramp_changed(&previous->generator_ramp, &next->generator_ramp) ||
        changed_f(previous->ramp_up_percent_per_second, next->ramp_up_percent_per_second) ||
        changed_f(previous->ramp_down_percent_per_second, next->ramp_down_percent_per_second)) {
        audit_log_record(AUDIT_CATEGORY_CONTROL, AUDIT_ACTION_CONTROL_RAMP_CHANGED,
                         AUDIT_OUTCOME_SUCCESS);
    }
}

esp_err_t audit_config_manager_save(const app_config_t *config)
{
    if (!config) return config_manager_save(config);

    /* app_config_t is far too large for an HTTP handler stack. The baseline is
     * read before the write so a control change can be attributed to this
     * request rather than inferred later from a differing snapshot. */
    app_config_t *previous = malloc(sizeof(*previous));
    const bool have_previous = previous != NULL && config_manager_get_snapshot(previous) == ESP_OK;

    const esp_err_t err = config_manager_save(config);

    if (err == ESP_OK && have_previous) {
        record_control_changes(&previous->control, &config->control);
    }
    /* Without a baseline no change can be claimed, so nothing is fabricated:
     * the write itself is still recorded, and the missing comparison simply
     * means no control entry accompanies it. */
    audit_log_record(AUDIT_CATEGORY_CONFIGURATION, AUDIT_ACTION_CONFIGURATION_PERSISTED,
                     err == ESP_OK ? AUDIT_OUTCOME_SUCCESS : AUDIT_OUTCOME_FAILURE);

    if (previous) {
        memset(previous, 0, sizeof(*previous));  /* the snapshot holds Wi-Fi PSKs */
        free(previous);
    }
    return err;
}

esp_err_t audit_solar_grid_config_save(const solar_grid_config_t *config)
{
    if (!config) return solar_grid_config_save(config);

    solar_grid_config_t previous;
    const bool have_previous = solar_grid_config_get_snapshot(&previous) == ESP_OK;

    const esp_err_t err = solar_grid_config_save(config);

    if (err == ESP_OK && have_previous && previous.policy != config->policy) {
        /* The export/import policy is the site's control mode. It decides
         * whether PV may export at all, so a change to it belongs in the
         * control category, not merely in configuration. */
        audit_log_record_value(AUDIT_CATEGORY_CONTROL, AUDIT_ACTION_CONTROL_MODE_CHANGED,
                               AUDIT_OUTCOME_SUCCESS, (float)config->policy);
    }
    audit_log_record(AUDIT_CATEGORY_CONFIGURATION, AUDIT_ACTION_CONFIGURATION_SOURCE_MODEL_PERSISTED,
                     err == ESP_OK ? AUDIT_OUTCOME_SUCCESS : AUDIT_OUTCOME_FAILURE);
    return err;
}
