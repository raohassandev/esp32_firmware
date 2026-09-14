#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Existing firmware backend contracts consumed by the local screen.
 *
 * IMPORTANT:
 * - These are not new backend endpoints.
 * - The source of truth remains the components/web_server C sources.
 * - Null numeric values remain explicit through has_* flags; the screen must
 *   never convert "unknown" into a measured zero.
 * - The local HMI is read-only in the current milestone.
 */
#define SCREEN_API_LIVE_PATH      "/api/live"
#define SCREEN_API_STATUS_PATH    "/api/status"
#define SCREEN_API_METERS_PATH    "/api/meters"
#define SCREEN_API_INVERTERS_PATH "/api/inverters"
#define SCREEN_API_TELEMETRY_PATH "/api/telemetry"
#define SCREEN_API_EVENTS_PATH    "/api/operator/events"
#define SCREEN_API_ALARMS_PATH    "/api/operator/alarms"

#define SCREEN_API_LABEL_MAX        48U
#define SCREEN_API_REASON_MAX      128U
#define SCREEN_API_VERSION_MAX      40U
#define SCREEN_API_NAME_MAX         48U
#define SCREEN_API_TITLE_MAX        72U
#define SCREEN_API_ALARM_NAME_MAX   64U
#define SCREEN_API_EVENT_TEXT_MAX  112U
#define SCREEN_API_MAX_METERS       16U
#define SCREEN_API_MAX_INVERTERS    32U
#define SCREEN_API_MAX_ALARMS       16U
#define SCREEN_API_MAX_EVENTS       16U
#define SCREEN_API_MAX_ALARM_NAMES   8U

typedef struct {
    bool valid;

    bool has_grid_kw;
    double grid_kw;
    bool has_solar_kw;
    double solar_kw;
    bool has_requested_pv_kw;
    double requested_pv_kw;
    bool has_applied_pv_kw;
    double applied_pv_kw;
    bool has_commandable_kw;
    double commandable_kw;

    bool control_enabled;
    char mode_label[SCREEN_API_LABEL_MAX];
    char inhibit_reason[SCREEN_API_REASON_MAX];
    /* Informational/raw source state only. Operator attribution MUST use
     * status.source.attributed_to because that field is fail-closed. */
    char source[SCREEN_API_LABEL_MAX];
    bool meter_online;

    bool has_command_percent;
    double command_percent;
    bool command_in_force;
    char command_blocked_by[SCREEN_API_REASON_MAX];
} screen_live_snapshot_t;

typedef struct {
    bool valid;

    bool network_online;
    int rssi;
    char firmware_version[SCREEN_API_VERSION_MAX];
    char source_attributed_to[SCREEN_API_LABEL_MAX];

    uint64_t controller_uptime_ms;
    char controller_state[SCREEN_API_LABEL_MAX];
    bool last_reboot_unexpected;

    bool meter_online;
    bool meter_has_data;
    bool meter_stale;
    uint32_t alarms;
    size_t alarm_name_count;
    char alarm_names[SCREEN_API_MAX_ALARM_NAMES][SCREEN_API_ALARM_NAME_MAX];

    char control_mode_label[SCREEN_API_LABEL_MAX];
    char control_inhibit_reason[SCREEN_API_REASON_MAX];
} screen_status_snapshot_t;

typedef struct {
    uint8_t index;
    bool enabled;
    char name[SCREEN_API_NAME_MAX];
    char role_name[SCREEN_API_LABEL_MAX];
    char state[SCREEN_API_LABEL_MAX];
    bool online;
    bool stale;
    bool has_power_kw;
    double power_kw;
    bool has_data_age_ms;
    uint32_t data_age_ms;
} screen_meter_row_t;

typedef struct {
    bool valid;
    uint32_t configured_count;
    uint32_t enabled_count;
    uint32_t online_count;
    uint32_t stale_or_unavailable_count;
    uint32_t initialization_failed_count;
    size_t row_count;
    bool truncated;
    screen_meter_row_t rows[SCREEN_API_MAX_METERS];
} screen_meters_snapshot_t;

typedef struct {
    uint8_t index;
    bool enabled;
    char name[SCREEN_API_NAME_MAX];
    char state[SCREEN_API_LABEL_MAX];
    bool telemetry_supported;
    bool has_measured_power_kw;
    double measured_power_kw;
    bool has_measured_age_ms;
    uint32_t measured_age_ms;
    bool has_commanded_percent;
    double commanded_percent;
} screen_inverter_row_t;

typedef struct {
    bool valid;
    uint32_t configured_count;
    uint32_t enabled_count;
    uint32_t online_count;
    uint32_t initialization_failed_count;
    bool has_configured_rated_kw;
    double configured_rated_kw;
    bool has_enabled_rated_kw;
    double enabled_rated_kw;
    bool has_commandable_rated_kw;
    double commandable_rated_kw;
    size_t row_count;
    bool truncated;
    screen_inverter_row_t rows[SCREEN_API_MAX_INVERTERS];
} screen_inverters_snapshot_t;

typedef struct {
    bool valid;
    bool network_online;
    int rssi;
    bool monitoring_ready;
    bool command_path_ready;
    bool automatic_control_active;
    uint32_t meters_configured;
    uint32_t meters_enabled;
    uint32_t meters_online;
    uint32_t meters_initialization_failed;
    uint32_t inverters_configured;
    uint32_t inverters_enabled;
    uint32_t inverters_initialization_failed;
    bool has_grid_power_kw;
    double grid_power_kw;
    char grid_state[SCREEN_API_LABEL_MAX];
} screen_telemetry_snapshot_t;

/* Compatibility-shaped read-only projection of current Core runtime command
 * authority. The historical commissioning_gate endpoint no longer exists. The
 * product build never infers production qualification from this runtime view. */
typedef struct {
    bool valid;
    bool commissioned;
    char scope[SCREEN_API_LABEL_MAX];
    bool production_qualified;
    bool automatic_control_permitted;
    bool command_authority;
    uint32_t prerequisite_count;
    uint32_t satisfied_count;
    uint32_t unmet_count;
    char first_unmet[SCREEN_API_LABEL_MAX];
    char first_unmet_title[SCREEN_API_TITLE_MAX];
    char first_unmet_detail[SCREEN_API_REASON_MAX];
    char summary[SCREEN_API_REASON_MAX];
    char inhibit_reason[SCREEN_API_REASON_MAX];
} screen_commissioning_snapshot_t;

typedef struct {
    uint32_t sequence;
    uint32_t age_ms;
    bool active;
    char severity[SCREEN_API_LABEL_MAX];
    char kind[SCREEN_API_LABEL_MAX];
    char state[SCREEN_API_LABEL_MAX];
    char title[SCREEN_API_EVENT_TEXT_MAX];
    char detail[SCREEN_API_EVENT_TEXT_MAX];
    char recommended_action[SCREEN_API_EVENT_TEXT_MAX];
} screen_event_row_t;

typedef struct {
    bool valid;
    uint32_t active_critical;
    uint32_t active_warning;
    uint32_t stored_events;
    size_t row_count;
    bool truncated;
    screen_event_row_t rows[SCREEN_API_MAX_EVENTS];
} screen_events_snapshot_t;

typedef struct {
    uint32_t code;
    bool present;
    bool acknowledged;
    bool stale;
    bool shelved;
    bool suppressed_by_design;
    bool out_of_service;
    char id[SCREEN_API_LABEL_MAX];
    char title[SCREEN_API_EVENT_TEXT_MAX];
    char severity[SCREEN_API_LABEL_MAX];
    char priority[SCREEN_API_LABEL_MAX];
    char state[SCREEN_API_LABEL_MAX];
    char role[SCREEN_API_LABEL_MAX];
    char caused_by[SCREEN_API_LABEL_MAX];
    char recommended_action[SCREEN_API_EVENT_TEXT_MAX];
} screen_alarm_row_t;

typedef struct {
    bool valid;
    uint32_t active_count;
    uint32_t unacknowledged_count;
    uint32_t primary_active_count;
    uint32_t consequential_active_count;
    size_t row_count;
    bool truncated;
    screen_alarm_row_t rows[SCREEN_API_MAX_ALARMS];
} screen_alarms_snapshot_t;

/* Parse the existing backend JSON payloads into bounded, screen-owned
 * presentation data. Missing optional fields remain unavailable rather than
 * being guessed. */
bool screen_api_parse_live_json(const char *json, screen_live_snapshot_t *out);
bool screen_api_parse_status_json(const char *json, screen_status_snapshot_t *out);
bool screen_api_parse_meters_json(const char *json, screen_meters_snapshot_t *out);
bool screen_api_parse_inverters_json(const char *json, screen_inverters_snapshot_t *out);
bool screen_api_parse_telemetry_json(const char *json, screen_telemetry_snapshot_t *out);
bool screen_api_parse_events_json(const char *json, screen_events_snapshot_t *out);
bool screen_api_parse_alarms_json(const char *json, screen_alarms_snapshot_t *out);

#ifdef __cplusplus
}
#endif
