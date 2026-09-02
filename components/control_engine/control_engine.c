#include "control_engine.h"

#include <math.h>
#include <stdlib.h>

#include "config_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "grid_control_gate.h"
#include "inverter_manager.h"
#include "meter_manager.h"
#include "power_control_policy.h"
#include "safety_manager.h"
#include "solar_grid_config.h"
#include "source_detection.h"
#include "source_mode.h"

static const char *TAG = "control";
static control_config_t s_config;
static solar_grid_config_t s_grid_config;
static control_status_t s_status;
static bool s_runtime_forced_disabled;
static bool s_safe_zero_pending;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    /* Grid evidence remains the admission gate for the existing Solar+Grid
     * product. Generator evidence is additional: when it is not commissioned,
     * grid-only operation behaves exactly as before and generator operation
     * cannot be inferred from missing contacts. */
    bool configured;
    bool generator_configured;
    bool grid_available;
    bool grid_breaker_closed;
    bool generator_running;
    bool generator_breaker_closed;
    bool transfer_active;
    bool grid_generator_synchronized;
    uint16_t grid_available_raw;
    uint16_t grid_breaker_raw;
    uint16_t generator_running_raw;
    uint16_t generator_breaker_raw;
    uint16_t transfer_active_raw;
    uint16_t grid_generator_synchronized_raw;
    uint32_t last_attempt_ms;
    uint32_t last_update_ms;
    uint32_t success_count;
    uint32_t error_count;
    esp_err_t last_error;
} grid_evidence_runtime_t;

static grid_evidence_runtime_t s_evidence;
static meter_role_assignment_t s_roles;

_Static_assert(APP_MAX_GENERATORS == SOURCE_MAX_GENERATORS,
               "meter generator slots and source-mode generator channels must agree");

/* Converts a ramp profile into the kW/s the policy layer expects.
 *
 * A disabled ramp yields a rate large enough that the policy's rate limiter can
 * never bind, which lets the command step straight to the allowed target. It
 * does NOT bypass the policy: the export/import target, the generator limit and
 * every safety clamp are applied first and still hold. */
static float ramp_kw_per_second(const ramp_profile_t *ramp, bool upward,
                                float fleet_capacity_kw, bool fleet_valid)
{
    if (!fleet_valid) return 0.0f;
    if (!ramp->enabled) return fleet_capacity_kw;   /* full range within one second */
    const float percent = upward ? ramp->up_percent_per_second
                                 : ramp->down_percent_per_second;
    if (!isfinite(percent) || percent <= 0.0f) return 0.0f;
    return fleet_capacity_kw * percent * 0.01f;
}

static meter_role_assignment_t current_role_assignment(void)
{
    meter_role_assignment_t roles;
    portENTER_CRITICAL(&s_lock);
    roles = s_roles;
    portEXIT_CRITICAL(&s_lock);
    return roles;
}
static portMUX_TYPE s_evidence_lock = portMUX_INITIALIZER_UNLOCKED;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void runtime_control_snapshot(bool *enabled, bool *safe_zero_pending)
{
    portENTER_CRITICAL(&s_lock);
    if (enabled) *enabled = s_config.enabled && !s_runtime_forced_disabled;
    if (safe_zero_pending) *safe_zero_pending = s_safe_zero_pending;
    portEXIT_CRITICAL(&s_lock);
}

static void clear_safe_zero_pending(void)
{
    portENTER_CRITICAL(&s_lock);
    s_safe_zero_pending = false;
    portEXIT_CRITICAL(&s_lock);
}

static bool meter_sample_fresh(const meter_data_t *meter, uint32_t timestamp)
{
    if (!meter || !meter->online || meter->degraded || meter->last_update_ms == 0U ||
        !isfinite(meter->active_power_kw)) {
        return false;
    }
    uint32_t timeout = s_config.meter_stale_timeout_ms;
    if (timeout < 100U) timeout = 100U;
    return timestamp - meter->last_update_ms <= timeout;
}

static float safe_interval_seconds(uint32_t timestamp, uint32_t *previous_timestamp)
{
    float interval = (timestamp - *previous_timestamp) / 1000.0f;
    *previous_timestamp = timestamp;
    float configured = s_config.interval_ms / 1000.0f;
    if (!isfinite(configured) || configured <= 0.0f) configured = 0.5f;
    if (!isfinite(interval) || interval <= 0.0f || interval > configured * 4.0f) {
        interval = configured;
    }
    return interval;
}

static grid_evidence_runtime_t evidence_snapshot(void)
{
    grid_evidence_runtime_t snapshot;
    portENTER_CRITICAL(&s_evidence_lock);
    snapshot = s_evidence;
    portEXIT_CRITICAL(&s_evidence_lock);
    return snapshot;
}

static void evidence_store(const grid_evidence_runtime_t *next)
{
    portENTER_CRITICAL(&s_evidence_lock);
    s_evidence = *next;
    portEXIT_CRITICAL(&s_evidence_lock);
}

static bool signal_active(const solar_grid_signal_config_t *signal, uint16_t raw)
{
    return (raw & signal->mask) == (signal->active_value & signal->mask);
}

static bool same_signal_register(const solar_grid_signal_config_t *left,
                                 const solar_grid_signal_config_t *right)
{
    return left->enabled && right->enabled &&
           left->meter_index == right->meter_index &&
           left->function_code == right->function_code &&
           left->address == right->address;
}

static esp_err_t read_signal(const solar_grid_signal_config_t *signal,
                             uint16_t *raw)
{
    if (!signal || !signal->enabled || !raw) return ESP_ERR_INVALID_ARG;
    return meter_manager_read_registers(signal->meter_index,
                                        signal->function_code,
                                        signal->address,
                                        1U,
                                        raw);
}

/* Disabled means "not commissioned", not false evidence. It consumes no Modbus
 * request and contributes no read error. Every enabled contact shares the same
 * freshness transaction: if any enabled source signal cannot be read, the
 * strong source snapshot is not advanced and therefore goes stale/fail-closed. */
static esp_err_t read_optional_signal(const solar_grid_signal_config_t *signal,
                                      uint16_t *raw, bool *active)
{
    if (!signal || !raw || !active) return ESP_ERR_INVALID_ARG;
    *raw = 0U;
    *active = false;
    if (!signal->enabled) return ESP_OK;
    esp_err_t error = read_signal(signal, raw);
    if (error == ESP_OK) *active = signal_active(signal, *raw);
    return error;
}

static esp_err_t first_error(esp_err_t current, esp_err_t candidate)
{
    return current != ESP_OK ? current : candidate;
}

static void grid_evidence_task(void *argument)
{
    (void)argument;
    const solar_grid_signal_config_t available = s_grid_config.grid_available;
    const solar_grid_signal_config_t breaker = s_grid_config.grid_breaker_closed;
    const solar_grid_signal_config_t generator_running = s_grid_config.generator_running;
    const solar_grid_signal_config_t generator_breaker = s_grid_config.generator_breaker_closed;
    const solar_grid_signal_config_t transfer = s_grid_config.transfer_active;
    const solar_grid_signal_config_t synchronized = s_grid_config.grid_generator_synchronized;
    const bool shared_grid_register = same_signal_register(&available, &breaker);
    const bool shared_generator_register = same_signal_register(&generator_running,
                                                                &generator_breaker);

    while (true) {
        uint16_t available_raw = 0U;
        uint16_t breaker_raw = 0U;
        uint16_t generator_running_raw = 0U;
        uint16_t generator_breaker_raw = 0U;
        uint16_t transfer_raw = 0U;
        uint16_t synchronized_raw = 0U;
        bool available_active = false;
        bool breaker_active = false;
        bool generator_running_active = false;
        bool generator_breaker_active = false;
        bool transfer_active = false;
        bool synchronized_active = false;
        uint32_t timestamp = now_ms();

        esp_err_t error = read_optional_signal(&available, &available_raw,
                                               &available_active);
        if (shared_grid_register && error == ESP_OK) {
            breaker_raw = available_raw;
            breaker_active = signal_active(&breaker, breaker_raw);
        } else {
            error = first_error(error,
                                read_optional_signal(&breaker, &breaker_raw,
                                                     &breaker_active));
        }

        esp_err_t generator_error =
            read_optional_signal(&generator_running, &generator_running_raw,
                                 &generator_running_active);
        error = first_error(error, generator_error);
        if (shared_generator_register && generator_error == ESP_OK) {
            generator_breaker_raw = generator_running_raw;
            generator_breaker_active = signal_active(&generator_breaker,
                                                      generator_breaker_raw);
        } else {
            error = first_error(error,
                                read_optional_signal(&generator_breaker,
                                                     &generator_breaker_raw,
                                                     &generator_breaker_active));
        }
        error = first_error(error,
                            read_optional_signal(&transfer, &transfer_raw,
                                                 &transfer_active));
        error = first_error(error,
                            read_optional_signal(&synchronized, &synchronized_raw,
                                                 &synchronized_active));

        grid_evidence_runtime_t next = evidence_snapshot();
        next.last_attempt_ms = timestamp;
        if (error == ESP_OK) {
            next.grid_available_raw = available_raw;
            next.grid_breaker_raw = breaker_raw;
            next.generator_running_raw = generator_running_raw;
            next.generator_breaker_raw = generator_breaker_raw;
            next.transfer_active_raw = transfer_raw;
            next.grid_generator_synchronized_raw = synchronized_raw;
            next.grid_available = available_active;
            next.grid_breaker_closed = breaker_active;
            next.generator_running = generator_running_active;
            next.generator_breaker_closed = generator_breaker_active;
            next.transfer_active = transfer_active;
            next.grid_generator_synchronized = synchronized_active;
            next.last_update_ms = timestamp;
            next.success_count++;
            next.last_error = ESP_OK;
        } else {
            next.error_count++;
            next.last_error = error;
            if (next.error_count == 1U || next.error_count % 30U == 0U) {
                ESP_LOGW(TAG, "Source evidence read failed: %s [error %u]",
                         esp_err_to_name(error), (unsigned)next.error_count);
            }
        }
        evidence_store(&next);

        uint32_t delay_ms = s_grid_config.evidence_poll_interval_ms;
        if (delay_ms < 100U) delay_ms = 100U;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

static float oriented_grid_power(float raw_grid_kw)
{
    if (!isfinite(raw_grid_kw)) return NAN;
    return s_grid_config.meter_orientation == SOLAR_GRID_EXPORT_POSITIVE
               ? -raw_grid_kw
               : raw_grid_kw;
}

static float configured_grid_target(void)
{
    switch (s_grid_config.policy) {
    case SOLAR_GRID_POLICY_LIMITED_EXPORT:
        return -fabsf(s_grid_config.export_limit_kw);
    case SOLAR_GRID_POLICY_MINIMUM_IMPORT:
        return fmaxf(0.0f, s_grid_config.minimum_import_kw);
    case SOLAR_GRID_POLICY_ZERO_EXPORT:
    default:
        return 0.0f;
    }
}

static void control_task(void *argument)
{
    (void)argument;
    float integral_kw = 0.0f;
    float current_target_kw = 0.0f;
    uint32_t previous_ms = now_ms();
    bool previous_cycle_valid = false;
    grid_gate_memory_t gate_memory = {0};

    while (true) {
        uint32_t timestamp = now_ms();
        float interval_seconds = safe_interval_seconds(timestamp, &previous_ms);
        bool control_enabled = false;
        bool safe_zero_pending = false;
        runtime_control_snapshot(&control_enabled, &safe_zero_pending);

        /* Selected by role, not by array position. Reordering the meter list
         * must never silently change which physical instrument the control loop
         * regulates against. An absent or ambiguous grid assignment yields no
         * measurement, so the existing freshness gate fails closed. */
        meter_data_t grid = {0};
        const meter_role_assignment_t roles = current_role_assignment();
        bool have_grid = roles.valid && roles.grid_index != METER_ROLE_INDEX_NONE &&
                         meter_manager_get_data(roles.grid_index, &grid);
        float fleet_capacity_kw = inverter_manager_get_total_rated_kw();
        bool measurement_fresh = have_grid && meter_sample_fresh(&grid, timestamp);
        bool fleet_valid = isfinite(fleet_capacity_kw) && fleet_capacity_kw > 0.0f;
        float raw_grid_kw = measurement_fresh ? grid.active_power_kw : NAN;
        float measured_grid_kw = oriented_grid_power(raw_grid_kw);

        grid_evidence_runtime_t evidence = evidence_snapshot();
        bool evidence_fresh = evidence.configured && evidence.last_update_ms != 0U &&
                              timestamp - evidence.last_update_ms <=
                                  s_grid_config.evidence_stale_timeout_ms;

        /* Strong contact evidence wins when the existing grid evidence is
         * commissioned. Generator/transfer/synchronism contacts are populated
         * only when explicitly configured; otherwise they remain false exactly
         * as in the previous grid-only product. No power sign is promoted into
         * breaker or synchronism knowledge. */
        source_mode_result_t source;
        if (evidence.configured) {
            source_evidence_t source_evidence = {
                .evidence_fresh = evidence_fresh,
                .transfer_active = evidence.transfer_active,
                .grid_available = evidence.grid_available,
                .grid_breaker_closed = evidence.grid_breaker_closed,
                .generator_running = evidence.generator_configured &&
                                     evidence.generator_running,
                .generator_breaker_closed = evidence.generator_configured &&
                                            evidence.generator_breaker_closed,
                .grid_generator_synchronized = evidence.grid_generator_synchronized,
            };
            source = source_mode_evaluate(&source_evidence);
        } else {
            measured_source_t measured = MEASURED_SOURCE_UNKNOWN;
            bool measured_fresh = false;
            source_detection_status_t detection;
            if (source_detection_get_status(&detection) == ESP_OK) {
                measured_fresh = detection.configured && detection.evidence_fresh &&
                                 !detection.fail_closed && !detection.transition_pending;
                if (detection.state == SOURCE_STATE_GRID) measured = MEASURED_SOURCE_GRID;
                else if (detection.state == SOURCE_STATE_GENERATOR) measured = MEASURED_SOURCE_GENERATOR;
            }
            source = source_mode_from_measured_source(measured, measured_fresh);
            evidence_fresh = measured_fresh;
        }
        grid_gate_input_t gate_input = {
            .configured = evidence.configured,
            .evidence_fresh = evidence_fresh,
            .source_mode = source.mode,
            .source_control_allowed = source.control_allowed,
            .timestamp_ms = timestamp,
            .loss_trip_ms = s_grid_config.grid_loss_trip_ms,
            .recovery_stable_ms = s_grid_config.grid_recovery_stable_ms,
        };
        grid_gate_output_t gate = grid_control_gate_step(&gate_memory, &gate_input);

        /* Ramping is chosen by which source is carrying the plant: a generator
         * needs its rate limited, the grid does not. */
        const bool generator_carrying = source.mode == SOURCE_MODE_GENERATOR_ONLY ||
                                        source.mode == SOURCE_MODE_ISLAND ||
                                        source.mode == SOURCE_MODE_GRID_GENERATOR_SYNC;
        const ramp_profile_t ramp = generator_carrying ? s_config.generator_ramp
                                                       : s_config.grid_ramp;

        /* The legacy single-generator limit remains in force for this first
         * strong-evidence slice. It now covers island operation as well as
         * Generator Only, because in both states the machine carries the plant.
         * Multi-generator per-channel aggregation is a separate follow-up and
         * remains fail-closed until its ratings and meter evidence are wired. */
        float generator_safe_limit_kw = 0.0f;
        if (source.mode == SOURCE_MODE_GENERATOR_ONLY || source.mode == SOURCE_MODE_ISLAND) {
            const generator_limit_input_t limit_input = {
                .evidence_fresh = measurement_fresh &&
                                  (!evidence.configured || evidence.generator_configured),
                .facility_load_kw = fabsf(measured_grid_kw),
                .running_generator_rated_kw = s_grid_config.generator_rated_kw,
                .minimum_loading_percent = s_grid_config.generator_minimum_loading_percent,
                .reserve_kw = s_grid_config.generator_reserve_kw,
                .reverse_power_margin_kw = s_grid_config.generator_reverse_power_margin_kw,
            };
            generator_safe_limit_kw = source_mode_generator_safe_pv_kw(&limit_input);
        }

        power_control_input_t input = {
            .measurement_fresh = measurement_fresh && fleet_valid && gate.control_allowed,
            .source_mode = source.mode,
            .policy = (grid_policy_t)s_grid_config.policy,
            .measured_grid_kw = measured_grid_kw,
            .export_limit_kw = s_grid_config.export_limit_kw,
            .minimum_import_kw = s_grid_config.minimum_import_kw,
            .current_pv_command_kw = current_target_kw,
            .fleet_capacity_kw = fleet_capacity_kw,
            .kp = s_config.kp,
            .ki = s_config.ki,
            .deadband_kw = s_config.deadband_kw,
            .interval_seconds = interval_seconds,
            .ramp_up_kw_per_second = ramp_kw_per_second(&ramp, true, fleet_capacity_kw, fleet_valid),
            .ramp_down_kw_per_second = ramp_kw_per_second(&ramp, false, fleet_capacity_kw, fleet_valid),
            .integral_kw = integral_kw,
            .generator_safe_limit_kw = generator_safe_limit_kw,
        };

        power_control_output_t policy = {0};
        if (control_enabled) policy = power_control_step(&input);

        if (!control_enabled || !policy.valid) {
            integral_kw = 0.0f;
            policy.requested_pv_kw = 0.0f;
        } else if (!previous_cycle_valid) {
            current_target_kw = 0.0f;
        }
        if (policy.valid) integral_kw = policy.next_integral_kw;

        float requested_kw = control_enabled && policy.valid
                                 ? policy.requested_pv_kw
                                 : 0.0f;
        float applied_kw = safety_manager_limit_target_kw(requested_kw, &grid, timestamp);
        if (!isfinite(applied_kw) || applied_kw < 0.0f) applied_kw = 0.0f;

        uint32_t alarm_flags = safety_manager_get_alarm_flags();
        app_mode_t mode = APP_MODE_DISABLED;
        /* Only an accepted inverter write sets this, so a failed command can
         * never be reported as a successful one. */
        bool command_accepted = false;
        if (control_enabled) {
            mode = policy.valid && alarm_flags == 0U
                       ? (generator_carrying ? APP_MODE_GENERATOR : APP_MODE_GRID)
                       : APP_MODE_FAILSAFE;
            esp_err_t write_result = inverter_manager_set_total_power_kw(applied_kw);
            if (write_result != ESP_OK) {
                if (applied_kw > 0.0f) {
                    ESP_LOGW(TAG, "inverter fleet command failed: %s",
                             esp_err_to_name(write_result));
                }
                applied_kw = 0.0f;
                current_target_kw = 0.0f;
                integral_kw = 0.0f;
                mode = APP_MODE_FAILSAFE;
            } else {
                current_target_kw = applied_kw;
                command_accepted = true;
            }
        } else if (safe_zero_pending) {
            /* The HTTP/configuration path only sets this latch. The control task
             * owns the physical zero command and retains the last confirmed
             * applied value if that command cannot be confirmed. */
            esp_err_t zero_result = inverter_manager_set_total_power_kw(0.0f);
            if (zero_result == ESP_OK ||
                (current_target_kw <= 0.0f && zero_result == ESP_ERR_INVALID_STATE)) {
                clear_safe_zero_pending();
                current_target_kw = 0.0f;
                applied_kw = 0.0f;
                command_accepted = true;
            } else {
                applied_kw = current_target_kw;
                mode = APP_MODE_FAILSAFE;
                ESP_LOGE(TAG, "Safe-zero command after control disable failed: %s",
                         esp_err_to_name(zero_result));
            }
            integral_kw = 0.0f;
        } else {
            current_target_kw = 0.0f;
            integral_kw = 0.0f;
            applied_kw = 0.0f;
        }

        previous_cycle_valid = control_enabled && policy.valid && alarm_flags == 0U;
        uint32_t evidence_age = evidence.last_update_ms != 0U
                                    ? timestamp - evidence.last_update_ms
                                    : 0U;

        control_status_t next = {
            .enabled = control_enabled,
            .mode = mode,
            .grid_power_kw = measured_grid_kw,
            .raw_grid_power_kw = raw_grid_kw,
            .grid_target_kw = configured_grid_target(),
            .error_kw = policy.valid ? policy.error_kw : NAN,
            .requested_pv_kw = requested_kw,
            .applied_pv_kw = applied_kw,
            .grid_policy = (uint8_t)s_grid_config.policy,
            .source_mode = (uint8_t)source.mode,
            .grid_gate_state = (uint8_t)gate.state,
            .grid_evidence_configured = evidence.configured,
            .grid_evidence_fresh = evidence_fresh,
            .grid_available = evidence.grid_available,
            .grid_breaker_closed = evidence.grid_breaker_closed,
            .generator_evidence_configured = evidence.generator_configured,
            .generator_running = evidence.generator_running,
            .generator_breaker_closed = evidence.generator_breaker_closed,
            .transfer_active = evidence.transfer_active,
            .grid_generator_synchronized = evidence.grid_generator_synchronized,
            .grid_recovery_stable = gate.recovery_stable,
            .grid_loss_confirmed = gate.loss_confirmed,
            .grid_evidence_age_ms = evidence_age,
            .grid_evidence_success_count = evidence.success_count,
            .grid_evidence_error_count = evidence.error_count,
            .grid_evidence_last_error = evidence.last_error,
            .grid_available_raw = evidence.grid_available_raw,
            .grid_breaker_raw = evidence.grid_breaker_raw,
            .generator_running_raw = evidence.generator_running_raw,
            .generator_breaker_raw = evidence.generator_breaker_raw,
            .transfer_active_raw = evidence.transfer_active_raw,
            .grid_generator_synchronized_raw = evidence.grid_generator_synchronized_raw,
            .alarm_flags = alarm_flags,
            .last_cycle_ms = timestamp,
            .command_authority = control_enabled && policy.valid && alarm_flags == 0U,
        };
        /* One authoritative answer, in the firmware's own words, rather than the
         * interface inferring intent from several scattered flags. Ordered most
         * specific first so the operator is told the thing they can act on. */
        const char *inhibit =
            !control_enabled          ? "Automatic control is disabled; engineering authorisation is required."
            : alarm_flags != 0U       ? "An active safety alarm is blocking commands."
            : !roles.valid            ? "No single enabled meter is assigned the grid role."
            : !measurement_fresh      ? "The grid measurement is missing, stale or non-finite."
            : !fleet_valid            ? "No commissioned inverter capacity is available to command."
            : !gate.control_allowed   ? "The grid-evidence gate has not confirmed a stable source."
            : !source.control_allowed ? "The source carrying the plant is not settled."
            : !policy.valid           ? "The control policy produced no valid command this cycle."
                                      : "";
        strlcpy(next.inhibit_reason, inhibit, sizeof(next.inhibit_reason));

        portENTER_CRITICAL(&s_lock);
        if (next.command_authority != s_status.command_authority) {
            next.last_authority_change_ms = timestamp;
        } else {
            next.last_authority_change_ms = s_status.last_authority_change_ms;
        }
        /* Only an accepted write advances this; a failed command must not look
         * like a successful one. */
        next.last_command_ms = command_accepted ? timestamp : s_status.last_command_ms;
        s_status = next;
        portEXIT_CRITICAL(&s_lock);

        uint32_t delay_ms = s_config.interval_ms;
        if (delay_ms < 100U) delay_ms = 100U;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

esp_err_t control_engine_init(void)
{
    app_config_t *config = malloc(sizeof(*config));
    if (!config) return ESP_ERR_NO_MEM;
    esp_err_t error = config_manager_get_snapshot(config);
    if (error != ESP_OK) {
        free(config);
        ESP_LOGE(TAG, "configuration unavailable: %s", esp_err_to_name(error));
        return error;
    }
    s_config = config->control;
    /* Resolved once here rather than every cycle: meter configuration changes
     * already require a restart, and an app_config_t must not go on this task's
     * 4 kB stack. */
    const meter_role_assignment_t roles = config_manager_role_assignment(config);
    portENTER_CRITICAL(&s_lock);
    s_roles = roles;
    portEXIT_CRITICAL(&s_lock);
    free(config);

    if (!roles.valid) {
        ESP_LOGW(TAG, "Automatic Solar-Grid control remains fail-closed: %s",
                 roles.grid_count == 0U
                     ? "no enabled meter is assigned the grid role"
                     : roles.grid_count > 1U
                           ? "more than one enabled meter is assigned the grid role"
                           : "two meters claim the same generator slot");
    }

    error = solar_grid_config_get_snapshot(&s_grid_config);
    if (error != ESP_OK || !solar_grid_config_valid(&s_grid_config)) {
        ESP_LOGE(TAG, "Solar-Grid configuration unavailable or invalid");
        return error == ESP_OK ? ESP_ERR_INVALID_STATE : error;
    }

    grid_evidence_runtime_t evidence = {
        .configured = solar_grid_config_evidence_complete(&s_grid_config),
        .generator_configured =
            solar_grid_config_generator_evidence_complete(&s_grid_config),
        .last_error = ESP_OK,
    };
    evidence_store(&evidence);

    portENTER_CRITICAL(&s_lock);
    s_runtime_forced_disabled = false;
    s_safe_zero_pending = false;
    s_status = (control_status_t){
        .enabled = s_config.enabled,
        .mode = s_config.enabled ? APP_MODE_FAILSAFE : APP_MODE_DISABLED,
        .grid_power_kw = NAN,
        .raw_grid_power_kw = NAN,
        .grid_target_kw = configured_grid_target(),
        .error_kw = NAN,
        .requested_pv_kw = 0.0f,
        .applied_pv_kw = 0.0f,
        .grid_policy = (uint8_t)s_grid_config.policy,
        .source_mode = SOURCE_MODE_UNKNOWN,
        .grid_gate_state = evidence.configured ? GRID_GATE_WAITING_EVIDENCE
                                               : GRID_GATE_UNCONFIGURED,
        .grid_evidence_configured = evidence.configured,
        .generator_evidence_configured = evidence.generator_configured,
    };
    portEXIT_CRITICAL(&s_lock);

    if (evidence.configured &&
        xTaskCreate(grid_evidence_task, "grid_evidence", 4096, NULL, 9, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(control_task, "pvdg_control", 4096, NULL, 10, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    if (!evidence.configured) {
        ESP_LOGW(TAG, "Automatic Solar-Grid control remains fail-closed: explicit grid availability and breaker evidence are not configured");
    } else if (!evidence.generator_configured) {
        ESP_LOGI(TAG, "Solar-Grid policy '%s' loaded with grid evidence; generator run/breaker evidence is not commissioned, so strong generator operation remains unavailable",
                 solar_grid_policy_name(s_grid_config.policy));
    } else {
        ESP_LOGI(TAG, "Solar-Grid policy '%s' loaded with explicit grid and generator source evidence",
                 solar_grid_policy_name(s_grid_config.policy));
    }
    return ESP_OK;
}

void control_engine_get_status(control_status_t *out_status)
{
    if (!out_status) return;
    portENTER_CRITICAL(&s_lock);
    *out_status = s_status;
    portEXIT_CRITICAL(&s_lock);
}

void control_engine_force_disable(void)
{
    portENTER_CRITICAL(&s_lock);
    if (s_status.enabled || s_status.applied_pv_kw > 0.0f) {
        s_safe_zero_pending = true;
    }
    s_runtime_forced_disabled = true;
    s_status.enabled = false;
    s_status.mode = APP_MODE_DISABLED;
    s_status.requested_pv_kw = 0.0f;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGW(TAG, "Runtime control disable latched; control task will confirm safe zero before clearing the pending state");
}
