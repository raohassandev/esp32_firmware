#include "inverter_profile_api.h"

#include <stdlib.h>
#include "cJSON.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "http_json.h"
#include "inverter_manager.h"
#include "inverter_profile_store.h"
#include "inverter_profiles.h"

#define MAX_ASSIGNMENT_BODY 256U
#define PROFILE_BODY_DEADLINE_MS 3000U
#define PROFILE_JSON_MAX_DEPTH 4U

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static esp_err_t send_json(httpd_req_t *request, cJSON *root)
{
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return httpd_resp_send_500(request);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    esp_err_t err = httpd_resp_sendstr(request, json);
    free(json);
    return err;
}

static esp_err_t profiles_get(httpd_req_t *request)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);

    cJSON_AddNumberToObject(root, "count", inverter_profiles_count());
    cJSON_AddBoolToObject(root, "writes_require_production_approval", true);
    cJSON *profiles = cJSON_AddArrayToObject(root, "profiles");

    for (size_t index = 0; index < inverter_profiles_count(); ++index) {
        const inverter_profile_t *profile = inverter_profiles_get(index);
        if (!profile) continue;

        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", profile->id);
        cJSON_AddStringToObject(item, "manufacturer", profile->manufacturer);
        cJSON_AddStringToObject(item, "model_family", profile->model_family);
        cJSON_AddStringToObject(item, "protocol", profile->protocol);
        cJSON_AddStringToObject(item, "connection", inverter_profile_connection_label(profile->connection));
        cJSON_AddStringToObject(item, "qualification", inverter_profile_qualification_label(profile->qualification));
        cJSON_AddStringToObject(item, "manual_reference", profile->manual_reference ? profile->manual_reference : "");
        cJSON_AddBoolToObject(item, "simulator_only", profile->simulator_only);
        cJSON_AddBoolToObject(item, "read_allowed", inverter_profile_allows_read(profile));
        cJSON_AddBoolToObject(item, "write_allowed", inverter_profile_allows_write(profile));
        cJSON_AddBoolToObject(item, "identity_probe_supported", profile->has_identity_probe);
        cJSON_AddBoolToObject(item, "active_power_supported", profile->has_active_power);
        cJSON_AddBoolToObject(item, "power_limit_supported", profile->has_power_limit);
        cJSON_AddBoolToObject(item, "power_limit_readback_supported", profile->has_power_limit_readback);
        cJSON_AddNumberToObject(item, "telemetry_poll_ms", profile->telemetry_poll_ms);
        cJSON_AddNumberToObject(item, "telemetry_stale_timeout_ms", profile->telemetry_stale_timeout_ms);

        cJSON *limits = cJSON_AddObjectToObject(item, "limits");
        cJSON_AddNumberToObject(limits, "minimum_percent", profile->minimum_percent);
        cJSON_AddNumberToObject(limits, "maximum_percent", profile->maximum_percent);

        cJSON_AddItemToArray(profiles, item);
    }

    return send_json(request, root);
}

static esp_err_t telemetry_get(httpd_req_t *request)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);

    uint32_t current_ms = now_ms();
    uint8_t count = inverter_manager_get_count();
    uint8_t online = 0;
    uint8_t telemetry_valid = 0;
    uint8_t stale = 0;
    uint8_t identity_verified = 0;
    uint8_t mismatched = 0;
    float measured_total_kw = 0.0f;

    cJSON_AddNumberToObject(root, "generated_ms", current_ms);
    cJSON_AddNumberToObject(root, "count", count);
    cJSON_AddBoolToObject(root, "read_only_endpoint", true);
    cJSON_AddBoolToObject(root, "writes_issued", false);
    cJSON *items = cJSON_AddArrayToObject(root, "inverters");

    for (uint8_t index = 0; index < count; ++index) {
        inverter_data_t data = {0};
        if (!inverter_manager_get_data(index, &data)) continue;

        if (data.online) online++;
        if (data.telemetry_valid) {
            telemetry_valid++;
            measured_total_kw += data.measured_power_kw;
        }
        if (data.telemetry_stale) stale++;
        if (data.identity_verified) identity_verified++;
        if (data.command_mismatch) mismatched++;

        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "index", index);
        cJSON_AddBoolToObject(item, "connection_initialized", data.connection_initialized);
        cJSON_AddBoolToObject(item, "online", data.online);
        cJSON_AddBoolToObject(item, "identity_supported", data.identity_supported);
        cJSON_AddBoolToObject(item, "identity_verified", data.identity_verified);
        cJSON_AddBoolToObject(item, "telemetry_supported", data.telemetry_supported);
        cJSON_AddBoolToObject(item, "telemetry_valid", data.telemetry_valid);
        cJSON_AddBoolToObject(item, "telemetry_stale", data.telemetry_stale);
        if (data.telemetry_valid) {
            cJSON_AddNumberToObject(item, "measured_power_kw", data.measured_power_kw);
            cJSON_AddNumberToObject(item, "telemetry_age_ms", current_ms - data.last_telemetry_ms);
        } else {
            cJSON_AddNullToObject(item, "measured_power_kw");
            cJSON_AddNullToObject(item, "telemetry_age_ms");
        }
        cJSON_AddBoolToObject(item, "has_readback", data.has_readback);
        if (data.has_readback) {
            cJSON_AddNumberToObject(item, "readback_percent", data.readback_percent);
            cJSON_AddNumberToObject(item, "readback_age_ms", current_ms - data.last_readback_ms);
        } else {
            cJSON_AddNullToObject(item, "readback_percent");
            cJSON_AddNullToObject(item, "readback_age_ms");
        }
        cJSON_AddBoolToObject(item, "has_command", data.has_command);
        cJSON_AddBoolToObject(item, "command_mismatch", data.command_mismatch);
        cJSON_AddNumberToObject(item, "mismatch_count", data.mismatch_count);
        cJSON_AddNumberToObject(item, "read_successes", data.read_successes);
        cJSON_AddNumberToObject(item, "read_errors", data.read_errors);
        cJSON_AddNumberToObject(item, "consecutive_read_failures", data.consecutive_read_failures);
        cJSON_AddNumberToObject(item, "last_error", data.last_error);
        cJSON_AddStringToObject(item, "last_error_name", esp_err_to_name(data.last_error));
        cJSON_AddItemToArray(items, item);
    }

    cJSON *summary = cJSON_AddObjectToObject(root, "summary");
    cJSON_AddNumberToObject(summary, "online", online);
    cJSON_AddNumberToObject(summary, "telemetry_valid", telemetry_valid);
    cJSON_AddNumberToObject(summary, "stale", stale);
    cJSON_AddNumberToObject(summary, "identity_verified", identity_verified);
    cJSON_AddNumberToObject(summary, "command_mismatched", mismatched);
    cJSON_AddNumberToObject(summary, "measured_total_kw", measured_total_kw);
    cJSON_AddNumberToObject(summary, "commandable_rated_kw", inverter_manager_get_total_rated_kw());

    return send_json(request, root);
}

static bool read_inverter_index(cJSON *json, uint8_t *inverter_index)
{
    cJSON *index_item = cJSON_GetObjectItemCaseSensitive(json, "inverter_index");
    if (!cJSON_IsNumber(index_item) || index_item->valueint < 0 ||
        index_item->valueint >= APP_MAX_INVERTERS) return false;
    *inverter_index = (uint8_t)index_item->valueint;
    return true;
}

static cJSON *parse_small_request(httpd_req_t *request)
{
    cJSON *root = NULL;
    if (http_json_parse_bounded(request, MAX_ASSIGNMENT_BODY,
                                PROFILE_BODY_DEADLINE_MS,
                                PROFILE_JSON_MAX_DEPTH, &root) != ESP_OK) {
        return NULL;
    }
    return root;
}

static esp_err_t profile_assignment_post(httpd_req_t *request)
{
    cJSON *json = parse_small_request(request);
    if (!json) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Profile assignment must be valid bounded JSON");
    }

    uint8_t inverter_index = 0;
    cJSON *profile_item = cJSON_GetObjectItemCaseSensitive(json, "profile_id");
    if (!read_inverter_index(json, &inverter_index) || !cJSON_IsString(profile_item)) {
        cJSON_Delete(json);
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "inverter_index and profile_id are required");
    }

    const inverter_profile_t *profile = inverter_profiles_find(profile_item->valuestring);
    if (!profile) {
        cJSON_Delete(json);
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Unknown inverter profile");
    }

    esp_err_t err = inverter_profile_store_set(inverter_index, profile->id);
    cJSON_Delete(json);
    if (err != ESP_OK) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Failed to save inverter profile assignment");
    }

    cJSON *response = cJSON_CreateObject();
    if (!response) return httpd_resp_send_500(request);
    cJSON_AddBoolToObject(response, "saved", true);
    cJSON_AddNumberToObject(response, "inverter_index", inverter_index);
    cJSON_AddStringToObject(response, "profile_id", profile->id);
    cJSON_AddBoolToObject(response, "simulator_only", profile->simulator_only);
    cJSON_AddBoolToObject(response, "automatic_control_disabled", true);
    cJSON_AddBoolToObject(response, "restart_required", true);
    cJSON_AddBoolToObject(response, "write_allowed_after_restart",
                          inverter_profile_allows_write(profile));
    return send_json(request, response);
}

static void add_register_array(cJSON *parent, const char *name,
                               const uint16_t *registers, uint8_t count)
{
    cJSON *array = cJSON_AddArrayToObject(parent, name);
    for (uint8_t index = 0; index < count; ++index) {
        cJSON_AddItemToArray(array, cJSON_CreateNumber(registers[index]));
    }
}

static esp_err_t inverter_probe_post(httpd_req_t *request)
{
    cJSON *json = parse_small_request(request);
    if (!json) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Inverter probe must be valid bounded JSON");
    }

    uint8_t inverter_index = 0;
    if (!read_inverter_index(json, &inverter_index)) {
        cJSON_Delete(json);
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Valid inverter_index is required");
    }
    cJSON_Delete(json);

    inverter_probe_result_t probe = {0};
    esp_err_t probe_error = inverter_manager_probe_read_only(inverter_index, &probe);

    cJSON *response = cJSON_CreateObject();
    if (!response) return httpd_resp_send_500(request);
    cJSON_AddNumberToObject(response, "inverter_index", inverter_index);
    cJSON_AddBoolToObject(response, "read_only", true);
    cJSON_AddBoolToObject(response, "writes_issued", false);
    cJSON_AddNumberToObject(response, "result_error", probe_error);
    cJSON_AddStringToObject(response, "result_error_name", esp_err_to_name(probe_error));
    cJSON_AddBoolToObject(response, "profile_read_allowed", probe.profile_read_allowed);
    cJSON_AddBoolToObject(response, "connection_initialized", probe.connection_initialized);

    cJSON *identity = cJSON_AddObjectToObject(response, "identity");
    cJSON_AddBoolToObject(identity, "attempted", probe.identity_attempted);
    cJSON_AddBoolToObject(identity, "ok", probe.identity_ok);
    cJSON_AddNumberToObject(identity, "error", probe.identity_error);
    cJSON_AddStringToObject(identity, "error_name", esp_err_to_name(probe.identity_error));
    add_register_array(identity, "registers", probe.identity_registers,
                       probe.identity_count);

    cJSON *active_power = cJSON_AddObjectToObject(response, "active_power");
    cJSON_AddBoolToObject(active_power, "attempted", probe.active_power_attempted);
    cJSON_AddBoolToObject(active_power, "ok", probe.active_power_ok);
    cJSON_AddNumberToObject(active_power, "error", probe.active_power_error);
    cJSON_AddStringToObject(active_power, "error_name",
                            esp_err_to_name(probe.active_power_error));
    add_register_array(active_power, "registers", probe.active_power_registers,
                       probe.active_power_count);

    return send_json(request, response);
}

esp_err_t inverter_profile_api_register(httpd_handle_t server)
{
    const httpd_uri_t handlers[] = {
        {.uri = "/api/inverter-profiles", .method = HTTP_GET, .handler = profiles_get},
        {.uri = "/api/inverter-telemetry", .method = HTTP_GET, .handler = telemetry_get},
        {.uri = "/api/inverter-profile-assignment", .method = HTTP_POST, .handler = profile_assignment_post},
        {.uri = "/api/inverter-probe", .method = HTTP_POST, .handler = inverter_probe_post}
    };

    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &handlers[i]),
                            "inverter_profile_api", "handler registration failed");
    }
    return ESP_OK;
}
