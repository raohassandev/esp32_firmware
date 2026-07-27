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
    state->meter_online = meter.online && meter_has_data && meter_age <= METER_FRESH_MS;
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

static void add_summary(cJSON *root, const operational_sample_t *samples, uint16_t capacity, uint16_t head, uint16_t count)
{
    float grid_min = INFINITY, grid_max = -INFINITY, grid_sum = 0.0f;
    float solar_min = INFINITY, solar_max = -INFINITY, solar_sum = 0.0f;
    uint16_t grid_count = 0, solar_count = 0;
    for (uint16_t i = 0; i < count; ++i) {
        uint16_t index = (uint16_t)((head + capacity - count + i) % capacity);
        const operational_sample_t *sample = &samples[index];
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
    uint32_t current = now_ms();
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddStringToObject(root, "range", use_minute ? (limit == 60U ? "1h" : "24h") : "15m");
    cJSON_AddNumberToObject(root, "generated_ms", current);
    cJSON_AddBoolToObject(root, "controller_resident", true);
    cJSON_AddNumberToObject(root, "sample_interval_ms", use_minute ? MINUTE_INTERVAL_MS : SAMPLE_INTERVAL_MS);
    cJSON *items = cJSON_AddArrayToObject(root, "samples");

    portENTER_CRITICAL(&s_lock);
    const operational_sample_t *ring = use_minute ? s_minute : s_fast;
    uint16_t capacity = use_minute ? MINUTE_SAMPLE_COUNT : FAST_SAMPLE_COUNT;
    uint16_t head = use_minute ? s_minute_head : s_fast_head;
    uint16_t count = use_minute ? s_minute_count : s_fast_count;
    if (count > limit) count = limit;
    for (uint16_t i = 0; i < count; ++i) {
        uint16_t index = (uint16_t)((head + capacity - count + i) % capacity);
        add_sample_json(items, &ring[index], current);
    }
    add_summary(root, ring, capacity, head, count);
    portEXIT_CRITICAL(&s_lock);
    return send_json(request, root);
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
    uint32_t current = now_ms();
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddBoolToObject(root, "operator_view", true);
    cJSON_AddBoolToObject(root, "engineering_details_hidden", true);
    cJSON_AddNumberToObject(root, "generated_ms", current);
    cJSON *items = cJSON_AddArrayToObject(root, "events");

    uint16_t active_critical = 0, active_warning = 0;
    portENTER_CRITICAL(&s_lock);
    for (uint16_t i = 0; i < s_event_count; ++i) {
        uint16_t index = (uint16_t)((s_event_head + EVENT_COUNT - 1U - i) % EVENT_COUNT);
        const operational_event_t *event = &s_events[index];
        const char *title, *detail, *action;
        event_text(event, &title, &detail, &action);
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "sequence", event->sequence);
        cJSON_AddNumberToObject(item, "age_ms", current - event->timestamp_ms);
        cJSON_AddStringToObject(item, "severity", severity_label(event->severity));
        cJSON_AddBoolToObject(item, "active", event->active != 0);
        cJSON_AddStringToObject(item, "title", title);
        cJSON_AddStringToObject(item, "detail", detail);
        cJSON_AddStringToObject(item, "recommended_action", action);
        cJSON_AddItemToArray(items, item);
        if (event->active && event->severity >= 2) active_critical++;
        else if (event->active && event->severity == 1) active_warning++;
    }
    portEXIT_CRITICAL(&s_lock);
    cJSON *summary = cJSON_AddObjectToObject(root, "summary");
    cJSON_AddNumberToObject(summary, "active_critical", active_critical);
    cJSON_AddNumberToObject(summary, "active_warning", active_warning);
    cJSON_AddNumberToObject(summary, "stored_events", s_event_count);
    return send_json(request, root);
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
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &handlers[i]), "operational_api", "handler registration failed");
    }
    return ESP_OK;
}
