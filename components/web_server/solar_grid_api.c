#include "solar_grid_api.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "cJSON.h"
#include "config_manager.h"
#include "control_engine.h"
#include "esp_check.h"
#include "http_json.h"
#include "solar_grid_config.h"

#define SOLAR_GRID_BODY_MAX 4096U
#define SOLAR_GRID_BODY_DEADLINE_MS 5000U
#define SOLAR_GRID_JSON_MAX_DEPTH 8U

/* Bounds mirror solar_grid_config_valid() exactly. They are duplicated here only
 * so a rejection can name the offending field; solar_grid_config_valid() stays
 * the authority and is still applied to the whole candidate before persistence. */
#define SOLAR_GRID_KW_MAX 1000000.0f
#define SOLAR_GRID_PERCENT_MAX 100.0f
#define SOLAR_GRID_FIELD_ERROR_MAX 160U

static esp_err_t send_root(httpd_req_t *request, cJSON *root, const char *status)
{
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return httpd_resp_send_500(request);
    if (status) httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    esp_err_t error = httpd_resp_sendstr(request, json);
    free(json);
    return error;
}

static esp_err_t send_error(httpd_req_t *request, const char *status,
                            const char *message)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddStringToObject(root, "error", message);
    return send_root(request, root, status);
}

static void signal_to_json(cJSON *parent, const char *name,
                           const solar_grid_signal_config_t *signal)
{
    cJSON *object = cJSON_AddObjectToObject(parent, name);
    cJSON_AddBoolToObject(object, "enabled", signal->enabled);
    cJSON_AddNumberToObject(object, "meter_index", signal->meter_index);
    cJSON_AddNumberToObject(object, "function", signal->function_code);
    cJSON_AddNumberToObject(object, "address", signal->address);
    cJSON_AddNumberToObject(object, "mask", signal->mask);
    cJSON_AddNumberToObject(object, "active_value", signal->active_value);
}

static cJSON *config_json(const solar_grid_config_t *config)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON_AddNumberToObject(root, "schema", config->version);
    cJSON_AddNumberToObject(root, "policy", config->policy);
    cJSON_AddStringToObject(root, "policy_name",
                            solar_grid_policy_name(config->policy));
    cJSON_AddNumberToObject(root, "meter_orientation", config->meter_orientation);
    cJSON_AddStringToObject(root, "meter_orientation_name",
                            solar_grid_orientation_name(config->meter_orientation));
    cJSON_AddNumberToObject(root, "export_limit_kw", config->export_limit_kw);
    cJSON_AddNumberToObject(root, "minimum_import_kw", config->minimum_import_kw);
    signal_to_json(root, "grid_available", &config->grid_available);
    signal_to_json(root, "grid_breaker_closed", &config->grid_breaker_closed);
    cJSON_AddNumberToObject(root, "evidence_poll_interval_ms",
                            config->evidence_poll_interval_ms);
    cJSON_AddNumberToObject(root, "evidence_stale_timeout_ms",
                            config->evidence_stale_timeout_ms);
    cJSON_AddNumberToObject(root, "grid_loss_trip_ms",
                            config->grid_loss_trip_ms);
    cJSON_AddNumberToObject(root, "grid_recovery_stable_ms",
                            config->grid_recovery_stable_ms);
    cJSON_AddNumberToObject(root, "generator_rated_kw", config->generator_rated_kw);
    cJSON_AddNumberToObject(root, "generator_minimum_loading_percent",
                            config->generator_minimum_loading_percent);
    cJSON_AddNumberToObject(root, "generator_reserve_kw",
                            config->generator_reserve_kw);
    cJSON_AddNumberToObject(root, "generator_reverse_power_margin_kw",
                            config->generator_reverse_power_margin_kw);
    cJSON_AddBoolToObject(root, "generator_commissioned",
                          config->generator_rated_kw > 0.0f);

    /* Strong source evidence is configuration, not a hard-coded plant model.
     * Every signal is read back exactly as commissioned. A disabled signal has
     * no implied address/polarity and is never treated as proof of a contact. */
    signal_to_json(root, "generator_running", &config->generator_running);
    signal_to_json(root, "generator_breaker_closed", &config->generator_breaker_closed);
    signal_to_json(root, "transfer_active", &config->transfer_active);
    signal_to_json(root, "grid_generator_synchronized",
                   &config->grid_generator_synchronized);
    cJSON_AddBoolToObject(root, "grid_evidence_complete",
                          solar_grid_config_evidence_complete(config));
    cJSON_AddBoolToObject(root, "generator_evidence_complete",
                          solar_grid_config_generator_evidence_complete(config));
    cJSON_AddBoolToObject(root, "evidence_complete",
                          solar_grid_config_evidence_complete(config) &&
                          solar_grid_config_generator_evidence_complete(config));
    cJSON_AddBoolToObject(root, "control_requires_restart", true);
    return root;
}

static bool read_bool(cJSON *object, const char *key, bool *value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!item) return true;
    if (!cJSON_IsBool(item)) return false;
    *value = cJSON_IsTrue(item);
    return true;
}

static bool read_uint(cJSON *object, const char *key, uint32_t maximum,
                      uint32_t *value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!item) return true;
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
        floor(item->valuedouble) != item->valuedouble ||
        item->valuedouble < 0.0 || item->valuedouble > maximum) {
        return false;
    }
    *value = (uint32_t)item->valuedouble;
    return true;
}

static bool read_float(cJSON *object, const char *key, float *value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!item) return true;
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) return false;
    *value = (float)item->valuedouble;
    return isfinite(*value);
}

static bool read_limit(cJSON *object, const char *key, float maximum,
                       float *value, char *error, size_t error_size)
{
    if (!cJSON_GetObjectItemCaseSensitive(object, key)) return true;
    float candidate = *value;
    if (!read_float(object, key, &candidate) || candidate < 0.0f ||
        candidate > maximum) {
        snprintf(error, error_size,
                 "Solar-Grid field '%s' must be a finite number between 0 and %.0f",
                 key, (double)maximum);
        return false;
    }
    *value = candidate;
    return true;
}

static bool parse_signal(cJSON *object, solar_grid_signal_config_t *signal)
{
    if (!cJSON_IsObject(object)) return false;
    uint32_t value = 0U;
    if (!read_bool(object, "enabled", &signal->enabled)) return false;
    value = signal->meter_index;
    if (!read_uint(object, "meter_index", UINT8_MAX, &value)) return false;
    signal->meter_index = (uint8_t)value;
    value = signal->function_code;
    if (!read_uint(object, "function", UINT8_MAX, &value)) return false;
    signal->function_code = (uint8_t)value;
    value = signal->address;
    if (!read_uint(object, "address", UINT16_MAX, &value)) return false;
    signal->address = (uint16_t)value;
    value = signal->mask;
    if (!read_uint(object, "mask", UINT16_MAX, &value)) return false;
    signal->mask = (uint16_t)value;
    value = signal->active_value;
    if (!read_uint(object, "active_value", UINT16_MAX, &value)) return false;
    signal->active_value = (uint16_t)value;
    return true;
}

static bool parse_optional_signal(cJSON *root, const char *key,
                                  solar_grid_signal_config_t *signal)
{
    cJSON *object = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!object) return true; /* partial POST preserves commissioned value */
    return parse_signal(object, signal);
}

static esp_err_t config_get(httpd_req_t *request)
{
    solar_grid_config_t config;
    ESP_RETURN_ON_ERROR(solar_grid_config_get_snapshot(&config),
                        "solar_grid_api", "config snapshot failed");
    cJSON *root = config_json(&config);
    return root ? send_root(request, root, NULL) : httpd_resp_send_500(request);
}

static esp_err_t config_post(httpd_req_t *request)
{
    cJSON *root = NULL;
    esp_err_t error = http_json_parse_bounded(request, SOLAR_GRID_BODY_MAX,
                                               SOLAR_GRID_BODY_DEADLINE_MS,
                                               SOLAR_GRID_JSON_MAX_DEPTH,
                                               &root);
    if (error == ESP_ERR_INVALID_SIZE) {
        return send_error(request, "413 Payload Too Large",
                          "Solar-Grid configuration body is missing or too large");
    }
    if (error == ESP_ERR_TIMEOUT) {
        return send_error(request, "408 Request Timeout",
                          "Solar-Grid configuration body timed out");
    }
    if (error != ESP_OK || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request",
                          "Invalid Solar-Grid configuration JSON");
    }

    solar_grid_config_t next;
    error = solar_grid_config_get_snapshot(&next);
    if (error != ESP_OK) {
        cJSON_Delete(root);
        return send_error(request, "500 Internal Server Error",
                          "Current Solar-Grid configuration is unavailable");
    }

    uint32_t value = next.policy;
    bool parsed = read_uint(root, "policy", SOLAR_GRID_POLICY_MINIMUM_IMPORT,
                            &value);
    next.policy = (solar_grid_policy_t)value;
    value = next.meter_orientation;
    parsed = parsed && read_uint(root, "meter_orientation",
                                 SOLAR_GRID_EXPORT_POSITIVE, &value);
    next.meter_orientation = (solar_grid_meter_orientation_t)value;
    parsed = parsed && read_float(root, "export_limit_kw", &next.export_limit_kw) &&
             read_float(root, "minimum_import_kw", &next.minimum_import_kw);

    cJSON *available = cJSON_GetObjectItemCaseSensitive(root, "grid_available");
    cJSON *breaker = cJSON_GetObjectItemCaseSensitive(root, "grid_breaker_closed");
    parsed = parsed && parse_signal(available, &next.grid_available) &&
             parse_signal(breaker, &next.grid_breaker_closed);

    value = next.evidence_poll_interval_ms;
    parsed = parsed && read_uint(root, "evidence_poll_interval_ms", 60000U,
                                 &value);
    next.evidence_poll_interval_ms = value;
    value = next.evidence_stale_timeout_ms;
    parsed = parsed && read_uint(root, "evidence_stale_timeout_ms", 600000U,
                                 &value);
    next.evidence_stale_timeout_ms = value;
    value = next.grid_loss_trip_ms;
    parsed = parsed && read_uint(root, "grid_loss_trip_ms", 60000U, &value);
    next.grid_loss_trip_ms = value;
    value = next.grid_recovery_stable_ms;
    parsed = parsed && read_uint(root, "grid_recovery_stable_ms", 600000U,
                                 &value);
    next.grid_recovery_stable_ms = value;

    char field_error[SOLAR_GRID_FIELD_ERROR_MAX] = {0};
    parsed = parsed &&
             read_limit(root, "generator_rated_kw", SOLAR_GRID_KW_MAX,
                        &next.generator_rated_kw, field_error,
                        sizeof(field_error)) &&
             read_limit(root, "generator_minimum_loading_percent",
                        SOLAR_GRID_PERCENT_MAX,
                        &next.generator_minimum_loading_percent, field_error,
                        sizeof(field_error)) &&
             read_limit(root, "generator_reserve_kw", SOLAR_GRID_KW_MAX,
                        &next.generator_reserve_kw, field_error,
                        sizeof(field_error)) &&
             read_limit(root, "generator_reverse_power_margin_kw",
                        SOLAR_GRID_KW_MAX,
                        &next.generator_reverse_power_margin_kw, field_error,
                        sizeof(field_error));

    parsed = parsed &&
             parse_optional_signal(root, "generator_running",
                                   &next.generator_running) &&
             parse_optional_signal(root, "generator_breaker_closed",
                                   &next.generator_breaker_closed) &&
             parse_optional_signal(root, "transfer_active",
                                   &next.transfer_active) &&
             parse_optional_signal(root, "grid_generator_synchronized",
                                   &next.grid_generator_synchronized);
    cJSON_Delete(root);

    next.magic = SOLAR_GRID_CONFIG_MAGIC;
    next.version = SOLAR_GRID_CONFIG_VERSION;
    if (!parsed || !solar_grid_config_valid(&next)) {
        return send_error(request, "400 Bad Request",
                          field_error[0] ? field_error
                                         : "Solar-Grid configuration validation failed");
    }

    /* A policy, sign, evidence-map or timing change invalidates the active
     * commissioning proof. Persist the new model only after automatic control
     * has been forced disabled in the main configuration. */
    app_config_t *application = malloc(sizeof(*application));
    if (!application) return httpd_resp_send_500(request);
    error = config_manager_get_snapshot(application);
    if (error == ESP_OK) {
        application->control.enabled = false;
        error = config_manager_save(application);
    }
    free(application);
    if (error != ESP_OK) {
        return send_error(request, "500 Internal Server Error",
                          "Automatic control could not be safely disabled");
    }

    /* Latch the already-running control task disabled before changing its
     * persisted source model. This call performs no Modbus or inverter I/O in
     * the HTTP handler; the control task applies safe zero on its next cycle. */
    control_engine_force_disable();

    error = solar_grid_config_save(&next);
    if (error != ESP_OK) {
        return send_error(request, "500 Internal Server Error",
                          "Solar-Grid configuration could not be persisted");
    }

    cJSON *response = config_json(&next);
    if (!response) return httpd_resp_send_500(request);
    cJSON_AddBoolToObject(response, "saved", true);
    cJSON_AddBoolToObject(response, "persisted", true);
    cJSON_AddBoolToObject(response, "control_forced_disabled", true);
    cJSON_AddBoolToObject(response, "restart_required", true);
    return send_root(request, response, NULL);
}

esp_err_t solar_grid_api_register(httpd_handle_t server)
{
    if (!server) return ESP_ERR_INVALID_ARG;
    const httpd_uri_t handlers[] = {
        {.uri = "/api/solar-grid/config", .method = HTTP_GET,
         .handler = config_get},
        {.uri = "/api/solar-grid/config", .method = HTTP_POST,
         .handler = config_post},
    };
    for (size_t index = 0; index < sizeof(handlers) / sizeof(handlers[0]); ++index) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &handlers[index]),
                            "solar_grid_api", "handler registration failed");
    }
    return ESP_OK;
}
