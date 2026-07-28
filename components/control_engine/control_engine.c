#include "control_engine.h"
#include "esp_check.h"
#include <math.h>
#include <stdlib.h>
#include "config_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "inverter_manager.h"
#include "meter_manager.h"
#include "safety_manager.h"

static const char *TAG = "control";
static control_config_t s_config;
static control_status_t s_status;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static float clampf_safe(float value, float low, float high)
{
    if (!isfinite(value) || !isfinite(low) || !isfinite(high) || low > high) return low;
    return fmaxf(low, fminf(high, value));
}

static void control_task(void *argument)
{
    (void)argument;
    float integral = 0.0f;
    float current_target_kw = 0.0f;
    uint32_t last_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    while (true) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        float dt = (now_ms - last_ms) / 1000.0f;
        if (!isfinite(dt) || dt <= 0.0f || dt > 2.0f) dt = s_config.interval_ms / 1000.0f;
        last_ms = now_ms;

        meter_data_t grid = {0};
        meter_manager_get_data(0, &grid);
        float total_rated_kw = inverter_manager_get_total_rated_kw();
        bool measurement_valid = grid.online && grid.last_update_ms != 0U &&
                                 isfinite(grid.active_power_kw) &&
                                 isfinite(total_rated_kw) && total_rated_kw > 0.0f;

        float error_kw = 0.0f;
        float requested_kw = 0.0f;
        app_mode_t mode = APP_MODE_DISABLED;

        if (s_config.enabled && measurement_valid) {
            mode = APP_MODE_GRID;
            error_kw = grid.active_power_kw - s_config.grid_import_target_kw;
            if (!isfinite(error_kw)) {
                measurement_valid = false;
            } else {
                if (fabsf(error_kw) <= s_config.deadband_kw) error_kw = 0.0f;
                integral = clampf_safe(integral + s_config.ki * error_kw * dt,
                                       -total_rated_kw, total_rated_kw);
                requested_kw = clampf_safe(current_target_kw + s_config.kp * error_kw + integral,
                                           0.0f, total_rated_kw);
                float delta = requested_kw - current_target_kw;
                float up_step = total_rated_kw * s_config.ramp_up_percent_per_second * 0.01f * dt;
                float down_step = total_rated_kw * s_config.ramp_down_percent_per_second * 0.01f * dt;
                delta = clampf_safe(delta, -down_step, up_step);
                requested_kw = clampf_safe(current_target_kw + delta, 0.0f, total_rated_kw);
            }
        }

        if (!s_config.enabled || !measurement_valid) {
            integral = 0.0f;
            requested_kw = 0.0f;
        }

        float applied_kw = safety_manager_limit_target_kw(requested_kw, &grid, now_ms);
        if (!isfinite(applied_kw)) applied_kw = 0.0f;
        if (s_config.enabled && (!measurement_valid || safety_manager_get_alarm_flags())) {
            mode = APP_MODE_FAILSAFE;
        }

        if (s_config.enabled) {
            esp_err_t write_result = inverter_manager_set_total_power_kw(applied_kw);
            if (write_result != ESP_OK) {
                if (applied_kw > 0.0f) ESP_LOGW(TAG, "inverter write failed: %s", esp_err_to_name(write_result));
                current_target_kw = 0.0f;
                integral = 0.0f;
            } else {
                current_target_kw = applied_kw;
            }
        } else {
            applied_kw = 0.0f;
            current_target_kw = 0.0f;
        }

        control_status_t next = {
            .enabled = s_config.enabled,
            .mode = mode,
            .grid_power_kw = measurement_valid ? grid.active_power_kw : 0.0f,
            .grid_target_kw = s_config.grid_import_target_kw,
            .error_kw = measurement_valid ? error_kw : 0.0f,
            .requested_pv_kw = requested_kw,
            .applied_pv_kw = applied_kw,
            .alarm_flags = safety_manager_get_alarm_flags(),
            .last_cycle_ms = now_ms
        };
        portENTER_CRITICAL(&s_lock);
        s_status = next;
        portEXIT_CRITICAL(&s_lock);
        vTaskDelay(pdMS_TO_TICKS(s_config.interval_ms));
    }
}

esp_err_t control_engine_init(void)
{
    app_config_t *cfg = malloc(sizeof(*cfg));
    if (!cfg) return ESP_ERR_NO_MEM;
    esp_err_t err = config_manager_get_snapshot(cfg);
    if (err != ESP_OK) {
        free(cfg);
        ESP_LOGE(TAG, "configuration unavailable: %s", esp_err_to_name(err));
        return err;
    }
    s_config = cfg->control;
    free(cfg);
    if (xTaskCreate(control_task, "pvdg_control", 4096, NULL, 10, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

void control_engine_get_status(control_status_t *out_status)
{
    if (!out_status) return;
    portENTER_CRITICAL(&s_lock);
    *out_status = s_status;
    portEXIT_CRITICAL(&s_lock);
}
