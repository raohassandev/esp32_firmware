#include "operational_api.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "control_engine.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "engineering_auth.h"
#include "http_json.h"
#include "inverter_manager.h"
#include "meter_manager.h"
#include "network_manager.h"
#include "safety_manager.h"

#define FAST_SAMPLE_COUNT 180
#define MINUTE_SAMPLE_COUNT 1440
#define EVENT_COUNT 96
#define SAMPLE_INTERVAL_MS 5000U
#define MINUTE_INTERVAL_MS 60000U
#define METER_FRESH_MS 5000U

typedef struct {
    uint32_t timestamp_ms;
    float grid_kw;
    float solar_kw;
    uint16_t alarm_flags;
    uint8_t meter_online;
    uint8_t inverter_online;
    uint8_t inverter_enabled;
    uint8_t control_enabled;
} operational_sample_t;

typedef enum {
    EVENT_CONTROLLER_STARTED = 0,
    EVENT_NETWORK_STATE,
    EVENT_METER_STATE,
    EVENT_INVERTER_FLEET_STATE,
    EVENT_CONTROL_STATE,
    EVENT_METER_OFFLINE_ALARM,
    EVENT_METER_STALE_ALARM
} operational_event_code_t;

typedef struct {
    uint32_t timestamp_ms;
    uint32_t sequence;
    int32_t value;
    uint8_t code;
    uint8_t active;
    uint8_t severity;
} operational_event_t;

typedef struct {
    bool initialized;
    bool network_online;
    bool meter_online;
    bool control_enabled;
    uint8_t inverter_online;
    uint8_t inverter_enabled;
    uint32_t alarm_flags;
} observed_state_t;

static operational_sample_t s_fast[FAST_SAMPLE_COUNT];
static operational_sample_t s_minute[MINUTE_SAMPLE_COUNT];
static operational_event_t s_events[EVENT_COUNT];
static uint16_t s_fast_head;
static uint16_t s_fast_count;
static uint16_t s_minute_head;
static uint16_t s_minute_count;
static uint16_t s_event_head;
static uint16_t s_event_count;
static uint32_t s_event_sequence;
static uint32_t s_last_minute_ms;
static observed_state_t s_observed;
static TaskHandle_t s_task;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

/* Defined below, next to the code-to-text mapping they belong with. */
static bool event_is_alarm_condition(uint8_t code);
static bool event_condition_present(uint8_t code, uint8_t raw_active);

/* An alarm is a condition with a lifecycle; an event is a record that something
 * happened. The ring above is the record. This table is the condition state, one
 * row per alarm code, which is what an operator actually works from: what is
 * wrong now, since when, how often it has recurred, and whether anyone has taken
 * responsibility for it.
 *
 * Acknowledgement is tracked separately from clearing on purpose. A condition
 * that clears itself was never acknowledged by anyone, and an acknowledged
 * condition that is still present must not disappear from view. */
typedef struct {
    bool present;
    bool acknowledged;
    uint8_t severity;
    uint16_t occurrences;
    uint32_t first_raised_ms;
    uint32_t last_raised_ms;
    uint32_t cleared_ms;
    uint32_t acknowledged_ms;
} operational_alarm_t;

static operational_alarm_t s_alarms[EVENT_METER_STALE_ALARM + 1U];

/* Caller holds s_lock. */
static void update_alarm_locked(operational_event_code_t code, bool present,
                                uint8_t severity, uint32_t timestamp)
{
    if ((size_t)code >= sizeof(s_alarms) / sizeof(s_alarms[0])) return;
    operational_alarm_t *alarm = &s_alarms[code];
    alarm->severity = severity;
    if (present) {
        if (!alarm->present) {
            if (alarm->first_raised_ms == 0U) alarm->first_raised_ms = timestamp;
            alarm->last_raised_ms = timestamp;
            if (alarm->occurrences < UINT16_MAX) alarm->occurrences++;
            /* A fresh occurrence demands fresh attention: a previous
             * acknowledgement does not carry over to a condition that went away
             * and came back. */
            alarm->acknowledged = false;
            alarm->acknowledged_ms = 0U;
        }
        alarm->present = true;
        alarm->cleared_ms = 0U;
    } else if (alarm->present) {
        alarm->present = false;
        alarm->cleared_ms = timestamp;
    }
}

static void append_event(operational_event_code_t code, bool active, uint8_t severity, int32_t value, uint32_t timestamp)
{
    portENTER_CRITICAL(&s_lock);
    operational_event_t *event = &s_events[s_event_head];
    event->timestamp_ms = timestamp;
    event->sequence = ++s_event_sequence;
    event->value = value;
    event->code = (uint8_t)code;
    event->active = active ? 1U : 0U;
    event->severity = severity;
    s_event_head = (uint16_t)((s_event_head + 1U) % EVENT_COUNT);
    if (s_event_count < EVENT_COUNT) s_event_count++;
    /* Same lock, same instant: the condition table can never disagree with the
     * record that produced it. */
    if (event_is_alarm_condition((uint8_t)code)) {
        update_alarm_locked(code, event_condition_present((uint8_t)code, active ? 1U : 0U),
                            severity, timestamp);
    }
    portEXIT_CRITICAL(&s_lock);
}

static void append_sample(operational_sample_t *ring, uint16_t capacity,
                          uint16_t *head, uint16_t *count,
                          const operational_sample_t *sample)
{
    ring[*head] = *sample;
    *head = (uint16_t)((*head + 1U) % capacity);
    if (*count < capacity) (*count)++;
}

static void collect_sample(operational_sample_t *sample, observed_state_t *state)
{
    memset(sample, 0, sizeof(*sample));
    memset(state, 0, sizeof(*state));
    sample->timestamp_ms = now_ms();

    network_status_t network = {0};
    meter_data_t meter = {0};
    control_status_t control = {0};
    network_manager_get_status(&network);
    meter_manager_get_data(0, &meter);
    control_engine_get_status(&control);

    bool meter_has_data = meter.last_update_ms != 0;
    uint32_t meter_age = meter_has_data ? sample->timestamp_ms - meter.last_update_ms : UINT32_MAX;
    state->network_online = network.network_ready;
    state->meter_online = meter.online && meter_has_data && meter_age <= METER_FRESH_MS &&
                          isfinite(meter.active_power_kw);
    state->control_enabled = control.enabled;
    state->alarm_flags = safety_manager_get_alarm_flags();

    sample->grid_kw = state->meter_online ? meter.active_power_kw : NAN;
    sample->control_enabled = control.enabled ? 1U : 0U;
    sample->meter_online = state->meter_online ? 1U : 0U;
    sample->alarm_flags = (uint16_t)(state->alarm_flags & 0xFFFFU);

    float solar_kw = 0.0f;
    uint8_t inverter_online = 0;
    uint8_t inverter_enabled = 0;
    uint8_t count = inverter_manager_get_count();
    for (uint8_t i = 0; i < count; ++i) {
        inverter_data_t data = {0};
        if (!inverter_manager_get_data(i, &data)) continue;
        if (data.rated_power_kw > 0.0f) inverter_enabled++;
        if (data.online) inverter_online++;
        if (data.telemetry_valid && !data.telemetry_stale && isfinite(data.measured_power_kw)) {
            solar_kw += data.measured_power_kw;
        }
    }
    state->inverter_online = inverter_online;
    state->inverter_enabled = inverter_enabled;
    sample->solar_kw = inverter_online > 0 ? solar_kw : NAN;
    sample->inverter_online = inverter_online;
    sample->inverter_enabled = inverter_enabled;
}

static void detect_events(const observed_state_t *next, uint32_t timestamp)
{
    if (!s_observed.initialized) {
        append_event(EVENT_CONTROLLER_STARTED, true, 0, 0, timestamp);
        /* A condition already present at the first sample never produced a
         * transition, so recording only changes meant a controller that booted
         * straight into a fault reported no alarm at all - the plant was
         * offline and the alarm list was empty. Anything already wrong at
         * startup is raised here, once. */
        if (!next->network_online) {
            append_event(EVENT_NETWORK_STATE, false, 2, 0, timestamp);
        }
        if (!next->meter_online) {
            append_event(EVENT_METER_STATE, false, 2, 0, timestamp);
        }
        if ((next->alarm_flags & SAFETY_ALARM_METER_OFFLINE) != 0U) {
            append_event(EVENT_METER_OFFLINE_ALARM, true, 2, 0, timestamp);
        }
        if ((next->alarm_flags & SAFETY_ALARM_METER_STALE) != 0U) {
            append_event(EVENT_METER_STALE_ALARM, true, 1, 0, timestamp);
        }
        s_observed = *next;
        s_observed.initialized = true;
        return;
    }
    if (next->network_online != s_observed.network_online) {
        append_event(EVENT_NETWORK_STATE, next->network_online, next->network_online ? 0 : 2, 0, timestamp);
    }
    if (next->meter_online != s_observed.meter_online) {
        append_event(EVENT_METER_STATE, next->meter_online, next->meter_online ? 0 : 2, 0, timestamp);
    }
    if (next->inverter_online != s_observed.inverter_online || next->inverter_enabled != s_observed.inverter_enabled) {
        uint8_t severity = next->inverter_enabled == 0 ? 0 : next->inverter_online == next->inverter_enabled ? 0 : next->inverter_online > 0 ? 1 : 2;
        int32_t packed = ((int32_t)next->inverter_online << 16) | next->inverter_enabled;
        append_event(EVENT_INVERTER_FLEET_STATE, next->inverter_online == next->inverter_enabled, severity, packed, timestamp);
    }
    if (next->control_enabled != s_observed.control_enabled) {
        append_event(EVENT_CONTROL_STATE, next->control_enabled, next->control_enabled ? 1 : 0, 0, timestamp);
    }
    bool previous_offline = (s_observed.alarm_flags & SAFETY_ALARM_METER_OFFLINE) != 0;
    bool next_offline = (next->alarm_flags & SAFETY_ALARM_METER_OFFLINE) != 0;
    if (previous_offline != next_offline) append_event(EVENT_METER_OFFLINE_ALARM, next_offline, next_offline ? 2 : 0, 0, timestamp);
    bool previous_stale = (s_observed.alarm_flags & SAFETY_ALARM_METER_STALE) != 0;
    bool next_stale = (next->alarm_flags & SAFETY_ALARM_METER_STALE) != 0;
    if (previous_stale != next_stale) append_event(EVENT_METER_STALE_ALARM, next_stale, next_stale ? 1 : 0, 0, timestamp);
    s_observed = *next;
    s_observed.initialized = true;
}

static void operational_task(void *argument)
{
    (void)argument;
    for (;;) {
        operational_sample_t sample;
        observed_state_t observed;
        collect_sample(&sample, &observed);
        detect_events(&observed, sample.timestamp_ms);

        portENTER_CRITICAL(&s_lock);
        append_sample(s_fast, FAST_SAMPLE_COUNT, &s_fast_head, &s_fast_count, &sample);
        if (s_last_minute_ms == 0 || sample.timestamp_ms - s_last_minute_ms >= MINUTE_INTERVAL_MS) {
            append_sample(s_minute, MINUTE_SAMPLE_COUNT, &s_minute_head, &s_minute_count, &sample);
            s_last_minute_ms = sample.timestamp_ms;
        }
        portEXIT_CRITICAL(&s_lock);
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
    }
}

/* Same envelope as send_json, with an explicit status line for error replies. */
static esp_err_t send_json_status(httpd_req_t *request, const char *status, cJSON *root)
{
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) return httpd_resp_send_500(request);
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    esp_err_t err = httpd_resp_sendstr(request, text);
    free(text);
    return err;
}

static esp_err_t send_json(httpd_req_t *request, cJSON *root)
{
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) return httpd_resp_send_500(request);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    esp_err_t err = httpd_resp_sendstr(request, text);
    free(text);
    return err;
}

static void add_sample_json(cJSON *array, const operational_sample_t *sample, uint32_t current)
{
    cJSON *item = cJSON_CreateObject();
    if (!item) return;
    cJSON_AddNumberToObject(item, "age_ms", current - sample->timestamp_ms);
    if (isfinite(sample->grid_kw)) cJSON_AddNumberToObject(item, "grid_kw", sample->grid_kw);
    else cJSON_AddNullToObject(item, "grid_kw");
    if (isfinite(sample->solar_kw)) cJSON_AddNumberToObject(item, "solar_kw", sample->solar_kw);
    else cJSON_AddNullToObject(item, "solar_kw");
    cJSON_AddBoolToObject(item, "meter_online", sample->meter_online != 0);
    cJSON_AddNumberToObject(item, "inverter_online", sample->inverter_online);
    cJSON_AddNumberToObject(item, "inverter_enabled", sample->inverter_enabled);
    cJSON_AddBoolToObject(item, "control_enabled", sample->control_enabled != 0);
    cJSON_AddNumberToObject(item, "alarms", sample->alarm_flags);
    cJSON_AddItemToArray(array, item);
}

static void add_summary(cJSON *root, const operational_sample_t *samples, uint16_t count)
{
    float grid_min = INFINITY, grid_max = -INFINITY, grid_sum = 0.0f;
    float solar_min = INFINITY, solar_max = -INFINITY, solar_sum = 0.0f;
    uint16_t grid_count = 0, solar_count = 0;
    for (uint16_t i = 0; i < count; ++i) {
        const operational_sample_t *sample = &samples[i];
        if (isfinite(sample->grid_kw)) {
            grid_min = fminf(grid_min, sample->grid_kw);
            grid_max = fmaxf(grid_max, sample->grid_kw);
            grid_sum += sample->grid_kw;
            grid_count++;
        }
        if (isfinite(sample->solar_kw)) {
            solar_min = fminf(solar_min, sample->solar_kw);
            solar_max = fmaxf(solar_max, sample->solar_kw);
            solar_sum += sample->solar_kw;
            solar_count++;
        }
    }
    cJSON *summary = cJSON_AddObjectToObject(root, "summary");
    if (grid_count) {
        cJSON_AddNumberToObject(summary, "grid_min_kw", grid_min);
        cJSON_AddNumberToObject(summary, "grid_max_kw", grid_max);
        cJSON_AddNumberToObject(summary, "grid_average_kw", grid_sum / grid_count);
    } else {
        cJSON_AddNullToObject(summary, "grid_min_kw");
        cJSON_AddNullToObject(summary, "grid_max_kw");
        cJSON_AddNullToObject(summary, "grid_average_kw");
    }
    if (solar_count) {
        cJSON_AddNumberToObject(summary, "solar_min_kw", solar_min);
        cJSON_AddNumberToObject(summary, "solar_max_kw", solar_max);
        cJSON_AddNumberToObject(summary, "solar_average_kw", solar_sum / solar_count);
    } else {
        cJSON_AddNullToObject(summary, "solar_min_kw");
        cJSON_AddNullToObject(summary, "solar_max_kw");
        cJSON_AddNullToObject(summary, "solar_average_kw");
    }
}

static esp_err_t history_get(httpd_req_t *request)
{
    char query[48] = {0};
    char range[12] = "15m";
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK) {
        (void)httpd_query_key_value(query, "range", range, sizeof(range));
    }
    bool use_minute = strcmp(range, "1h") == 0 || strcmp(range, "24h") == 0;
    uint16_t limit = strcmp(range, "1h") == 0 ? 60U : strcmp(range, "24h") == 0 ? MINUTE_SAMPLE_COUNT : FAST_SAMPLE_COUNT;
    operational_sample_t *snapshot = calloc(limit ? limit : 1U, sizeof(*snapshot));
    if (!snapshot) return httpd_resp_send_500(request);

    uint16_t count;
    portENTER_CRITICAL(&s_lock);
    const operational_sample_t *ring = use_minute ? s_minute : s_fast;
    uint16_t capacity = use_minute ? MINUTE_SAMPLE_COUNT : FAST_SAMPLE_COUNT;
    uint16_t head = use_minute ? s_minute_head : s_fast_head;
    count = use_minute ? s_minute_count : s_fast_count;
    if (count > limit) count = limit;
    for (uint16_t i = 0; i < count; ++i) {
        uint16_t index = (uint16_t)((head + capacity - count + i) % capacity);
        snapshot[i] = ring[index];
    }
    portEXIT_CRITICAL(&s_lock);

    uint32_t current = now_ms();
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(snapshot);
        return httpd_resp_send_500(request);
    }
    cJSON_AddStringToObject(root, "range", use_minute ? (limit == 60U ? "1h" : "24h") : "15m");
    cJSON_AddNumberToObject(root, "generated_ms", current);
    cJSON_AddBoolToObject(root, "controller_resident", true);
    cJSON_AddNumberToObject(root, "sample_interval_ms", use_minute ? MINUTE_INTERVAL_MS : SAMPLE_INTERVAL_MS);
    cJSON *items = cJSON_AddArrayToObject(root, "samples");
    for (uint16_t i = 0; i < count; ++i) add_sample_json(items, &snapshot[i], current);
    add_summary(root, snapshot, count);
    free(snapshot);
    return send_json(request, root);
}

/* The stored `active` byte is a transition polarity, not a condition state, and
 * its sense is not the same for every code: for the network and grid-measurement
 * records a 1 means "restored", while for the meter alarms a 1 means "the alarm
 * is present". Reporting that byte directly as "active" therefore told an
 * operator that "Grid measurement restored" and "Network restored" were active
 * conditions, which reads as an unresolved fault.
 *
 * These two helpers translate the stored polarity into the two things a caller
 * actually needs: whether this record is an alarm condition or a plain event,
 * and, for an alarm, whether the condition is present right now. */
static bool event_is_alarm_condition(uint8_t code)
{
    switch ((operational_event_code_t)code) {
    case EVENT_NETWORK_STATE:
    case EVENT_METER_STATE:
    case EVENT_METER_OFFLINE_ALARM:
    case EVENT_METER_STALE_ALARM:
        return true;
    case EVENT_CONTROLLER_STARTED:
    case EVENT_INVERTER_FLEET_STATE:
    case EVENT_CONTROL_STATE:
    default:
        /* A controller start or a deliberate mode change is something that
         * happened, not a condition that persists. */
        return false;
    }
}

static bool event_condition_present(uint8_t code, uint8_t raw_active)
{
    switch ((operational_event_code_t)code) {
    /* Here a stored 1 means the good state was reached, so the condition that
     * would concern an operator is present when the byte is 0. */
    case EVENT_NETWORK_STATE:
    case EVENT_METER_STATE:
        return raw_active == 0U;
    /* Here a stored 1 means the alarm itself is present. */
    case EVENT_METER_OFFLINE_ALARM:
    case EVENT_METER_STALE_ALARM:
        return raw_active != 0U;
    default:
        return false;   /* events do not persist as a condition */
    }
}

static const char *event_state_label(uint8_t code, uint8_t raw_active)
{
    if (!event_is_alarm_condition(code)) return "recorded";
    return event_condition_present(code, raw_active) ? "active" : "cleared";
}

static const char *severity_label(uint8_t severity)
{
    return severity >= 2 ? "critical" : severity == 1 ? "warning" : "information";
}

static void event_text(const operational_event_t *event, const char **title, const char **detail, const char **action)
{
    switch ((operational_event_code_t)event->code) {
        case EVENT_CONTROLLER_STARTED:
            *title = "Controller started"; *detail = "The controller completed startup and began operational monitoring."; *action = "No action required."; break;
        case EVENT_NETWORK_STATE:
            *title = event->active ? "Network restored" : "Controller network offline";
            *detail = event->active ? "The controller reconnected to the configured network." : "The controller lost its primary network connection.";
            *action = event->active ? "Confirm remote access is stable." : "Check Wi-Fi coverage, router power, and site network availability."; break;
        case EVENT_METER_STATE:
            *title = event->active ? "Grid measurement restored" : "Grid measurement unavailable";
            *detail = event->active ? "Fresh utility-grid measurements are available again." : "Fresh grid power data is not available for monitoring or control.";
            *action = event->active ? "No action required." : "Check meter power, communication wiring, gateway, and network path."; break;
        case EVENT_INVERTER_FLEET_STATE: {
            uint8_t online = (uint8_t)((event->value >> 16) & 0xFF);
            uint8_t enabled = (uint8_t)(event->value & 0xFF);
            *title = online == enabled ? "Solar fleet available" : "Solar fleet attention required";
            *detail = online == enabled ? "All enabled solar inverters are available." : "One or more enabled solar inverters are unavailable.";
            *action = online == enabled ? "No action required." : "Review the Solar Inverters page and inspect unavailable units.";
            break;
        }
        case EVENT_CONTROL_STATE:
            *title = event->active ? "Automatic control enabled" : "Automatic control disabled";
            *detail = event->active ? "The qualified PV-DG control path is active." : "The controller is operating in monitoring-only mode.";
            *action = event->active ? "Observe power flow and alarms." : "Engineering authorization is required before enabling control."; break;
        case EVENT_METER_OFFLINE_ALARM:
            *title = event->active ? "Meter offline alarm" : "Meter offline alarm cleared";
            *detail = event->active ? "The primary grid meter is not communicating." : "Meter communication has recovered.";
            *action = event->active ? "Inspect meter and communication equipment." : "Confirm measurements remain stable."; break;
        case EVENT_METER_STALE_ALARM:
            *title = event->active ? "Meter data stale" : "Meter data freshness restored";
            *detail = event->active ? "The latest grid measurement exceeded the allowed freshness window." : "Current grid measurements are fresh again.";
            *action = event->active ? "Check polling latency and communication reliability." : "No action required."; break;
        default:
            *title = "Controller event"; *detail = "An operational state changed."; *action = "Review controller status."; break;
    }
}

static esp_err_t events_get(httpd_req_t *request)
{
    operational_event_t *snapshot = calloc(EVENT_COUNT, sizeof(*snapshot));
    if (!snapshot) return httpd_resp_send_500(request);

    uint16_t count;
    portENTER_CRITICAL(&s_lock);
    count = s_event_count;
    for (uint16_t i = 0; i < count; ++i) {
        uint16_t index = (uint16_t)((s_event_head + EVENT_COUNT - 1U - i) % EVENT_COUNT);
        snapshot[i] = s_events[index];
    }
    portEXIT_CRITICAL(&s_lock);

    uint32_t current = now_ms();
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(snapshot);
        return httpd_resp_send_500(request);
    }
    cJSON_AddBoolToObject(root, "operator_view", true);
    cJSON_AddBoolToObject(root, "engineering_details_hidden", true);
    cJSON_AddNumberToObject(root, "generated_ms", current);
    cJSON *items = cJSON_AddArrayToObject(root, "events");

    uint16_t active_critical = 0, active_warning = 0;
    for (uint16_t i = 0; i < count; ++i) {
        const operational_event_t *event = &snapshot[i];
        const char *title, *detail, *action;
        event_text(event, &title, &detail, &action);
        cJSON *item = cJSON_CreateObject();
        if (!item) continue;
        cJSON_AddNumberToObject(item, "sequence", event->sequence);
        cJSON_AddNumberToObject(item, "age_ms", current - event->timestamp_ms);
        const bool is_condition = event_is_alarm_condition(event->code);
        const bool present = event_condition_present(event->code, event->active);
        cJSON_AddStringToObject(item, "severity", severity_label(event->severity));
        cJSON_AddStringToObject(item, "kind", is_condition ? "alarm" : "event");
        cJSON_AddStringToObject(item, "state", event_state_label(event->code, event->active));
        /* "active" now means the condition is present right now, which is what a
         * caller reasonably assumes. A restored or informational record is not
         * active. */
        cJSON_AddBoolToObject(item, "active", present);
        cJSON_AddStringToObject(item, "title", title);
        cJSON_AddStringToObject(item, "detail", detail);
        cJSON_AddStringToObject(item, "recommended_action", action);
        cJSON_AddItemToArray(items, item);
        if (present && event->severity >= 2) active_critical++;
        else if (present && event->severity == 1) active_warning++;
    }
    free(snapshot);
    cJSON *summary = cJSON_AddObjectToObject(root, "summary");
    cJSON_AddNumberToObject(summary, "active_critical", active_critical);
    cJSON_AddNumberToObject(summary, "active_warning", active_warning);
    cJSON_AddNumberToObject(summary, "stored_events", count);
    return send_json(request, root);
}

static const char *alarm_code_id(uint8_t code)
{
    switch ((operational_event_code_t)code) {
    case EVENT_NETWORK_STATE:        return "NET-001";
    case EVENT_METER_STATE:          return "MTR-001";
    case EVENT_METER_OFFLINE_ALARM:  return "MTR-002";
    case EVENT_METER_STALE_ALARM:    return "MTR-003";
    default:                         return "GEN-000";
    }
}

/* The alarm condition table: what is wrong now, since when, how often, and
 * whether anyone has taken responsibility. Cleared-but-unacknowledged rows are
 * still returned, because an operator needs to see that something happened
 * while they were not looking. */
static esp_err_t alarms_get(httpd_req_t *request)
{
    operational_alarm_t snapshot[sizeof(s_alarms) / sizeof(s_alarms[0])];
    portENTER_CRITICAL(&s_lock);
    memcpy(snapshot, s_alarms, sizeof(snapshot));
    portEXIT_CRITICAL(&s_lock);

    const uint32_t current = now_ms();
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddNumberToObject(root, "generated_ms", current);
    cJSON *items = cJSON_AddArrayToObject(root, "alarms");
    uint16_t active = 0, unacknowledged = 0;

    for (size_t code = 0; code < sizeof(snapshot) / sizeof(snapshot[0]); ++code) {
        const operational_alarm_t *a = &snapshot[code];
        if (!event_is_alarm_condition((uint8_t)code)) continue;
        if (a->occurrences == 0U) continue;          /* never seen: nothing to report */

        const operational_event_t probe = {
            .code = (uint8_t)code,
            /* Ask for the wording of whichever side the condition is on now. */
            .active = event_condition_present((uint8_t)code, 1U) == a->present ? 1U : 0U,
        };
        const char *title, *detail, *action;
        event_text(&probe, &title, &detail, &action);

        cJSON *item = cJSON_CreateObject();
        if (!item) continue;
        cJSON_AddStringToObject(item, "id", alarm_code_id((uint8_t)code));
        cJSON_AddNumberToObject(item, "code", (double)code);
        cJSON_AddStringToObject(item, "title", title);
        cJSON_AddStringToObject(item, "detail", detail);
        cJSON_AddStringToObject(item, "recommended_action", action);
        cJSON_AddStringToObject(item, "severity", severity_label(a->severity));
        cJSON_AddStringToObject(item, "state",
                                a->present ? (a->acknowledged ? "acknowledged" : "active")
                                           : "cleared");
        cJSON_AddBoolToObject(item, "present", a->present);
        cJSON_AddBoolToObject(item, "acknowledged", a->acknowledged);
        cJSON_AddNumberToObject(item, "occurrences", a->occurrences);
        cJSON_AddNumberToObject(item, "first_raised_age_ms",
                                (double)(current - a->first_raised_ms));
        cJSON_AddNumberToObject(item, "last_raised_age_ms",
                                (double)(current - a->last_raised_ms));
        /* Duration is measured to the clear for a finished condition and to now
         * for one still standing, so a live alarm's age keeps growing. */
        cJSON_AddNumberToObject(item, "duration_ms",
                                (double)((a->present ? current : a->cleared_ms) - a->last_raised_ms));
        if (a->acknowledged) {
            cJSON_AddNumberToObject(item, "acknowledged_age_ms",
                                    (double)(current - a->acknowledged_ms));
        } else {
            cJSON_AddNullToObject(item, "acknowledged_age_ms");
        }
        cJSON_AddItemToArray(items, item);

        if (a->present) active++;
        if (a->present && !a->acknowledged) unacknowledged++;
    }

    cJSON *summary = cJSON_AddObjectToObject(root, "summary");
    cJSON_AddNumberToObject(summary, "active", active);
    cJSON_AddNumberToObject(summary, "unacknowledged", unacknowledged);
    return send_json(request, root);
}

/* Acknowledgement is a deliberate act, so it names the condition rather than
 * offering a blanket "clear all": acknowledging something an operator has not
 * looked at is exactly what this is meant to prevent. It never clears the
 * condition - only the plant can do that. */
static esp_err_t alarms_ack_post(httpd_req_t *request)
{
    /* This translation unit is deliberately outside the authorization gateway so
     * operator history and events stay readable without a session. That makes
     * this POST the one mutating endpoint here, and it must not be anonymous:
     * an unattributable acknowledgement is a way to make an active condition
     * look attended to. The check is therefore explicit rather than inherited.
     *
     * Note the controller has no operator identity model, so an acknowledgement
     * records that an authenticated engineering session did it, not which
     * person. Reporting a name would be inventing one. */
    if (!engineering_auth_is_authorized(request)) {
        cJSON *err = cJSON_CreateObject();
        if (!err) return httpd_resp_send_500(request);
        cJSON_AddStringToObject(err, "error", "engineering_authentication_required");
        cJSON_AddStringToObject(err, "message",
                                "Acknowledging an alarm requires an authenticated engineering session.");
        return send_json_status(request, "401 Unauthorized", err);
    }

    cJSON *root = NULL;
    const esp_err_t read_error = http_json_parse_bounded(request, 256U, 3000ULL, 4U, &root);
    if (read_error != ESP_OK) {
        cJSON *err = cJSON_CreateObject();
        if (!err) return httpd_resp_send_500(request);
        cJSON_AddStringToObject(err, "error", "Acknowledgement body must be valid bounded JSON");
        return send_json_status(request, "400 Bad Request", err);
    }

    const cJSON *code_item = cJSON_GetObjectItemCaseSensitive(root, "code");
    const bool have_code = cJSON_IsNumber(code_item);
    const int code = have_code ? code_item->valueint : -1;
    cJSON_Delete(root);

    if (!have_code || code < 0 ||
        (size_t)code >= sizeof(s_alarms) / sizeof(s_alarms[0]) ||
        !event_is_alarm_condition((uint8_t)code)) {
        cJSON *err = cJSON_CreateObject();
        if (!err) return httpd_resp_send_500(request);
        cJSON_AddStringToObject(err, "error", "A known alarm code is required");
        return send_json_status(request, "400 Bad Request", err);
    }

    const uint32_t timestamp = now_ms();
    bool present = false;
    portENTER_CRITICAL(&s_lock);
    operational_alarm_t *alarm = &s_alarms[code];
    present = alarm->present;
    if (present && !alarm->acknowledged) {
        alarm->acknowledged = true;
        alarm->acknowledged_ms = timestamp;
    }
    portEXIT_CRITICAL(&s_lock);

    cJSON *reply = cJSON_CreateObject();
    if (!reply) return httpd_resp_send_500(request);
    cJSON_AddBoolToObject(reply, "acknowledged", present);
    cJSON_AddNumberToObject(reply, "code", code);
    /* Acknowledging something that is no longer present is not an error, but
     * saying so plainly stops it looking like the condition was dismissed. */
    cJSON_AddStringToObject(reply, "note",
                            present ? "Condition acknowledged; it remains active until the plant clears it."
                                    : "Condition is no longer present; nothing to acknowledge.");
    return send_json(request, reply);
}

esp_err_t operational_api_register(httpd_handle_t server)
{
    if (!s_task) {
        BaseType_t created = xTaskCreate(operational_task, "op_history", 5120, NULL, 4, &s_task);
        if (created != pdPASS) return ESP_ERR_NO_MEM;
    }
    const httpd_uri_t handlers[] = {
        {.uri = "/api/operator/history", .method = HTTP_GET, .handler = history_get},
        {.uri = "/api/operator/events", .method = HTTP_GET, .handler = events_get},
        {.uri = "/api/operator/alarms", .method = HTTP_GET, .handler = alarms_get},
        {.uri = "/api/operator/alarms/ack", .method = HTTP_POST, .handler = alarms_ack_post},
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &handlers[i]), "operational_api", "handler registration failed");
    }
    return ESP_OK;
}
