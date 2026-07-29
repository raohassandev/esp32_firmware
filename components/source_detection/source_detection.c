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

static uint32_t evaluation_period_ms(const source_detection_config_t *config,
                                     const app_config_t *app_config)
{
    if (!config || !app_config) return 0U;
    if (config->mode == SOURCE_DETECTION_MODE_SINGLE_INPUT &&
        meter_configured(app_config, config->single.meter_index)) {
        return app_config->meters[config->single.meter_index].poll_interval_ms;
    }
    if (config->mode == SOURCE_DETECTION_MODE_DUAL_METER &&
        meter_configured(app_config, config->dual.grid_meter_index) &&
        meter_configured(app_config, config->dual.generator_meter_index)) {
        const uint32_t grid_period =
            app_config->meters[config->dual.grid_meter_index].poll_interval_ms;
        const uint32_t generator_period =
            app_config->meters[config->dual.generator_meter_index].poll_interval_ms;
        return grid_period < generator_period ? grid_period : generator_period;
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

    if (config->mode == SOURCE_DETECTION_MODE_SINGLE_INPUT) {
        if (meter_configured(app_config, config->single.meter_index)) {
            acquire_single_input(config, timestamp);
        }
        evidence.single_has_sample = s_single_has_sample;
        evidence.single_raw_value = s_single_raw_value;
        evidence.single_age_ms = s_single_updated_ms == 0U
                                     ? 0U
                                     : timestamp - s_single_updated_ms;
        return evidence;
    }

    if (config->mode == SOURCE_DETECTION_MODE_DUAL_METER) {
        meter_data_t grid = {0};
        meter_data_t generator = {0};
        const bool have_grid = meter_configured(app_config, config->dual.grid_meter_index) &&
                               meter_manager_get_data(config->dual.grid_meter_index, &grid);
        const bool have_generator =
            meter_configured(app_config, config->dual.generator_meter_index) &&
            meter_manager_get_data(config->dual.generator_meter_index, &generator);

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
    if (config->mode == SOURCE_DETECTION_MODE_DISABLED) {
        return SOURCE_REASON_NOT_CONFIGURED;
    }
    if (config->mode == SOURCE_DETECTION_MODE_SINGLE_INPUT) {
        return meter_configured(app_config, config->single.meter_index)
                   ? SOURCE_REASON_NONE
                   : SOURCE_REASON_INVALID_CONFIG;
    }
    return meter_configured(app_config, config->dual.grid_meter_index) &&
           meter_configured(app_config, config->dual.generator_meter_index)
               ? SOURCE_REASON_NONE
               : SOURCE_REASON_INVALID_CONFIG;
}

static void evaluate_once(uint32_t *seen_generation)
{
    source_detection_config_t config;
    app_config_t app_config;
    if (source_detection_config_get_snapshot(&config) != ESP_OK ||
        config_manager_get_snapshot(&app_config) != ESP_OK) {
        return;
    }

    const uint32_t generation = source_detection_config_generation();
    if (generation != *seen_generation) {
        *seen_generation = generation;
        source_detection_reset(&s_memory);
        clear_single_runtime();
    }

    const uint32_t timestamp = now_ms();
    const source_reason_t config_reason = runtime_config_reason(&config, &app_config);
    source_detection_evidence_t evidence = {0};
    source_detection_result_t result = {0};
    if (config_reason == SOURCE_REASON_NONE) {
        evidence = collect_evidence(&config, &app_config, timestamp);
        const source_detection_policy_t policy = source_detection_config_policy(&config);
        result = source_detection_step(&s_memory, &policy, &evidence, timestamp);
    } else {
        source_detection_reset(&s_memory);
        result.state = SOURCE_STATE_UNKNOWN;
        result.candidate_state = SOURCE_STATE_UNKNOWN;
        result.tariff = SOURCE_TARIFF_NONE;
        result.reason = config_reason;
        result.fail_closed = true;
    }

    source_detection_status_t previous;
    source_detection_get_status(&previous);

    source_detection_status_t status = {
        .config_generation = generation,
        .mode = (source_detection_mode_t)config.mode,
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

    if (previous.state != status.state || previous.reason != status.reason) {
        if (status.state == SOURCE_STATE_UNKNOWN) {
            ESP_LOGW(TAG, "%s", status.reason_text);
        } else {
            ESP_LOGI(TAG, "EM500 source resolved as %s; reporting tariff %u (detection only)",
                     source_detection_state_name(status.state), (unsigned)status.tariff);
        }
    }
}

static void source_detection_task(void *argument)
{
    (void)argument;
    uint32_t seen_generation = 0U;
    for (;;) {
        evaluate_once(&seen_generation);

        source_detection_config_t config;
        app_config_t app_config;
        uint32_t period_ms = 0U;
        if (source_detection_config_get_snapshot(&config) == ESP_OK &&
            config_manager_get_snapshot(&app_config) == ESP_OK) {
            period_ms = evaluation_period_ms(&config, &app_config);
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
