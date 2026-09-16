#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PVDG_UI_TEXT_SMALL 32U
#define PVDG_UI_TEXT_MEDIUM 64U
#define PVDG_UI_MAX_INVERTERS 32U
#define PVDG_UI_MAX_METERS 16U
#define PVDG_UI_MAX_ALARMS 8U

typedef enum {
    PVDG_UI_QUALITY_UNKNOWN = 0,
    PVDG_UI_QUALITY_GOOD,
    PVDG_UI_QUALITY_STALE,
    PVDG_UI_QUALITY_DEGRADED,
    PVDG_UI_QUALITY_UNAVAILABLE
} pvdg_ui_quality_t;

typedef enum {
    PVDG_UI_GRID_FLOW_UNKNOWN = 0,
    PVDG_UI_GRID_FLOW_IDLE,
    PVDG_UI_GRID_FLOW_IMPORT,
    PVDG_UI_GRID_FLOW_EXPORT
} pvdg_ui_grid_flow_t;

typedef enum {
    PVDG_UI_EXPORT_POLICY_UNKNOWN = 0,
    PVDG_UI_EXPORT_POLICY_ZERO_EXPORT,
    PVDG_UI_EXPORT_POLICY_EXPORT_ENABLED,
    PVDG_UI_EXPORT_POLICY_MINIMUM_IMPORT
} pvdg_ui_export_policy_kind_t;

typedef struct {
    bool available;
    double value;
    pvdg_ui_quality_t quality;
    bool has_age_ms;
    uint32_t age_ms;
} pvdg_ui_measurement_t;

typedef struct {
    bool online;
    char ssid[PVDG_UI_TEXT_MEDIUM];
    char ip[PVDG_UI_TEXT_SMALL];
    int rssi;
} pvdg_ui_network_t;

typedef struct {
    bool enabled;
    bool command_authority;
    char authority_label[PVDG_UI_TEXT_SMALL];
    char inhibit_reason[PVDG_UI_TEXT_MEDIUM];
    pvdg_ui_measurement_t requested_pv_kw;
    pvdg_ui_measurement_t applied_pv_kw;
} pvdg_ui_controller_t;

typedef struct {
    pvdg_ui_measurement_t power_kw;
    bool available;
    bool breaker_known;
    bool breaker_closed;
    char meter_name[PVDG_UI_TEXT_SMALL];
    pvdg_ui_measurement_t voltage_l1_v;
    pvdg_ui_measurement_t voltage_l2_v;
    pvdg_ui_measurement_t voltage_l3_v;
    pvdg_ui_measurement_t current_l1_a;
    pvdg_ui_measurement_t current_l2_a;
    pvdg_ui_measurement_t current_l3_a;
    pvdg_ui_measurement_t frequency_hz;
    pvdg_ui_measurement_t power_factor;
    pvdg_ui_measurement_t reactive_kvar;
    pvdg_ui_measurement_t apparent_kva;
    pvdg_ui_measurement_t imported_energy_kwh;
    pvdg_ui_measurement_t exported_energy_kwh;
} pvdg_ui_grid_t;

typedef struct {
    bool online;
    bool telemetry_valid;
    bool telemetry_stale;
    char name[PVDG_UI_TEXT_SMALL];
    pvdg_ui_measurement_t measured_power_kw;
    pvdg_ui_measurement_t readback_percent;
    char status[PVDG_UI_TEXT_SMALL];
} pvdg_ui_inverter_t;

typedef struct {
    pvdg_ui_measurement_t measured_kw;
    uint8_t configured_count;
    uint8_t online_count;
    uint8_t inverter_count;
    pvdg_ui_inverter_t inverters[PVDG_UI_MAX_INVERTERS];
} pvdg_ui_solar_t;

typedef struct {
    bool running;
    bool breaker_known;
    bool breaker_closed;
    bool evidence_configured;
    uint8_t running_count;
    pvdg_ui_measurement_t measured_kw;
    pvdg_ui_measurement_t running_rated_kw;
    pvdg_ui_measurement_t required_minimum_kw;
    pvdg_ui_measurement_t safe_pv_limit_kw;
} pvdg_ui_generator_t;

typedef struct {
    pvdg_ui_measurement_t power_kw;
    bool derived;
} pvdg_ui_load_t;

typedef struct {
    pvdg_ui_export_policy_kind_t kind;
    char label[PVDG_UI_TEXT_SMALL];
} pvdg_ui_export_policy_t;

typedef struct {
    pvdg_ui_grid_flow_t grid_flow;
    bool solar_feeding_bus;
    bool generator_feeding_bus;
    bool load_consuming;
    bool reverse_flow_permitted;
    bool zero_export_blocked;
    double grid_import_kw;
    double grid_export_kw;
} pvdg_ui_power_flow_t;

typedef struct {
    bool valid;
    pvdg_ui_network_t network;
    pvdg_ui_controller_t controller;
    pvdg_ui_grid_t grid;
    pvdg_ui_solar_t solar;
    pvdg_ui_generator_t generator;
    pvdg_ui_load_t load;
    pvdg_ui_export_policy_t export_policy;
    uint32_t alarm_flags;
    uint8_t alarm_count;
    char alarms[PVDG_UI_MAX_ALARMS][PVDG_UI_TEXT_MEDIUM];
} pvdg_ui_model_t;

void pvdg_ui_model_reset(pvdg_ui_model_t *model);
pvdg_ui_power_flow_t pvdg_ui_resolve_power_flow(const pvdg_ui_model_t *model);
const char *pvdg_ui_quality_label(pvdg_ui_quality_t quality);
const char *pvdg_ui_grid_flow_label(pvdg_ui_grid_flow_t flow);
void pvdg_ui_format_measurement(char *buffer, size_t buffer_size,
                                const pvdg_ui_measurement_t *measurement,
                                const char *unit, unsigned decimals);

#ifdef __cplusplus
}
#endif
