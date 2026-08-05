#include "source_detection.h"

#include <math.h>
#include <string.h>

#include "config_manager.h"
#include "config_types.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "meter_manager.h"
#include "meter_types.h"

/* app_config_t is ~2.5 kB. Two of these on a task stack is what caused this
 * project's original boot loop, so the snapshot lives in static storage and is
 * only ever touched by source_detection_task. The stack then only has to carry
 * the status struct and the small evidence/result values. */
#define SOURCE_DETECTION_TASK_STACK_BYTES 4096U
#define SOURCE_DETECTION_TASK_PRIORITY 5U

static const char *TAG = "source_detect";
static const char *MODE_A_LIMITATION =
    "Mode A uses one clone-specific digital input and has no redundant confirmation; "
    "a stuck or miswired input cannot be detected by the controller.";

static source_detection_status_t s_status;
static source_detection_memory_t s_memory;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_task;

static bool s_single_has_sample;
static uint16_t s_single_raw_value;
static uint32_t s_single_updated_ms;
static uint32_t s_successful_reads;
static uint32_t s_failed_reads;
static esp_err_t s_last_error;

/* Owned exclusively by source_detection_task; never read from another task. */
static app_config_t s_app_config;
static source_detection_config_t s_config;
/* Only the state and reason of the previous evaluation are needed, to decide
 * whether the change is worth logging. Reading the whole status struct back
 * just for that put another 464 bytes on the stack. */
static source_state_t s_previous_state = SOURCE_STATE_UNKNOWN;
static source_reason_t s_previous_reason = SOURCE_REASON_NOT_CONFIGURED;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

const char *source_detection_mode_name(source_detection_mode_t mode)
{
    switch (mode) {
    case SOURCE_DETECTION_MODE_SINGLE_INPUT: return "single_input";
    case SOURCE_DETECTION_MODE_DUAL_METER: return "dual_meter";
    case SOURCE_DETECTION_MODE_DISABLED:
    default: return "disabled";
    }
}

const char *source_detection_reason_message(source_reason_t reason)
{
    switch (reason) {
    case SOURCE_REASON_NONE:
        return "Source detection is resolved; automatic control remains disabled until all commissioning prerequisites are satisfied.";
    case SOURCE_REASON_NOT_CONFIGURED:
        return "Automatic Solar-Grid control remains fail-closed: EM500 source detection is not configured.";
    case SOURCE_REASON_INVALID_CONFIG:
        return "Automatic Solar-Grid control remains fail-closed: EM500 source-detection configuration is invalid.";
    case SOURCE_REASON_EVIDENCE_UNAVAILABLE:
        return "Automatic Solar-Grid control remains fail-closed: EM500 source evidence is unavailable.";
    case SOURCE_REASON_EVIDENCE_STALE:
        return "Automatic Solar-Grid control remains fail-closed: EM500 source evidence is stale.";
    case SOURCE_REASON_NON_FINITE:
        return "Automatic Solar-Grid control remains fail-closed: an EM500 source power reading is non-finite.";
    case SOURCE_REASON_UNKNOWN_INPUT_VALUE:
        return "Automatic Solar-Grid control remains fail-closed: the EM500 source input does not match the configured grid or generator value.";
    case SOURCE_REASON_CONFLICT:
        return "Automatic Solar-Grid control remains fail-closed: grid and generator meters are both above their configured thresholds.";
    case SOURCE_REASON_NO_SOURCE:
        return "Automatic Solar-Grid control remains fail-closed: neither grid nor generator meter is above its configured threshold.";
    case SOURCE_REASON_DEBOUNCE_PENDING:
        return "Automatic Solar-Grid control remains fail-closed: the source transition has not yet satisfied the configured debounce interval.";
    default:
        return "Automatic Solar-Grid control remains fail-closed: the source state is unknown.";
    }
}

static void store_status(const source_detection_status_t *status)
{
    portENTER_CRITICAL(&s_status_lock);
    s_status = *status;
    portEXIT_CRITICAL(&s_status_lock);
}

esp_err_t source_detection_get_status(source_detection_status_t *out_status)
{
    if (!out_status) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_status_lock);
    *out_status = s_status;
    portEXIT_CRITICAL(&s_status_lock);
    return ESP_OK;
}

static bool meter_configured(const app_config_t *app_config, uint8_t index)
{
    return app_config && index < app_config->meter_count && index < APP_MAX_METERS &&
           app_config->meters[index].enabled;
}

/*
 * THE TOPOLOGY IS COUNTED, NOT CHOSEN.
 *
 * It used to be a radio button in the commissioning wizard, saved separately
 * from the meters themselves, and the two could disagree. They did: a plant with
 * one meter was switched to the two-meter topology, the save was refused as
 * incomplete, the refusal appeared in a status line nobody reads, and the
 * controller went on deciding from a tariff input while the screen implied
 * otherwise.
 *
 * The owner's rule, and it is the right one: "1 hi hai to tariff, otherwise
 * depend on role". One supply meter cannot be compared against anything, so its
 * tariff input is the only evidence there is. Two or more can be compared, and
 * then the question is which is which -- which is exactly what the commissioned
 * ROLE already says.
 *
 * Counting removes the disagreement rather than adding a check for it. There is
 * nothing left to get wrong: commission the meters, and the method follows.
 *
 * An explicit DISABLED is still honoured. That is a decision not to detect at
 * all, which is different from not having said how.
 */
static uint8_t supply_meter_count(const app_config_t *app_config)
{
    if (!app_config) return 0U;
    uint8_t count = 0U;
    for (uint8_t i = 0U; i < app_config->meter_count && i < APP_MAX_METERS; ++i) {
        if (!app_config->meters[i].enabled) continue;
        const uint8_t role = app_config->meters[i].role;
        if (role == METER_ROLE_GRID || role == METER_ROLE_GENERATOR) count++;
    }
    return count;
}

static source_detection_mode_t effective_mode(const source_detection_config_t *config,
                                              const app_config_t *app_config)
{
    if (!config) return SOURCE_DETECTION_MODE_DISABLED;
    if (config->mode == SOURCE_DETECTION_MODE_DISABLED) return SOURCE_DETECTION_MODE_DISABLED;
    /* No roles assigned yet is not a topology. The stored mode stands so a
     * part-commissioned plant behaves as it did rather than changing method
     * halfway through being set up. */
    const uint8_t supplies = supply_meter_count(app_config);
    if (supplies == 0U) return (source_detection_mode_t)config->mode;
    return supplies == 1U ? SOURCE_DETECTION_MODE_SINGLE_INPUT
                          : SOURCE_DETECTION_MODE_DUAL_METER;
}

/*
 * WHICH METER IS THE GRID, AND WHICH IS THE GENERATOR.
 *
 * There were two answers to this and nothing kept them in step. The commissioned
 * ROLE lives on the meter itself and is what the engineer sets, what the meter
 * pages route by, and what the control loop selects the grid measurement with.
 * This module read a SEPARATE pair of indices stored in its own configuration,
 * which the commissioning wizard never wrote -- it saves the topology mode and
 * nothing else.
 *
 * Observed on the bench: a plant commissioned as two meters with its one meter
 * set to the generator role. The role was stored correctly, the meter page
 * showed it as the generator, and the plant overview went on reporting GRID with
 * the generator's 391 kW under it -- because this module was still in
 * single-input mode and, had it not been, would have looked up meter index 255
 * for both supplies and resolved nothing.
 *
 * The role wins. It is the answer an engineer actually gave, it is already the
 * answer every other subsystem uses, and deriving from it here removes the
 * second copy rather than adding a third writer to keep it synchronised. The
 * stored indices remain as the fallback for a configuration that set them
 * explicitly and has no roles assigned.
 */
static void resolve_dual_meters(const source_detection_config_t *config,
                                const app_config_t *app_config,
                                uint8_t *grid_index, uint8_t *generator_index)
{
    *grid_index = config ? config->dual.grid_meter_index : METER_ROLE_INDEX_NONE;
    *generator_index = config ? config->dual.generator_meter_index : METER_ROLE_INDEX_NONE;
    if (!app_config) return;

    const meter_role_assignment_t roles = config_manager_role_assignment(app_config);
    /* An invalid assignment -- duplicate generators, nothing assigned -- is not
     * an instruction to guess. The stored indices stand and the freshness gates
     * below decide, which is the same fail-closed path as an absent meter. */
    if (!roles.valid) return;
    if (roles.grid_index != METER_ROLE_INDEX_NONE) *grid_index = roles.grid_index;
    if (roles.generator_count > 0U) *generator_index = roles.generator_index[0];
}

static uint32_t evaluation_period_ms(const source_detection_config_t *config,
                                     const app_config_t *app_config)
{
    if (!config || !app_config) return 0U;
    const source_detection_mode_t mode = effective_mode(config, app_config);
    if (mode == SOURCE_DETECTION_MODE_SINGLE_INPUT &&
        meter_configured(app_config, config->single.meter_index)) {
        return app_config->meters[config->single.meter_index].poll_interval_ms;
    }
    if (mode == SOURCE_DETECTION_MODE_DUAL_METER) {
        uint8_t grid_index = METER_ROLE_INDEX_NONE;
        uint8_t generator_index = METER_ROLE_INDEX_NONE;
        resolve_dual_meters(config, app_config, &grid_index, &generator_index);
        if (meter_configured(app_config, grid_index) &&
            meter_configured(app_config, generator_index)) {
            const uint32_t grid_period = app_config->meters[grid_index].poll_interval_ms;
            const uint32_t generator_period =
                app_config->meters[generator_index].poll_interval_ms;
            return grid_period < generator_period ? grid_period : generator_period;
        }
    }
    return 0U;
}

static void clear_single_runtime(void)
{
    s_single_has_sample = false;
    s_single_raw_value = 0U;
    s_single_updated_ms = 0U;
    s_successful_reads = 0U;
    s_failed_reads = 0U;
    s_last_error = ESP_OK;
}

static void acquire_single_input(const source_detection_config_t *config,
                                 uint32_t timestamp)
{
    if (!config) return;
    const uint16_t pdu_address =
        (uint16_t)(config->single.register_address - config->single.address_base);
    uint16_t raw = 0U;
    const esp_err_t error = meter_manager_read_registers(
        config->single.meter_index,
        config->single.function_code,
        pdu_address,
        1U,
        &raw);
    s_last_error = error;
    if (error == ESP_OK) {
        s_single_has_sample = true;
        s_single_raw_value = raw;
        s_single_updated_ms = timestamp;
        s_successful_reads++;
    } else {
        s_failed_reads++;
    }
}

static source_detection_evidence_t collect_evidence(
    const source_detection_config_t *config,
    const app_config_t *app_config,
    uint32_t timestamp)
{
    source_detection_evidence_t evidence = {0};
    if (!config || !app_config) return evidence;

    /* Counted, not stored -- see effective_mode(). Gathering evidence for the
     * stored mode while the engine judged by the counted one would put the
     * decision and its evidence on different topologies. */
    const source_detection_mode_t mode = effective_mode(config, app_config);
    if (mode == SOURCE_DETECTION_MODE_SINGLE_INPUT) {
        if (meter_configured(app_config, config->single.meter_index)) {
            acquire_single_input(config, timestamp);
        }
        evidence.single_has_sample = s_single_has_sample;
        evidence.single_raw_value = s_single_raw_value;
        /* Keyed off whether a sample exists, not off the update timestamp being
         * non-zero: a read landing in the first millisecond after boot would
         * otherwise pin the age at zero permanently and defeat the stale check. */
        evidence.single_age_ms = s_single_has_sample
                                     ? timestamp - s_single_updated_ms
                                     : 0U;
        return evidence;
    }

    if (mode == SOURCE_DETECTION_MODE_DUAL_METER) {
        meter_data_t grid = {0};
        meter_data_t generator = {0};
        uint8_t grid_index = METER_ROLE_INDEX_NONE;
        uint8_t generator_index = METER_ROLE_INDEX_NONE;
        resolve_dual_meters(config, app_config, &grid_index, &generator_index);
        const bool have_grid = meter_configured(app_config, grid_index) &&
                               meter_manager_get_data(grid_index, &grid);
        const bool have_generator =
            meter_configured(app_config, generator_index) &&
            meter_manager_get_data(generator_index, &generator);

        evidence.grid_has_sample = have_grid && grid.last_update_ms != 0U &&
                                   grid.online && !grid.degraded;
        evidence.grid_power_kw = grid.active_power_kw;
        evidence.grid_age_ms = grid.last_update_ms == 0U
                                   ? 0U
                                   : timestamp - grid.last_update_ms;
        evidence.generator_has_sample = have_generator && generator.last_update_ms != 0U &&
                                        generator.online && !generator.degraded;
        evidence.generator_power_kw = generator.active_power_kw;
        evidence.generator_age_ms = generator.last_update_ms == 0U
                                        ? 0U
                                        : timestamp - generator.last_update_ms;
    }
    return evidence;
}

static source_reason_t runtime_config_reason(
    const source_detection_config_t *config,
    const app_config_t *app_config)
{
    if (!config || !source_detection_config_valid(config)) {
        return SOURCE_REASON_INVALID_CONFIG;
    }
    const source_detection_mode_t mode = effective_mode(config, app_config);
    if (mode == SOURCE_DETECTION_MODE_DISABLED) {
        return SOURCE_REASON_NOT_CONFIGURED;
    }
    if (mode == SOURCE_DETECTION_MODE_SINGLE_INPUT) {
        return meter_configured(app_config, config->single.meter_index)
                   ? SOURCE_REASON_NONE
                   : SOURCE_REASON_INVALID_CONFIG;
    }
    uint8_t grid_index = METER_ROLE_INDEX_NONE;
    uint8_t generator_index = METER_ROLE_INDEX_NONE;
    resolve_dual_meters(config, app_config, &grid_index, &generator_index);
    return meter_configured(app_config, grid_index) &&
           meter_configured(app_config, generator_index)
               ? SOURCE_REASON_NONE
               : SOURCE_REASON_INVALID_CONFIG;
}

/* Returns the evaluation period for the next cycle, so the caller does not have
 * to take a second snapshot of the same configuration. */
static uint32_t evaluate_once(uint32_t *seen_generation)
{
    if (source_detection_config_get_snapshot(&s_config) != ESP_OK ||
        config_manager_get_snapshot(&s_app_config) != ESP_OK) {
        return 0U;
    }

    const uint32_t generation = source_detection_config_generation();
    if (generation != *seen_generation) {
        *seen_generation = generation;
        source_detection_reset(&s_memory);
        clear_single_runtime();
    }

    const uint32_t timestamp = now_ms();
    const source_reason_t config_reason = runtime_config_reason(&s_config, &s_app_config);
    source_detection_evidence_t evidence = {0};
    source_detection_result_t result = {0};
    if (config_reason == SOURCE_REASON_NONE) {
        evidence = collect_evidence(&s_config, &s_app_config, timestamp);
        /* The one place a commissioned meter model becomes an engine policy
         * input. config_reason == NONE already established that the single-input
         * meter index names an enabled meter, so the lookup is in range; the
         * bound is repeated anyway because a policy input that decides how a
         * register is READ must not depend on a caller's memory of an earlier
         * check. Anything other than a commissioned EM500 -- including a meter
         * whose model was never declared -- yields false and strict equality. */
        const bool single_meter_is_em500 =
            s_config.single.meter_index < APP_MAX_METERS &&
            meter_model_is_em500(s_app_config.meters[s_config.single.meter_index].model);
        source_detection_policy_t policy =
            source_detection_config_policy(&s_config, single_meter_is_em500);
        /* The engine branches on policy.mode, and the evidence above was
         * gathered for the COUNTED topology. Handing it the stored one instead
         * would have it judge tariff evidence by threshold rules, or the
         * reverse -- the two halves of one decision disagreeing about what kind
         * of plant this is. */
        policy.mode = effective_mode(&s_config, &s_app_config);
        result = source_detection_step(&s_memory, &policy, &evidence, timestamp);
    } else {
        source_detection_reset(&s_memory);
        result.state = SOURCE_STATE_UNKNOWN;
        result.candidate_state = SOURCE_STATE_UNKNOWN;
        result.tariff = SOURCE_TARIFF_NONE;
        result.reason = config_reason;
        result.fail_closed = true;
    }

    source_detection_status_t status = {
        .config_generation = generation,
        /* Report what the controller is actually doing, not what was
         * stored: the screen and the decision must agree. */
        .mode = effective_mode(&s_config, &s_app_config),
        .state = result.state,
        .candidate_state = result.candidate_state,
        .tariff = result.tariff,
        .reason = result.reason,
        .configured = config_reason == SOURCE_REASON_NONE,
        .evidence_fresh = result.evidence_fresh,
        .transition_pending = result.transition_pending,
        .conflict = result.conflict,
        .fail_closed = result.fail_closed,
        .detection_only = true,
        .evaluated_ms = timestamp,
        .successful_reads = s_successful_reads,
        .failed_reads = s_failed_reads,
        .last_error = s_last_error,
        .single_has_sample = evidence.single_has_sample,
        .single_raw_value = evidence.single_raw_value,
        .single_age_ms = evidence.single_age_ms,
        .grid_has_sample = evidence.grid_has_sample,
        .grid_power_kw = evidence.grid_power_kw,
        .grid_age_ms = evidence.grid_age_ms,
        .generator_has_sample = evidence.generator_has_sample,
        .generator_power_kw = evidence.generator_power_kw,
        .generator_age_ms = evidence.generator_age_ms,
    };
    strlcpy(status.reason_text, source_detection_reason_message(status.reason),
            sizeof(status.reason_text));
    if (status.mode == SOURCE_DETECTION_MODE_SINGLE_INPUT) {
        strlcpy(status.limitation_text, MODE_A_LIMITATION,
                sizeof(status.limitation_text));
    }
    store_status(&status);

    if (s_previous_state != status.state || s_previous_reason != status.reason) {
        s_previous_state = status.state;
        s_previous_reason = status.reason;
        if (status.state == SOURCE_STATE_UNKNOWN) {
            ESP_LOGW(TAG, "%s", status.reason_text);
        } else {
            ESP_LOGI(TAG, "EM500 source resolved as %s; reporting tariff %u (detection only)",
                     source_detection_state_name(status.state), (unsigned)status.tariff);
        }
    }

    return evaluation_period_ms(&s_config, &s_app_config);
}

static void source_detection_task(void *argument)
{
    (void)argument;
    uint32_t seen_generation = 0U;
    bool reported_headroom = false;
    for (;;) {
        const uint32_t period_ms = evaluate_once(&seen_generation);

        /* Reported once, after the first full evaluation has exercised the
         * deepest path, so the margin is a measured fact rather than an
         * assumption. */
        if (!reported_headroom) {
            reported_headroom = true;
            ESP_LOGI(TAG, "Source detection stack headroom: %u bytes of %u",
                     (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)),
                     (unsigned)SOURCE_DETECTION_TASK_STACK_BYTES);
        }

        if (period_ms == 0U) {
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        } else {
            (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(period_ms));
        }
    }
}

esp_err_t source_detection_init(void)
{
    if (s_task) return ESP_OK;
    source_detection_status_t initial = {
        .mode = SOURCE_DETECTION_MODE_DISABLED,
        .state = SOURCE_STATE_UNKNOWN,
        .candidate_state = SOURCE_STATE_UNKNOWN,
        .tariff = SOURCE_TARIFF_NONE,
        .reason = SOURCE_REASON_NOT_CONFIGURED,
        .fail_closed = true,
        .detection_only = true,
    };
    strlcpy(initial.reason_text, source_detection_reason_message(initial.reason),
            sizeof(initial.reason_text));
    store_status(&initial);

    if (xTaskCreate(source_detection_task, "source_detect",
                    SOURCE_DETECTION_TASK_STACK_BYTES, NULL,
                    SOURCE_DETECTION_TASK_PRIORITY, &s_task) != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void source_detection_notify_config_changed(void)
{
    if (s_task) xTaskNotifyGive(s_task);
}
