#include "control_engine.h"

#include <math.h>
#include <stdlib.h>

#include "config_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "inverter_manager.h"
#include "meter_manager.h"
#include "power_control_policy.h"
#include "safety_manager.h"

static const char *TAG = "control";
static control_config_t s_config;
static control_status_t s_status;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
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

static void control_task(void *argument)
{
    (void)argument;
    float integral_kw = 0.0f;
    float current_target_kw = 0.0f;
    uint32_t previous_ms = now_ms();
    bool previous_cycle_valid = false;

    while (true) {
        uint32_t timestamp = now_ms();
        float interval_seconds = safe_interval_seconds(timestamp, &previous_ms);

        meter_data_t grid = {0};
        bool have_grid = meter_manager_get_data(0, &grid);
        float fleet_capacity_kw = inverter_manager_get_total_rated_kw();
        bool measurement_fresh = have_grid && meter_sample_fresh(&grid, timestamp);
        bool fleet_valid = isfinite(fleet_capacity_kw) && fleet_capacity_kw > 0.0f;

        /* The persisted control schema currently defines a grid-import target
         * only. Therefore the live controller explicitly operates in minimum
         * grid-import mode. Generator and transfer modes remain blocked until
         * real run, breaker, ATS, synchronization and generator-meter evidence
         * is configured; they are never inferred from the grid power value. */
        power_control_input_t input = {
            .measurement_fresh = measurement_fresh && fleet_valid,
            .source_mode = SOURCE_MODE_GRID_ONLY,
            .policy = GRID_POLICY_MINIMUM_IMPORT,
            .measured_grid_kw = grid.active_power_kw,
            .export_limit_kw = 0.0f,
            .minimum_import_kw = s_config.grid_import_target_kw,
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
        if (s_config.enabled) policy = power_control_step(&input);

        if (!s_config.enabled || !policy.valid || !previous_cycle_valid) {
            if (!policy.valid || !s_config.enabled) {
                integral_kw = 0.0f;
                policy.requested_pv_kw = 0.0f;
            }
        }
        if (policy.valid) integral_kw = policy.next_integral_kw;

        float requested_kw = s_config.enabled && policy.valid
                                 ? policy.requested_pv_kw
                                 : 0.0f;
        float applied_kw = safety_manager_limit_target_kw(requested_kw, &grid, timestamp);
        if (!isfinite(applied_kw) || applied_kw < 0.0f) applied_kw = 0.0f;

        uint32_t alarm_flags = safety_manager_get_alarm_flags();
        app_mode_t mode = APP_MODE_DISABLED;
        if (s_config.enabled) {
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
        } else {
            current_target_kw = 0.0f;
            integral_kw = 0.0f;
            applied_kw = 0.0f;
        }

        previous_cycle_valid = s_config.enabled && policy.valid && alarm_flags == 0U;

        control_status_t next = {
            .enabled = s_config.enabled,
            .mode = mode,
            .grid_power_kw = measurement_fresh ? grid.active_power_kw : NAN,
            .grid_target_kw = s_config.grid_import_target_kw,
            .error_kw = policy.valid ? policy.error_kw : NAN,
            .requested_pv_kw = requested_kw,
            .applied_pv_kw = applied_kw,
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
    esp_err_t err = config_manager_get_snapshot(config);
    if (err != ESP_OK) {
        free(config);
        ESP_LOGE(TAG, "configuration unavailable: %s", esp_err_to_name(err));
        return err;
    }
    s_config = config->control;
    free(config);

    portENTER_CRITICAL(&s_lock);
    s_status = (control_status_t){
        .enabled = s_config.enabled,
        .mode = s_config.enabled ? APP_MODE_FAILSAFE : APP_MODE_DISABLED,
        .grid_power_kw = NAN,
        .grid_target_kw = s_config.grid_import_target_kw,
        .error_kw = NAN,
        .requested_pv_kw = 0.0f,
        .applied_pv_kw = 0.0f,
    };
    portEXIT_CRITICAL(&s_lock);

    if (xTaskCreate(control_task, "pvdg_control", 4096, NULL, 10, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
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
