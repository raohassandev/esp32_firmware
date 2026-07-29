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
#include "source_mode.h"

static const char *TAG = "control";
static control_config_t s_config;
static solar_grid_config_t s_grid_config;
static control_status_t s_status;
static bool s_runtime_forced_disabled;
static bool s_safe_zero_pending;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    bool configured;
    bool grid_available;
    bool grid_breaker_closed;
    uint16_t grid_available_raw;
    uint16_t grid_breaker_raw;
    uint32_t last_attempt_ms;
    uint32_t last_update_ms;
    uint32_t success_count;
    uint32_t error_count;
    esp_err_t last_error;
} grid_evidence_runtime_t;

static grid_evidence_runtime_t s_evidence;
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
    return left->meter_index == right->meter_index &&
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

static void grid_evidence_task(void *argument)
{
    (void)argument;
    const solar_grid_signal_config_t available = s_grid_config.grid_available;
    const solar_grid_signal_config_t breaker = s_grid_config.grid_breaker_closed;
    const bool shared_register = same_signal_register(&available, &breaker);

    while (true) {
        uint16_t available_raw = 0U;
        uint16_t breaker_raw = 0U;
        uint32_t timestamp = now_ms();
        esp_err_t available_error = read_signal(&available, &available_raw);
        esp_err_t breaker_error = ESP_OK;
        if (shared_register && available_error == ESP_OK) {
            breaker_raw = available_raw;
        } else {
            breaker_error = read_signal(&breaker, &breaker_raw);
        }

        grid_evidence_runtime_t next = evidence_snapshot();
        next.last_attempt_ms = timestamp;
        esp_err_t error = available_error != ESP_OK ? available_error : breaker_error;
        if (error == ESP_OK) {
            next.grid_available_raw = available_raw;
            next.grid_breaker_raw = breaker_raw;
            next.grid_available = signal_active(&available, available_raw);
            next.grid_breaker_closed = signal_active(&breaker, breaker_raw);
            next.last_update_ms = timestamp;
            next.success_count++;
            next.last_error = ESP_OK;
        } else {
            next.error_count++;
            next.last_error = error;
            if (next.error_count == 1U || next.error_count % 30U == 0U) {
                ESP_LOGW(TAG, "Grid evidence read failed: %s [error %u]",
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

        meter_data_t grid = {0};
        bool have_grid = meter_manager_get_data(0, &grid);
        float fleet_capacity_kw = inverter_manager_get_total_rated_kw();
        bool measurement_fresh = have_grid && meter_sample_fresh(&grid, timestamp);
        bool fleet_valid = isfinite(fleet_capacity_kw) && fleet_capacity_kw > 0.0f;
        float raw_grid_kw = measurement_fresh ? grid.active_power_kw : NAN;
        float measured_grid_kw = oriented_grid_power(raw_grid_kw);

        grid_evidence_runtime_t evidence = evidence_snapshot();
        bool evidence_fresh = evidence.configured && evidence.last_update_ms != 0U &&
                              timestamp - evidence.last_update_ms <=
                                  s_grid_config.evidence_stale_timeout_ms;
        source_evidence_t source_evidence = {
            .evidence_fresh = evidence_fresh,
            .transfer_active = false,
            .grid_available = evidence.grid_available,
            .grid_breaker_closed = evidence.grid_breaker_closed,
            .generator_running = false,
            .generator_breaker_closed = false,
            .grid_generator_synchronized = false,
        };
        source_mode_result_t source = source_mode_evaluate(&source_evidence);
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
            .ramp_up_kw_per_second = fleet_valid
                ? fleet_capacity_kw * s_config.ramp_up_percent_per_second * 0.01f
                : 0.0f,
            .ramp_down_kw_per_second = fleet_valid
                ? fleet_capacity_kw * s_config.ramp_down_percent_per_second * 0.01f
                : 0.0f,
            .integral_kw = integral_kw,
            .generator_safe_limit_kw = 0.0f,
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
        if (control_enabled) {
            mode = policy.valid && alarm_flags == 0U ? APP_MODE_GRID : APP_MODE_FAILSAFE;
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
            .grid_recovery_stable = gate.recovery_stable,
            .grid_loss_confirmed = gate.loss_confirmed,
            .grid_evidence_age_ms = evidence_age,
            .grid_evidence_success_count = evidence.success_count,
            .grid_evidence_error_count = evidence.error_count,
            .grid_evidence_last_error = evidence.last_error,
            .grid_available_raw = evidence.grid_available_raw,
            .grid_breaker_raw = evidence.grid_breaker_raw,
            .alarm_flags = alarm_flags,
            .last_cycle_ms = timestamp,
        };
        portENTER_CRITICAL(&s_lock);
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
    free(config);

    error = solar_grid_config_get_snapshot(&s_grid_config);
    if (error != ESP_OK || !solar_grid_config_valid(&s_grid_config)) {
        ESP_LOGE(TAG, "Solar-Grid configuration unavailable or invalid");
        return error == ESP_OK ? ESP_ERR_INVALID_STATE : error;
    }

    grid_evidence_runtime_t evidence = {
        .configured = solar_grid_config_evidence_complete(&s_grid_config),
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
    } else {
        ESP_LOGI(TAG, "Solar-Grid policy '%s' loaded with explicit Modbus grid evidence",
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
