#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Existing firmware backend contracts consumed by the local screen.
 *
 * IMPORTANT:
 * - These are not new backend endpoints.
 * - The source of truth remains components/web_server/live_api.c and web_api.c.
 * - Null numeric values remain explicit through the has_* flags; the screen must
 *   never convert "unknown" into a measured zero.
 */
#define SCREEN_API_LIVE_PATH   "/api/live"
#define SCREEN_API_STATUS_PATH "/api/status"

#define SCREEN_API_LABEL_MAX   48U
#define SCREEN_API_REASON_MAX  128U
#define SCREEN_API_VERSION_MAX 40U

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

    char control_mode_label[SCREEN_API_LABEL_MAX];
    char control_inhibit_reason[SCREEN_API_REASON_MAX];
} screen_status_snapshot_t;

/* Parse the existing backend JSON payloads into screen-owned presentation data.
 * Missing optional fields remain unavailable rather than being guessed.
 */
bool screen_api_parse_live_json(const char *json, screen_live_snapshot_t *out);
bool screen_api_parse_status_json(const char *json, screen_status_snapshot_t *out);

#ifdef __cplusplus
}
#endif
