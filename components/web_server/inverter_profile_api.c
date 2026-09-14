#include "inverter_profile_api.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "http_json.h"
#include "inverter_manager.h"
#include "inverter_profile_store.h"
#include "inverter_profiles.h"
#include "inverter_status.h"

#define MAX_ASSIGNMENT_BODY 256U
#define PROFILE_MANIFEST_MAX_BODY 8192U
#define PROFILE_BODY_DEADLINE_MS 3000U
#define PROFILE_MANIFEST_BODY_DEADLINE_MS 5000U
#define PROFILE_JSON_MAX_DEPTH 4U
#define PROFILE_MANIFEST_JSON_MAX_DEPTH 5U
#define PROFILE_MANIFEST_SCHEMA 1
#define PROFILE_MANIFEST_KIND "compiled_profile_assignment_manifest"

#define FNV1A64_OFFSET UINT64_C(14695981039346656037)
#define FNV1A64_PRIME UINT64_C(1099511628211)

/* Implemented in inverter_profile_store_guard.c. The guard stops the running
 * control task before the store disables persisted automatic control and
 * atomically replaces the assignment map. */
esp_err_t inverter_profile_store_set_all_guarded(
    const inverter_profile_assignment_manifest_t *manifest);

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

static uint64_t fingerprint_update(uint64_t hash, const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0; i < length; ++i) {
        hash ^= bytes[i];
        hash *= FNV1A64_PRIME;
    }
    return hash;
}

static uint64_t fingerprint_string(uint64_t hash, const char *value)
{
    const char *text = value ? value : "";
    hash = fingerprint_update(hash, text, strlen(text));
    const uint8_t delimiter = 0U;
    return fingerprint_update(hash, &delimiter, sizeof(delimiter));
}

static uint64_t profile_definition_fingerprint(const inverter_profile_t *profile)
{
    if (!profile) return 0U;
    uint64_t hash = FNV1A64_OFFSET;

    hash = fingerprint_string(hash, profile->id);
    hash = fingerprint_string(hash, profile->manufacturer);
    hash = fingerprint_string(hash, profile->model_family);
    hash = fingerprint_string(hash, profile->protocol);
    hash = fingerprint_update(hash, &profile->connection, sizeof(profile->connection));
    hash = fingerprint_update(hash, &profile->qualification, sizeof(profile->qualification));
    hash = fingerprint_string(hash, profile->manual_reference);
    hash = fingerprint_update(hash, &profile->simulator_only, sizeof(profile->simulator_only));

    hash = fingerprint_update(hash, &profile->has_identity_probe, sizeof(profile->has_identity_probe));
    hash = fingerprint_update(hash, &profile->identity_function, sizeof(profile->identity_function));
    hash = fingerprint_update(hash, &profile->identity_address, sizeof(profile->identity_address));
    hash = fingerprint_update(hash, &profile->identity_words, sizeof(profile->identity_words));
    hash = fingerprint_update(hash, &profile->identity_expected, sizeof(profile->identity_expected));
    hash = fingerprint_update(hash, &profile->identity_mask, sizeof(profile->identity_mask));

    hash = fingerprint_update(hash, &profile->has_active_power, sizeof(profile->has_active_power));
    hash = fingerprint_update(hash, &profile->active_power_function, sizeof(profile->active_power_function));
    hash = fingerprint_update(hash, &profile->active_power_address, sizeof(profile->active_power_address));
    hash = fingerprint_update(hash, &profile->active_power_words, sizeof(profile->active_power_words));
    hash = fingerprint_update(hash, &profile->active_power_type, sizeof(profile->active_power_type));
    hash = fingerprint_update(hash, &profile->active_power_word_order, sizeof(profile->active_power_word_order));
    hash = fingerprint_update(hash, &profile->active_power_scale, sizeof(profile->active_power_scale));

    hash = fingerprint_update(hash, &profile->has_power_limit, sizeof(profile->has_power_limit));
    hash = fingerprint_update(hash, &profile->power_limit_function, sizeof(profile->power_limit_function));
    hash = fingerprint_update(hash, &profile->power_limit_address, sizeof(profile->power_limit_address));
    hash = fingerprint_update(hash, &profile->power_limit_words, sizeof(profile->power_limit_words));
    hash = fingerprint_update(hash, &profile->raw_units_per_percent, sizeof(profile->raw_units_per_percent));
    hash = fingerprint_update(hash, &profile->minimum_percent, sizeof(profile->minimum_percent));
    hash = fingerprint_update(hash, &profile->maximum_percent, sizeof(profile->maximum_percent));

    hash = fingerprint_update(hash, &profile->has_power_limit_readback,
                              sizeof(profile->has_power_limit_readback));
    hash = fingerprint_update(hash, &profile->power_limit_readback_function,
                              sizeof(profile->power_limit_readback_function));
    hash = fingerprint_update(hash, &profile->power_limit_readback_address,
                              sizeof(profile->power_limit_readback_address));
    hash = fingerprint_update(hash, &profile->power_limit_readback_words,
                              sizeof(profile->power_limit_readback_words));
    hash = fingerprint_update(hash, &profile->power_limit_readback_type,
                              sizeof(profile->power_limit_readback_type));
    hash = fingerprint_update(hash, &profile->power_limit_readback_word_order,
                              sizeof(profile->power_limit_readback_word_order));
    hash = fingerprint_update(hash, &profile->power_limit_readback_scale,
                              sizeof(profile->power_limit_readback_scale));
    hash = fingerprint_update(hash, &profile->readback_tolerance_percent,
                              sizeof(profile->readback_tolerance_percent));

    hash = fingerprint_update(hash, &profile->status_register.configured,
                              sizeof(profile->status_register.configured));
    hash = fingerprint_update(hash, &profile->status_register.function,
                              sizeof(profile->status_register.function));
    hash = fingerprint_update(hash, &profile->status_register.address,
                              sizeof(profile->status_register.address));
    hash = fingerprint_update(hash, &profile->status_register.words,
                              sizeof(profile->status_register.words));
    hash = fingerprint_update(hash, &profile->status_register.type,
                              sizeof(profile->status_register.type));
    hash = fingerprint_update(hash, &profile->status_register.word_order,
                              sizeof(profile->status_register.word_order));
    hash = fingerprint_update(hash, &profile->status_register.mapping_count,
                              sizeof(profile->status_register.mapping_count));
    for (uint8_t i = 0; i < profile->status_register.mapping_count &&
                        i < INVERTER_STATUS_MAX_MAPPINGS; ++i) {
        const inverter_status_mapping_t *mapping = &profile->status_register.mappings[i];
        hash = fingerprint_update(hash, &mapping->raw_value, sizeof(mapping->raw_value));
        hash = fingerprint_update(hash, &mapping->raw_mask, sizeof(mapping->raw_mask));
        hash = fingerprint_update(hash, &mapping->state, sizeof(mapping->state));
    }

    hash = fingerprint_update(hash, &profile->telemetry_poll_ms, sizeof(profile->telemetry_poll_ms));
    hash = fingerprint_update(hash, &profile->telemetry_stale_timeout_ms,
                              sizeof(profile->telemetry_stale_timeout_ms));
    return hash;
}

static void profile_fingerprint_text(const inverter_profile_t *profile, char output[17])
{
    snprintf(output, 17, "%016" PRIx64, profile_definition_fingerprint(profile));
}

static void add_profile_identity(cJSON *item, const inverter_profile_t *profile)
{
    char fingerprint[17];
    profile_fingerprint_text(profile, fingerprint);
    cJSON_AddStringToObject(item, "profile_id", profile->id);
    cJSON_AddStringToObject(item, "manufacturer", profile->manufacturer);
    cJSON_AddStringToObject(item, "model_family", profile->model_family);
    cJSON_AddStringToObject(item, "protocol", profile->protocol);
    cJSON_AddStringToObject(item, "connection", inverter_profile_connection_label(profile->connection));
    cJSON_AddStringToObject(item, "qualification", inverter_profile_qualification_label(profile->qualification));
    cJSON_AddStringToObject(item, "manual_reference", profile->manual_reference ? profile->manual_reference : "");
    cJSON_AddBoolToObject(item, "simulator_only", profile->simulator_only);
    cJSON_AddStringToObject(item, "profile_definition_fingerprint", fingerprint);
    cJSON_AddStringToObject(item, "profile_definition_fingerprint_algorithm", "fnv1a64-v1");
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
        cJSON_AddBoolToObject(item, "status_register_supported",
                              inverter_profile_has_status_register(profile));
        cJSON_AddBoolToObject(item, "status_register_commissioning_required",
                              !inverter_profile_has_status_register(profile));
        cJSON_AddNumberToObject(item, "telemetry_poll_ms", profile->telemetry_poll_ms);
        cJSON_AddNumberToObject(item, "telemetry_stale_timeout_ms", profile->telemetry_stale_timeout_ms);
        char fingerprint[17];
        profile_fingerprint_text(profile, fingerprint);
        cJSON_AddStringToObject(item, "profile_definition_fingerprint", fingerprint);
        cJSON_AddStringToObject(item, "profile_definition_fingerprint_algorithm", "fnv1a64-v1");

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
    uint8_t status_supported = 0;
    uint8_t status_on_grid = 0;
    uint8_t status_unknown = 0;
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
        if (data.status_supported) status_supported++;
        if (data.status_state == INVERTER_STATE_ON_GRID && !data.status_stale) status_on_grid++;
        if (data.status_state == INVERTER_STATE_UNKNOWN) status_unknown++;

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
        cJSON_AddBoolToObject(item, "status_supported", data.status_supported);
        cJSON_AddStringToObject(item, "status_state", inverter_state_label(data.status_state));
        cJSON_AddNumberToObject(item, "status_state_code", data.status_state);
        cJSON_AddBoolToObject(item, "status_stale", data.status_stale);
        cJSON_AddBoolToObject(item, "status_synchronised",
                              inverter_state_is_synchronised(data.status_state) &&
                                  !data.status_stale);
        if (data.status_raw_valid) {
            cJSON_AddNumberToObject(item, "status_raw", data.status_raw);
        } else {
            cJSON_AddNullToObject(item, "status_raw");
        }
        if (data.status_supported && data.last_status_ms != 0U) {
            cJSON_AddNumberToObject(item, "status_age_ms", current_ms - data.last_status_ms);
        } else {
            cJSON_AddNullToObject(item, "status_age_ms");
        }
        cJSON_AddNumberToObject(item, "status_read_successes", data.status_read_successes);
        cJSON_AddNumberToObject(item, "status_read_errors", data.status_read_errors);
        cJSON_AddStringToObject(item, "status_last_error_name",
                                esp_err_to_name(data.status_last_error));
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
    cJSON_AddNumberToObject(summary, "status_supported", status_supported);
    cJSON_AddNumberToObject(summary, "status_on_grid", status_on_grid);
    cJSON_AddNumberToObject(summary, "status_unknown", status_unknown);
    cJSON_AddBoolToObject(summary, "fleet_synchronised", inverter_manager_fleet_synchronised());

    return send_json(request, root);
}

static bool read_inverter_index(cJSON *json, uint8_t *inverter_index)
{
    cJSON *index_item = cJSON_GetObjectItemCaseSensitive(json, "inverter_index");
    if (!cJSON_IsNumber(index_item) || index_item->valuedouble != (double)index_item->valueint ||
        index_item->valueint < 0 || index_item->valueint >= APP_MAX_INVERTERS) return false;
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

static esp_err_t profile_manifest_get(httpd_req_t *request)
{
    inverter_profile_assignment_manifest_t manifest;
    if (inverter_profile_store_get_all(&manifest) != ESP_OK) {
        return httpd_resp_send_500(request);
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddNumberToObject(root, "schema", PROFILE_MANIFEST_SCHEMA);
    cJSON_AddStringToObject(root, "kind", PROFILE_MANIFEST_KIND);
    cJSON_AddNumberToObject(root, "assignment_count", APP_MAX_INVERTERS);
    cJSON_AddBoolToObject(root, "dynamic_profile_definitions_importable", false);
    cJSON_AddBoolToObject(root, "qualification_importable", false);
    cJSON_AddBoolToObject(root, "production_approval_importable", false);
    cJSON_AddBoolToObject(root, "automatic_control_forced_disabled_on_import", true);
    cJSON *assignments = cJSON_AddArrayToObject(root, "assignments");

    for (uint8_t index = 0; index < APP_MAX_INVERTERS; ++index) {
        const inverter_profile_t *profile = inverter_profiles_find(manifest.profile_ids[index]);
        if (!profile) {
            cJSON_Delete(root);
            return httpd_resp_send_500(request);
        }
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "inverter_index", index);
        add_profile_identity(item, profile);
        cJSON_AddItemToArray(assignments, item);
    }
    return send_json(request, root);
}

static bool manifest_string_matches(cJSON *item, const char *key, const char *expected)
{
    cJSON *value = cJSON_GetObjectItemCaseSensitive(item, key);
    return cJSON_IsString(value) && strcmp(value->valuestring, expected ? expected : "") == 0;
}

static esp_err_t profile_manifest_post(httpd_req_t *request)
{
    cJSON *json = NULL;
    if (http_json_parse_bounded(request, PROFILE_MANIFEST_MAX_BODY,
                                PROFILE_MANIFEST_BODY_DEADLINE_MS,
                                PROFILE_MANIFEST_JSON_MAX_DEPTH, &json) != ESP_OK ||
        !cJSON_IsObject(json)) {
        cJSON_Delete(json);
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Profile manifest must be valid bounded JSON");
    }

    cJSON *schema = cJSON_GetObjectItemCaseSensitive(json, "schema");
    cJSON *kind = cJSON_GetObjectItemCaseSensitive(json, "kind");
    cJSON *assignments = cJSON_GetObjectItemCaseSensitive(json, "assignments");
    if (!cJSON_IsNumber(schema) || schema->valuedouble != PROFILE_MANIFEST_SCHEMA ||
        !cJSON_IsString(kind) || strcmp(kind->valuestring, PROFILE_MANIFEST_KIND) != 0 ||
        !cJSON_IsArray(assignments) || cJSON_GetArraySize(assignments) != APP_MAX_INVERTERS) {
        cJSON_Delete(json);
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Unsupported or incomplete profile manifest");
    }

    inverter_profile_assignment_manifest_t manifest = {0};
    bool seen[APP_MAX_INVERTERS] = {0};
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, assignments) {
        if (!cJSON_IsObject(item)) {
            cJSON_Delete(json);
            return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                       "Profile manifest assignment must be an object");
        }
        cJSON *index_item = cJSON_GetObjectItemCaseSensitive(item, "inverter_index");
        cJSON *profile_item = cJSON_GetObjectItemCaseSensitive(item, "profile_id");
        cJSON *fingerprint_item = cJSON_GetObjectItemCaseSensitive(item,
                                                                   "profile_definition_fingerprint");
        if (!cJSON_IsNumber(index_item) || index_item->valuedouble != (double)index_item->valueint ||
            index_item->valueint < 0 || index_item->valueint >= APP_MAX_INVERTERS ||
            seen[index_item->valueint] || !cJSON_IsString(profile_item) ||
            !profile_item->valuestring[0] ||
            strlen(profile_item->valuestring) >= INVERTER_PROFILE_ID_MAX ||
            !cJSON_IsString(fingerprint_item)) {
            cJSON_Delete(json);
            return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                       "Profile manifest assignment identity is invalid");
        }

        const inverter_profile_t *profile = inverter_profiles_find(profile_item->valuestring);
        if (!profile) {
            cJSON_Delete(json);
            return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                       "Profile manifest references an unknown compiled profile");
        }

        char expected_fingerprint[17];
        profile_fingerprint_text(profile, expected_fingerprint);
        if (strcmp(fingerprint_item->valuestring, expected_fingerprint) != 0 ||
            !manifest_string_matches(item, "manufacturer", profile->manufacturer) ||
            !manifest_string_matches(item, "model_family", profile->model_family) ||
            !manifest_string_matches(item, "protocol", profile->protocol) ||
            !manifest_string_matches(item, "connection", inverter_profile_connection_label(profile->connection)) ||
            !manifest_string_matches(item, "qualification", inverter_profile_qualification_label(profile->qualification)) ||
            !manifest_string_matches(item, "manual_reference",
                                     profile->manual_reference ? profile->manual_reference : "")) {
            cJSON_Delete(json);
            return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                       "Profile manifest does not match the compiled profile definition");
        }
        cJSON *simulator_item = cJSON_GetObjectItemCaseSensitive(item, "simulator_only");
        if (!cJSON_IsBool(simulator_item) || cJSON_IsTrue(simulator_item) != profile->simulator_only) {
            cJSON_Delete(json);
            return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                       "Profile manifest simulator identity mismatch");
        }

        uint8_t index = (uint8_t)index_item->valueint;
        seen[index] = true;
        strlcpy(manifest.profile_ids[index], profile->id,
                sizeof(manifest.profile_ids[index]));
    }

    for (uint8_t index = 0; index < APP_MAX_INVERTERS; ++index) {
        if (!seen[index]) {
            cJSON_Delete(json);
            return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                       "Profile manifest is missing an inverter assignment");
        }
    }
    cJSON_Delete(json);

    esp_err_t err = inverter_profile_store_set_all_guarded(&manifest);
    if (err != ESP_OK) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Failed to save inverter profile manifest");
    }

    cJSON *response = cJSON_CreateObject();
    if (!response) return httpd_resp_send_500(request);
    cJSON_AddBoolToObject(response, "saved", true);
    cJSON_AddNumberToObject(response, "assignment_count", APP_MAX_INVERTERS);
    cJSON_AddBoolToObject(response, "automatic_control_disabled", true);
    cJSON_AddBoolToObject(response, "restart_required", true);
    cJSON_AddBoolToObject(response, "dynamic_profile_definitions_imported", false);
    cJSON_AddBoolToObject(response, "qualification_imported", false);
    cJSON_AddBoolToObject(response, "production_approval_imported", false);
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
        {.uri = "/api/inverter-profile-manifest", .method = HTTP_GET, .handler = profile_manifest_get},
        {.uri = "/api/inverter-profile-manifest", .method = HTTP_POST, .handler = profile_manifest_post},
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