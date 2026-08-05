#include "source_detection_api.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "config_manager.h"
#include "config_types.h"
#include "http_json.h"
#include "source_detection.h"
#include "source_detection_config.h"
#include "source_detection_engine.h"

#define SOURCE_DETECTION_MAX_BODY 4096U
#define SOURCE_DETECTION_BODY_DEADLINE_MS 5000U
#define SOURCE_DETECTION_JSON_MAX_DEPTH 6U

static esp_err_t send_json_text(httpd_req_t *request, const char *status,
                                const char *text)
{
    if (status) httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    return httpd_resp_sendstr(request, text);
}

static esp_err_t send_json_error(httpd_req_t *request, const char *status,
                                 const char *message)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddStringToObject(root, "error", message ? message : "Request failed");
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return httpd_resp_send_500(request);
    const esp_err_t error = send_json_text(request, status, json);
    free(json);
    return error;
}

static void add_float_or_null(cJSON *object, const char *name, float value)
{
    if (isfinite(value)) cJSON_AddNumberToObject(object, name, value);
    else cJSON_AddNullToObject(object, name);
}

static void add_config_json(cJSON *root, const source_detection_config_t *config)
{
    cJSON *object = cJSON_AddObjectToObject(root, "config");
    cJSON_AddNumberToObject(object, "schema", config->version);
    cJSON_AddNumberToObject(object, "mode", config->mode);
    cJSON_AddStringToObject(object, "mode_name",
                            source_detection_mode_name((source_detection_mode_t)config->mode));
    cJSON_AddNumberToObject(object, "debounce_ms", config->debounce_ms);
    cJSON_AddNumberToObject(object, "stale_timeout_ms", config->stale_timeout_ms);

    cJSON *single = cJSON_AddObjectToObject(object, "single_input");
    cJSON_AddNumberToObject(single, "meter_index", config->single.meter_index);
    cJSON_AddNumberToObject(single, "function", config->single.function_code);
    cJSON_AddNumberToObject(single, "address_base", config->single.address_base);
    cJSON_AddNumberToObject(single, "register", config->single.register_address);
    cJSON_AddNumberToObject(single, "grid_value", config->single.grid_value);
    cJSON_AddNumberToObject(single, "generator_value", config->single.generator_value);

    cJSON *dual = cJSON_AddObjectToObject(object, "dual_meter");
    cJSON_AddNumberToObject(dual, "grid_meter_index", config->dual.grid_meter_index);
    cJSON_AddNumberToObject(dual, "generator_meter_index",
                            config->dual.generator_meter_index);
    cJSON_AddNumberToObject(dual, "grid_threshold_kw",
                            config->dual.grid_threshold_kw);
    cJSON_AddNumberToObject(dual, "generator_threshold_kw",
                            config->dual.generator_threshold_kw);
    cJSON_AddBoolToObject(dual, "sync_capable", config->dual.sync_capable != 0U);
}

static void add_status_json(cJSON *root, const source_detection_status_t *status)
{
    cJSON *object = cJSON_AddObjectToObject(root, "status");
    cJSON_AddNumberToObject(object, "config_generation", status->config_generation);
    cJSON_AddStringToObject(object, "state", source_detection_state_name(status->state));
    cJSON_AddStringToObject(object, "candidate_state",
                            source_detection_state_name(status->candidate_state));
    cJSON_AddNumberToObject(object, "tariff", status->tariff);
    cJSON_AddStringToObject(object, "reason_code",
                            source_detection_reason_name(status->reason));
    cJSON_AddStringToObject(object, "reason", status->reason_text);
    cJSON_AddBoolToObject(object, "configured", status->configured);
    cJSON_AddBoolToObject(object, "evidence_fresh", status->evidence_fresh);
    cJSON_AddBoolToObject(object, "transition_pending", status->transition_pending);
    cJSON_AddBoolToObject(object, "conflict", status->conflict);
    cJSON_AddBoolToObject(object, "fail_closed", status->fail_closed);
    cJSON_AddBoolToObject(object, "detection_only", status->detection_only);
    cJSON_AddNumberToObject(object, "evaluated_ms", status->evaluated_ms);
    cJSON_AddNumberToObject(object, "successful_reads", status->successful_reads);
    cJSON_AddNumberToObject(object, "failed_reads", status->failed_reads);
    cJSON_AddNumberToObject(object, "last_error", status->last_error);
    cJSON_AddStringToObject(object, "mode_a_limitation", status->limitation_text);

    cJSON *single = cJSON_AddObjectToObject(object, "single_input_evidence");
    cJSON_AddBoolToObject(single, "has_sample", status->single_has_sample);
    cJSON_AddNumberToObject(single, "raw_value", status->single_raw_value);
    cJSON_AddNumberToObject(single, "age_ms", status->single_age_ms);

    cJSON *dual = cJSON_AddObjectToObject(object, "dual_meter_evidence");
    cJSON_AddBoolToObject(dual, "grid_has_sample", status->grid_has_sample);
    add_float_or_null(dual, "grid_power_kw", status->grid_power_kw);
    cJSON_AddNumberToObject(dual, "grid_age_ms", status->grid_age_ms);
    cJSON_AddBoolToObject(dual, "generator_has_sample",
                          status->generator_has_sample);
    add_float_or_null(dual, "generator_power_kw", status->generator_power_kw);
    cJSON_AddNumberToObject(dual, "generator_age_ms", status->generator_age_ms);
}

static void add_meter_options(cJSON *root, const app_config_t *config)
{
    cJSON *array = cJSON_AddArrayToObject(root, "meters");
    for (uint8_t index = 0U; index < config->meter_count && index < APP_MAX_METERS;
         ++index) {
        cJSON *meter = cJSON_CreateObject();
        cJSON_AddNumberToObject(meter, "index", index);
        cJSON_AddStringToObject(meter, "name", config->meters[index].name);
        cJSON_AddBoolToObject(meter, "enabled", config->meters[index].enabled);
        cJSON_AddItemToArray(array, meter);
    }
}

static esp_err_t source_detection_get(httpd_req_t *request)
{
    source_detection_config_t config;
    source_detection_status_t status;
    /* app_config_t is ~2.5 kB and the httpd task stack is shared by every
     * handler, so this snapshot goes on the heap rather than the stack. */
    app_config_t *app_config = calloc(1U, sizeof(*app_config));
    if (!app_config) return httpd_resp_send_500(request);
    if (source_detection_config_get_snapshot(&config) != ESP_OK ||
        source_detection_get_status(&status) != ESP_OK ||
        config_manager_get_snapshot(app_config) != ESP_OK) {
        free(app_config);
        return send_json_error(request, "500 Internal Server Error",
                               "Source-detection state is unavailable");
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(app_config);
        return httpd_resp_send_500(request);
    }
    add_config_json(root, &config);
    add_status_json(root, &status);
    add_meter_options(root, app_config);
    free(app_config);
    cJSON_AddBoolToObject(root, "automatic_control_enabled_by_this_phase", false);
    cJSON_AddStringToObject(root, "preferred_topology", "dual_meter");

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return httpd_resp_send_500(request);
    const esp_err_t error = send_json_text(request, NULL, json);
    free(json);
    return error;
}

static bool read_u32(cJSON *object, const char *name, uint32_t minimum,
                     uint32_t maximum, uint32_t *value,
                     char *error, size_t error_size)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!item) return true;
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
        floor(item->valuedouble) != item->valuedouble ||
        item->valuedouble < minimum || item->valuedouble > maximum) {
        snprintf(error, error_size, "%s is outside its valid integer range", name);
        return false;
    }
    *value = (uint32_t)item->valuedouble;
    return true;
}

static bool read_float(cJSON *object, const char *name, float *value,
                       char *error, size_t error_size)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!item) return true;
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
        item->valuedouble <= 0.0) {
        snprintf(error, error_size, "%s must be a finite value greater than zero", name);
        return false;
    }
    *value = (float)item->valuedouble;
    if (!isfinite(*value)) {
        /* A finite double can still overflow float. Without this the caller
         * returned 400 with an empty error string. */
        snprintf(error, error_size, "%s is outside the supported numeric range", name);
        return false;
    }
    return true;
}

static bool parse_config(cJSON *root, source_detection_config_t *config,
                         char *error, size_t error_size)
{
    if (!cJSON_IsObject(root) || !config) {
        strlcpy(error, "Source-detection configuration must be a JSON object",
                error_size);
        return false;
    }

    uint32_t value = config->mode;
    if (!read_u32(root, "mode", SOURCE_DETECTION_MODE_DISABLED,
                  SOURCE_DETECTION_MODE_DUAL_METER, &value,
                  error, error_size)) return false;
    config->mode = (uint8_t)value;

    value = config->debounce_ms;
    if (!read_u32(root, "debounce_ms", 0U, UINT32_MAX, &value,
                  error, error_size)) return false;
    config->debounce_ms = value;

    value = config->stale_timeout_ms;
    if (!read_u32(root, "stale_timeout_ms", 0U, UINT32_MAX, &value,
                  error, error_size)) return false;
    config->stale_timeout_ms = value;

    cJSON *single = cJSON_GetObjectItemCaseSensitive(root, "single_input");
    if (cJSON_IsObject(single)) {
        value = config->single.meter_index;
        if (!read_u32(single, "meter_index", 0U, UINT8_MAX, &value,
                      error, error_size)) return false;
        config->single.meter_index = (uint8_t)value;

        value = config->single.function_code;
        if (!read_u32(single, "function", 0U, 4U, &value,
                      error, error_size)) return false;
        config->single.function_code = (uint8_t)value;

        value = config->single.address_base;
        if (!read_u32(single, "address_base", 0U, UINT8_MAX, &value,
                      error, error_size)) return false;
        config->single.address_base = (uint8_t)value;

        value = config->single.register_address;
        if (!read_u32(single, "register", 0U, UINT16_MAX, &value,
                      error, error_size)) return false;
        config->single.register_address = (uint16_t)value;

        value = config->single.grid_value;
        if (!read_u32(single, "grid_value", 0U, UINT16_MAX, &value,
                      error, error_size)) return false;
        config->single.grid_value = (uint16_t)value;

        value = config->single.generator_value;
        if (!read_u32(single, "generator_value", 0U, UINT16_MAX, &value,
                      error, error_size)) return false;
        config->single.generator_value = (uint16_t)value;
    }

    cJSON *dual = cJSON_GetObjectItemCaseSensitive(root, "dual_meter");
    if (cJSON_IsObject(dual)) {
        value = config->dual.grid_meter_index;
        if (!read_u32(dual, "grid_meter_index", 0U, UINT8_MAX, &value,
                      error, error_size)) return false;
        config->dual.grid_meter_index = (uint8_t)value;

        value = config->dual.generator_meter_index;
        if (!read_u32(dual, "generator_meter_index", 0U, UINT8_MAX, &value,
                      error, error_size)) return false;
        config->dual.generator_meter_index = (uint8_t)value;

        if (!read_float(dual, "grid_threshold_kw",
                        &config->dual.grid_threshold_kw, error, error_size) ||
            !read_float(dual, "generator_threshold_kw",
                        &config->dual.generator_threshold_kw, error, error_size)) {
            return false;
        }

        /* Absent leaves whatever was commissioned, and a plant that has never
         * been asked stays false -- two live sources is a fault until somebody
         * states that this site synchronises. */
        cJSON *sync = cJSON_GetObjectItemCaseSensitive(dual, "sync_capable");
        if (sync) {
            if (!cJSON_IsBool(sync)) {
                strlcpy(error, "dual_meter.sync_capable must be true or false", error_size);
                return false;
            }
            config->dual.sync_capable = cJSON_IsTrue(sync) ? 1U : 0U;
        }
    }

    config->magic = SOURCE_DETECTION_CONFIG_MAGIC;
    config->version = SOURCE_DETECTION_CONFIG_VERSION;

    /*
     * SEEDED FROM THE COMMISSIONED ROLES WHEN THE REQUEST DOES NOT NAME THEM.
     *
     * Selecting the two-meter topology in the commissioning wizard sends the
     * MODE and nothing else -- it has no field for "which meter is the grid",
     * because the engineer already answered that when they set each meter's
     * role. The validator, correctly, refuses a dual configuration with no
     * meters in it, so every such save was rejected with "Configuration is
     * incomplete" and the topology silently stayed on single-input.
     *
     * Observed on the bench: a plant switched to two meters with its meter set
     * to the generator role, and the plant overview still reporting GRID with
     * the generator's power under it. The wizard showed the refusal in a status
     * line that is easy to miss, so the setting looked as though it had been
     * accepted.
     *
     * Only ever fills what the request left unset, so an explicit index in the
     * body still wins. The role remains the source of truth at evaluation time
     * -- see resolve_dual_meters() in source_detection.c -- so a role changed
     * later is followed without another save here.
     */
    if (config->mode == SOURCE_DETECTION_MODE_DUAL_METER) {
        app_config_t *snapshot = calloc(1, sizeof(*snapshot));
        if (snapshot && config_manager_get_snapshot(snapshot) == ESP_OK) {
            const meter_role_assignment_t roles =
                config_manager_role_assignment(snapshot);
            if (roles.valid) {
                if (config->dual.grid_meter_index == SOURCE_DETECTION_METER_INDEX_NONE &&
                    roles.grid_index != METER_ROLE_INDEX_NONE) {
                    config->dual.grid_meter_index = roles.grid_index;
                }
                if (config->dual.generator_meter_index == SOURCE_DETECTION_METER_INDEX_NONE &&
                    roles.generator_count > 0U) {
                    config->dual.generator_meter_index = roles.generator_index[0];
                }
            }
        }
        free(snapshot);
    }

    if (!source_detection_config_valid(config)) {
        strlcpy(error,
                "Configuration is incomplete: select a topology, valid enabled meters, site-verified function/address convention, and non-zero debounce and stale timeouts",
                error_size);
        return false;
    }
    return true;
}

static bool enabled_meter(const app_config_t *config, uint8_t index)
{
    return config && index < config->meter_count && index < APP_MAX_METERS &&
           config->meters[index].enabled;
}

static bool cross_validate_meters(const source_detection_config_t *config,
                                  const app_config_t *app_config,
                                  char *error, size_t error_size)
{
    if (config->mode == SOURCE_DETECTION_MODE_DISABLED) return true;
    if (config->mode == SOURCE_DETECTION_MODE_SINGLE_INPUT) {
        if (!enabled_meter(app_config, config->single.meter_index)) {
            strlcpy(error, "Mode A requires one enabled configured EM500 meter",
                    error_size);
            return false;
        }
        return true;
    }
    if (!enabled_meter(app_config, config->dual.grid_meter_index) ||
        !enabled_meter(app_config, config->dual.generator_meter_index)) {
        strlcpy(error,
                "Mode B requires separate enabled grid and generator EM500 meters",
                error_size);
        return false;
    }
    return true;
}

static esp_err_t source_detection_post(httpd_req_t *request)
{
    cJSON *root = NULL;
    const esp_err_t read_error = http_json_parse_bounded(
        request, SOURCE_DETECTION_MAX_BODY,
        SOURCE_DETECTION_BODY_DEADLINE_MS,
        SOURCE_DETECTION_JSON_MAX_DEPTH, &root);
    if (read_error == ESP_ERR_INVALID_SIZE) {
        return send_json_error(request, "413 Payload Too Large",
                               "Source-detection body is missing or too large");
    }
    if (read_error == ESP_ERR_TIMEOUT) {
        return send_json_error(request, "408 Request Timeout",
                               "Source-detection body timed out");
    }
    if (read_error != ESP_OK) {
        return send_json_error(request, "400 Bad Request",
                               "Source-detection body must be valid bounded JSON");
    }

    source_detection_config_t config;
    /* Heap rather than the shared httpd stack; see source_detection_get(). */
    app_config_t *app_config = calloc(1U, sizeof(*app_config));
    if (!app_config) {
        cJSON_Delete(root);
        return httpd_resp_send_500(request);
    }
    if (source_detection_config_get_snapshot(&config) != ESP_OK ||
        config_manager_get_snapshot(app_config) != ESP_OK) {
        cJSON_Delete(root);
        free(app_config);
        return send_json_error(request, "500 Internal Server Error",
                               "Current configuration is unavailable");
    }

    char validation_error[256] = {0};
    const bool parsed = parse_config(root, &config,
                                     validation_error, sizeof(validation_error));
    cJSON_Delete(root);
    const bool valid = parsed && cross_validate_meters(&config, app_config,
                                                       validation_error,
                                                       sizeof(validation_error));
    free(app_config);
    if (!valid) {
        return send_json_error(request, "400 Bad Request", validation_error);
    }

    const esp_err_t error = source_detection_config_save(&config);
    if (error != ESP_OK) {
        return send_json_error(request, "500 Internal Server Error",
                               "Source-detection configuration could not be persisted and verified");
    }
    source_detection_notify_config_changed();
    return send_json_text(
        request, "202 Accepted",
        "{\"saved\":true,\"persisted\":true,\"restart_required\":false,\"detection_only\":true,\"automatic_control_enabled\":false}");
}

esp_err_t source_detection_api_register(httpd_handle_t server)
{
    if (!server) return ESP_ERR_INVALID_ARG;
    const httpd_uri_t handlers[] = {
        {.uri = "/api/source-detection", .method = HTTP_GET,
         .handler = source_detection_get},
        {.uri = "/api/source-detection", .method = HTTP_POST,
         .handler = source_detection_post},
    };
    for (size_t index = 0U; index < sizeof(handlers) / sizeof(handlers[0]); ++index) {
        const esp_err_t error = httpd_register_uri_handler(server, &handlers[index]);
        if (error != ESP_OK) return error;
    }
    return ESP_OK;
}
