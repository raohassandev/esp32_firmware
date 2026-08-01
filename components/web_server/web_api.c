#include "web_api.h"
#include "esp_check.h"
#include "esp_system.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "config_manager.h"
#include "commissioning_gate.h"
#include "control_engine.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_json.h"
#include "meter_manager.h"
#include "network_manager.h"
#include "source_detection.h"
#include "source_detection_engine.h"
#include "system_resource_api.h"
#include "safety_manager.h"

/* Staleness is NOT defined here either. One definition, owned by safety_manager,
 * so the dashboard's quality word cannot say "good" while control is inhibited for
 * staleness -- which a local 5000 ms constant against a configured 1000 ms did. */
#define MASKED_PASSWORD "********"
#define WIFI_CONFIG_MAX_BODY 4096U
#define CONFIG_MAX_BODY 16384U
#define WEB_API_BODY_DEADLINE_MS 5000U
#define WEB_API_JSON_MAX_DEPTH 8U

static esp_err_t send_json_text(httpd_req_t *request, const char *status, const char *text)
{
    if (status) httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, text);
}

static esp_err_t send_json_error(httpd_req_t *request, const char *status, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddStringToObject(root, "error", message ? message : "Request failed");
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return httpd_resp_send_500(request);
    esp_err_t err = send_json_text(request, status, json);
    free(json);
    return err;
}

static esp_err_t status_get(httpd_req_t *request)
{
    meter_data_t meter = {0};
    control_status_t control = {0};
    network_status_t network = {0};
    meter_manager_get_data(0, &meter);
    control_engine_get_status(&control);
    network_manager_get_status(&network);

    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    cJSON_AddBoolToObject(root, "network_online", network.network_ready);
    cJSON_AddNumberToObject(root, "wifi_state", network.state);
    cJSON_AddStringToObject(root, "ssid", network.ssid);
    cJSON_AddStringToObject(root, "ip", network.ip);
    cJSON_AddStringToObject(root, "gateway", network.gateway);
    cJSON_AddStringToObject(root, "netmask", network.netmask);
    cJSON_AddNumberToObject(root, "rssi", network.rssi);
    cJSON_AddBoolToObject(root, "using_fallback_sta", network.using_fallback_sta);
    cJSON_AddBoolToObject(root, "fallback_ap_active", network.fallback_ap_active);
    /* Name and channel so an engineer knows what to look for. The passphrase is
     * never carried in an HTTP response; it is disclosed on the serial console
     * at boot and nowhere else. */
    cJSON_AddStringToObject(root, "recovery_ap_ssid", network.ap_ssid);
    cJSON_AddNumberToObject(root, "recovery_ap_channel", network.ap_channel);
    cJSON_AddStringToObject(root, "mdns_hostname", network.hostname);
    cJSON_AddNumberToObject(root, "disconnect_count", network.disconnect_count);

    /*
     * WHICH SOURCE IS CARRYING THE PLANT.
     *
     * This was missing entirely, and the consequence was on the home page: the
     * controller had resolved GENERATOR from the EM-500 tariff input, and the
     * power-flow diagram drew the measured 347 kW under GRID with the generator
     * dimmed and captioned "not running".
     *
     * The diagram was not at fault. On a single-meter tariff plant ONE meter
     * measures whichever source is live -- the number is the same, and what it
     * MEANS changes with the tariff input. Without the source in this payload
     * every reader had no choice but to call it "grid", and was wrong whenever
     * the generator was running.
     *
     * PUBLISHED WITH ITS CONFIDENCE, NOT AS A BARE LABEL. A reader must be able
     * to tell "the controller knows this is the generator" from "the controller
     * cannot establish the source", because the honest rendering of the second
     * is not a guess -- it is saying so. attributed_to is exactly that: the node
     * the measurement may be drawn under, or "unknown" when nothing may be
     * claimed.
     */
    source_detection_status_t source = {0};
    const bool have_source = source_detection_get_status(&source) == ESP_OK;
    cJSON *source_json = cJSON_AddObjectToObject(root, "source");
    cJSON_AddBoolToObject(source_json, "available", have_source);
    if (have_source) {
        cJSON_AddNumberToObject(source_json, "mode", source.mode);
        cJSON_AddStringToObject(source_json, "state", source_detection_state_name(source.state));
        cJSON_AddBoolToObject(source_json, "configured", source.configured);
        cJSON_AddBoolToObject(source_json, "evidence_fresh", source.evidence_fresh);
        cJSON_AddBoolToObject(source_json, "fail_closed", source.fail_closed);
        cJSON_AddBoolToObject(source_json, "conflict", source.conflict);
        cJSON_AddStringToObject(source_json, "reason", source.reason_text);

        /*
         * The single field a screen needs, so no page has to re-derive the rule
         * and none of them can derive it differently.
         *
         * "unknown" unless the source is BOTH resolved and backed by fresh
         * evidence. A stale tariff read that last said generator is not a
         * statement about the plant now, and drawing 347 kW under a node on the
         * strength of it is the same error in the other direction.
         */
        const bool trustworthy = source.configured && source.evidence_fresh &&
                                 !source.conflict &&
                                 (source.state == SOURCE_STATE_GRID ||
                                  source.state == SOURCE_STATE_GENERATOR);
        cJSON_AddStringToObject(source_json, "attributed_to",
                                !trustworthy ? "unknown"
                                : source.state == SOURCE_STATE_GENERATOR ? "generator"
                                : "grid");
    } else {
        cJSON_AddStringToObject(source_json, "attributed_to", "unknown");
    }

    /*
     * THE CONTROLLER'S OWN HEALTH, WHERE AN OPERATOR WILL SEE IT.
     *
     * A small factory cannot read a heap fragmentation ratio and does not need
     * to. It needs to know the controller has been running for eleven days and
     * did not fall over in the night -- which is the difference between trusting
     * the box on the wall and not.
     *
     * Three fields, from the same rule the engineering resource page uses, so
     * the two can never disagree about the same controller. Everything else in
     * that report -- chip revision, partitions, block sizes -- stays gated,
     * because it is detail about HOW the controller works rather than WHETHER it
     * is working.
     */
    const system_resource_health_t health = system_resource_health();
    cJSON *controller = cJSON_AddObjectToObject(root, "controller");
    cJSON_AddNumberToObject(controller, "uptime_ms", (double)health.uptime_ms);
    cJSON_AddStringToObject(controller, "state", health.state);
    /* The one that matters: a controller that restarted by itself did so for a
     * reason, and nobody was watching when it happened. */
    cJSON_AddBoolToObject(controller, "last_reboot_unexpected", health.last_reboot_unexpected);
    cJSON_AddNumberToObject(root, "reconnect_count", network.reconnect_count);

    uint32_t current_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    bool meter_has_data = meter.last_update_ms != 0 && isfinite(meter.active_power_kw);
    uint32_t meter_age_ms = meter_has_data ? current_ms - meter.last_update_ms : 0;
    cJSON_AddBoolToObject(root, "meter_online", meter.online);
    cJSON_AddBoolToObject(root, "meter_has_data", meter_has_data);
    cJSON_AddBoolToObject(root, "meter_stale", !meter_has_data || meter_age_ms > safety_manager_meter_stale_timeout_ms());
    if (meter_has_data) {
        cJSON_AddNumberToObject(root, "meter_age_ms", (double)meter_age_ms);
        cJSON_AddNumberToObject(root, "grid_power_kw", meter.active_power_kw);
    } else {
        cJSON_AddNullToObject(root, "meter_age_ms");
        cJSON_AddNullToObject(root, "grid_power_kw");
    }
    cJSON_AddNumberToObject(root, "meter_errors", meter.response_errors);

    /* Provenance for the grid measurement. Without it the same signal read at
     * two different instants looks like two disagreeing values, and an operator
     * has no way to tell which one the controller is acting on. Every live value
     * should say where it came from, how old it is and whether it can be
     * trusted. The flat fields above are kept so existing callers still work. */
    {
        app_config_t *config = calloc(1U, sizeof(*config));
        cJSON *m = cJSON_AddObjectToObject(root, "grid_measurement");
        if (m) {
            const char *quality = !meter.online      ? "unavailable"
                                  : !meter_has_data  ? "unavailable"
                                  : meter.degraded   ? "degraded"
                                  : meter_age_ms > safety_manager_meter_stale_timeout_ms() ? "stale"
                                                                        : "good";
            cJSON_AddStringToObject(m, "quantity", "Grid active power");
            cJSON_AddStringToObject(m, "unit", "kW");
            /* Measured, not requested or commanded - the distinction matters on
             * a controller that also publishes setpoints. */
            cJSON_AddStringToObject(m, "kind", "measured");
            cJSON_AddStringToObject(m, "quality", quality);
            if (meter_has_data) {
                cJSON_AddNumberToObject(m, "value", meter.active_power_kw);
                cJSON_AddNumberToObject(m, "age_ms", (double)meter_age_ms);
            } else {
                cJSON_AddNullToObject(m, "value");
                cJSON_AddNullToObject(m, "age_ms");
            }
            if (config && config_manager_get_snapshot(config) == ESP_OK) {
                const meter_role_assignment_t roles = config_manager_role_assignment(config);
                if (roles.grid_index != METER_ROLE_INDEX_NONE &&
                    roles.grid_index < config->meter_count) {
                    cJSON_AddStringToObject(m, "source", config->meters[roles.grid_index].name);
                    cJSON_AddNumberToObject(m, "source_index", roles.grid_index);
                    cJSON_AddNumberToObject(m, "unit_id",
                                            config->meters[roles.grid_index].endpoint.unit_id);
                } else {
                    /* No single grid meter resolved: say so rather than naming
                     * an arbitrary instrument. */
                    cJSON_AddNullToObject(m, "source");
                    cJSON_AddNullToObject(m, "source_index");
                    cJSON_AddNullToObject(m, "unit_id");
                }
                cJSON_AddBoolToObject(m, "role_assignment_valid", roles.valid);
            }
        }
        free(config);
    }
    cJSON_AddBoolToObject(root, "control_enabled", control.enabled);
    cJSON_AddNumberToObject(root, "mode", control.mode);
    cJSON_AddNumberToObject(root, "requested_pv_kw", control.requested_pv_kw);
    cJSON_AddNumberToObject(root, "applied_pv_kw", control.applied_pv_kw);

    /* One authoritative statement of control authority. "Monitoring",
     * "Monitoring only", "Automatic control locked" and "No commands issued"
     * were separate phrases in separate places that may or may not have meant
     * the same thing; an operator of a device that writes inverter power limits
     * has to be able to read one answer. The inhibit reason is the firmware's
     * own wording, so the interface never paraphrases a safety decision. */
    {
        cJSON *authority = cJSON_AddObjectToObject(root, "control_authority");
        if (authority) {
            const bool commanding = control.command_authority;
            cJSON_AddStringToObject(authority, "mode",
                                    !control.enabled ? "monitoring_only"
                                    : commanding     ? "commanding"
                                                     : "inhibited");
            cJSON_AddStringToObject(authority, "mode_label",
                                    !control.enabled ? "Monitoring only"
                                    : commanding     ? "Commanding"
                                                     : "Inhibited");
            cJSON_AddBoolToObject(authority, "control_enabled", control.enabled);
            cJSON_AddBoolToObject(authority, "command_authority", commanding);
            cJSON_AddStringToObject(authority, "inhibit_reason", control.inhibit_reason);
            /* Commissioning scope is published on this PUBLIC endpoint on purpose.
             * The detailed gate lives behind the engineering gateway, but "the
             * machines being commanded are declared simulators" is not an
             * engineering detail -- it is the single fact that stops an operator
             * reading a lab run as real plant control. Withholding it from
             * unauthenticated clients would leave exactly the people most likely
             * to be misled with no way to know, which defeats the purpose of
             * marking lab mode at all.
             *
             * It leaks no secret: it says whether this controller is commissioned
             * and whether its targets are real, which any operator watching the
             * plant is entitled to know. */
            cJSON_AddStringToObject(authority, "commissioning_scope",
                                    commissioning_scope_label(
                                        (commissioning_scope_t)control.commissioning_scope));
            cJSON_AddBoolToObject(authority, "lab_simulator_mode",
                                  (commissioning_scope_t)control.commissioning_scope ==
                                      COMMISSIONING_SCOPE_LAB);
            cJSON_AddBoolToObject(authority, "commissioned", control.commissioned);
            cJSON_AddNumberToObject(authority, "requested_pv_kw", control.requested_pv_kw);
            cJSON_AddNumberToObject(authority, "applied_pv_kw", control.applied_pv_kw);
            /* Null rather than zero when nothing has happened yet: zero would
             * read as "just now". */
            if (control.last_command_ms != 0U) {
                cJSON_AddNumberToObject(authority, "last_command_age_ms",
                                        (double)(current_ms - control.last_command_ms));
            } else {
                cJSON_AddNullToObject(authority, "last_command_age_ms");
            }
            if (control.last_authority_change_ms != 0U) {
                cJSON_AddNumberToObject(authority, "last_change_age_ms",
                                        (double)(current_ms - control.last_authority_change_ms));
            } else {
                cJSON_AddNullToObject(authority, "last_change_age_ms");
            }
        }
    }

    uint32_t alarms = safety_manager_get_alarm_flags();
    cJSON_AddNumberToObject(root, "alarms", alarms);
    cJSON *alarm_names = cJSON_AddArrayToObject(root, "alarm_names");
    if (alarms & SAFETY_ALARM_METER_OFFLINE) cJSON_AddItemToArray(alarm_names, cJSON_CreateString("Meter offline"));
    if (alarms & SAFETY_ALARM_METER_STALE) cJSON_AddItemToArray(alarm_names, cJSON_CreateString("Meter data stale"));

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return ESP_ERR_NO_MEM;
    esp_err_t err = send_json_text(request, NULL, json);
    free(json);
    return err;
}

static esp_err_t config_get(httpd_req_t *request)
{
    char *json = NULL;
    ESP_RETURN_ON_ERROR(config_manager_export_json(&json), "web_api", "config export failed");
    esp_err_t err = send_json_text(request, NULL, json);
    free(json);
    return err;
}

static esp_err_t config_post(httpd_req_t *request)
{
    char *body = NULL;
    esp_err_t read_err = http_body_read_bounded(request, CONFIG_MAX_BODY,
                                                 WEB_API_BODY_DEADLINE_MS, &body);
    if (read_err == ESP_ERR_INVALID_SIZE) {
        return send_json_error(request, "413 Payload Too Large",
                               "Configuration body is missing or too large");
    }
    if (read_err == ESP_ERR_TIMEOUT) {
        return send_json_error(request, "408 Request Timeout",
                               "Configuration body timed out");
    }
    if (read_err != ESP_OK) {
        return send_json_error(request, "400 Bad Request", "Invalid configuration body");
    }

    esp_err_t err = config_manager_import_json(body);
    free(body);
    if (err != ESP_OK) {
        return send_json_error(request, "400 Bad Request", "Configuration validation or persistence failed");
    }
    return send_json_text(request, NULL, "{\"saved\":true,\"persisted\":true,\"restart_required\":true}");
}

static bool parse_ipv4(const char *text, uint32_t *out_value)
{
    if (!text || !text[0]) return false;
    unsigned a = 0, b = 0, c = 0, d = 0;
    char tail = '\0';
    if (sscanf(text, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4 ||
        a > 255 || b > 255 || c > 255 || d > 255) {
        return false;
    }
    if (out_value) *out_value = (a << 24) | (b << 16) | (c << 8) | d;
    return true;
}

static bool valid_netmask(const char *text, uint32_t *out_mask)
{
    uint32_t mask = 0;
    if (!parse_ipv4(text, &mask) || mask == 0 || mask == UINT32_MAX) return false;
    uint32_t inverted = ~mask;
    if (((inverted + 1U) & inverted) != 0) return false;
    if (out_mask) *out_mask = mask;
    return true;
}

static bool valid_static_profile(const app_wifi_sta_profile_t *profile)
{
    if (profile->ip_mode == APP_WIFI_IP_DHCP) return true;

    uint32_t ip = 0, gateway = 0, mask = 0;
    if (!parse_ipv4(profile->static_ip, &ip) ||
        !parse_ipv4(profile->gateway, &gateway) ||
        !valid_netmask(profile->netmask, &mask)) {
        return false;
    }
    if ((ip & mask) != (gateway & mask) || ip == gateway) return false;

    uint32_t network = ip & mask;
    uint32_t broadcast = network | ~mask;
    if (ip == network || ip == broadcast || gateway == network || gateway == broadcast) return false;

    if (profile->dns1[0] && !parse_ipv4(profile->dns1, NULL)) return false;
    if (profile->dns2[0] && !parse_ipv4(profile->dns2, NULL)) return false;
    return true;
}

static bool copy_optional_string(cJSON *object, const char *key, char *destination,
                                 size_t destination_size, char *error, size_t error_size)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!item) return true;
    if (!cJSON_IsString(item) || strlen(item->valuestring) >= destination_size) {
        snprintf(error, error_size, "Invalid %s", key);
        return false;
    }
    strlcpy(destination, item->valuestring, destination_size);
    return true;
}

static bool parse_wifi_profile(cJSON *object, const app_wifi_sta_profile_t *current,
                               app_wifi_sta_profile_t *next, char *error, size_t error_size)
{
    if (!cJSON_IsObject(object) || !current || !next) {
        strlcpy(error, "Invalid Wi-Fi profile", error_size);
        return false;
    }
    *next = *current;

    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, "enabled");
    if (item && !cJSON_IsBool(item)) {
        strlcpy(error, "Profile enabled must be boolean", error_size);
        return false;
    }
    if (cJSON_IsBool(item)) next->enabled = cJSON_IsTrue(item);

    char requested_ssid[sizeof(next->ssid)];
    strlcpy(requested_ssid, current->ssid, sizeof(requested_ssid));
    if (!copy_optional_string(object, "ssid", requested_ssid, sizeof(requested_ssid), error, error_size)) return false;
    bool ssid_changed = strcmp(requested_ssid, current->ssid) != 0;
    strlcpy(next->ssid, requested_ssid, sizeof(next->ssid));

    item = cJSON_GetObjectItemCaseSensitive(object, "clear_password");
    bool clear_password = cJSON_IsTrue(item);
    item = cJSON_GetObjectItemCaseSensitive(object, "password");
    if (item && !cJSON_IsString(item)) {
        strlcpy(error, "Invalid Wi-Fi password", error_size);
        return false;
    }
    if (clear_password || (ssid_changed && (!cJSON_IsString(item) || !item->valuestring[0] || strcmp(item->valuestring, MASKED_PASSWORD) == 0))) {
        next->password[0] = '\0';
    } else if (cJSON_IsString(item) && item->valuestring[0] && strcmp(item->valuestring, MASKED_PASSWORD) != 0) {
        size_t length = strlen(item->valuestring);
        if (length < 8 || length > 64) {
            strlcpy(error, "Wi-Fi password must contain 8-64 characters", error_size);
            return false;
        }
        strlcpy(next->password, item->valuestring, sizeof(next->password));
    }

    item = cJSON_GetObjectItemCaseSensitive(object, "ip_mode");
    if (item && (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
                 floor(item->valuedouble) != item->valuedouble ||
                 item->valueint < APP_WIFI_IP_DHCP || item->valueint > APP_WIFI_IP_STATIC)) {
        strlcpy(error, "Invalid IP mode", error_size);
        return false;
    }
    if (cJSON_IsNumber(item)) next->ip_mode = (app_wifi_ip_mode_t)item->valueint;

    if (!copy_optional_string(object, "static_ip", next->static_ip, sizeof(next->static_ip), error, error_size) ||
        !copy_optional_string(object, "gateway", next->gateway, sizeof(next->gateway), error, error_size) ||
        !copy_optional_string(object, "netmask", next->netmask, sizeof(next->netmask), error, error_size) ||
        !copy_optional_string(object, "dns1", next->dns1, sizeof(next->dns1), error, error_size) ||
        !copy_optional_string(object, "dns2", next->dns2, sizeof(next->dns2), error, error_size)) {
        return false;
    }

    if (next->enabled && !next->ssid[0]) {
        strlcpy(error, "Enabled Wi-Fi profiles require an SSID", error_size);
        return false;
    }
    if (!valid_static_profile(next)) {
        strlcpy(error, "Static IP, gateway or netmask is invalid", error_size);
        return false;
    }
    return true;
}

static bool parse_wifi_config(cJSON *root, const app_wifi_config_t *current,
                              app_wifi_config_t *next, char *error, size_t error_size)
{
    if (!cJSON_IsObject(root) || !current || !next) {
        strlcpy(error, "Invalid Wi-Fi configuration", error_size);
        return false;
    }
    *next = *current;

    if (!parse_wifi_profile(cJSON_GetObjectItemCaseSensitive(root, "primary"),
                            &current->primary, &next->primary, error, error_size) ||
        !parse_wifi_profile(cJSON_GetObjectItemCaseSensitive(root, "fallback"),
                            &current->fallback, &next->fallback, error, error_size)) {
        return false;
    }

    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "scan_before_connect");
    if (item && !cJSON_IsBool(item)) {
        strlcpy(error, "scan_before_connect must be boolean", error_size);
        return false;
    }
    if (cJSON_IsBool(item)) next->scan_before_connect = cJSON_IsTrue(item);

    item = cJSON_GetObjectItemCaseSensitive(root, "fallback_ap_enabled");
    if (item && !cJSON_IsBool(item)) {
        strlcpy(error, "fallback_ap_enabled must be boolean", error_size);
        return false;
    }
    if (cJSON_IsBool(item) && !cJSON_IsTrue(item)) {
        /* The recovery AP is the controller's guaranteed way in after it is
         * moved to a site whose Wi-Fi it does not know. Switching it off is
         * indistinguishable from bricking the unit remotely, so the request is
         * refused rather than quietly ignored. */
        strlcpy(error, "The recovery access point cannot be switched off; it is always on",
                error_size);
        return false;
    }
    next->fallback_ap_enabled = true;

    char requested_ap_ssid[sizeof(next->fallback_ap_ssid)];
    strlcpy(requested_ap_ssid, current->fallback_ap_ssid, sizeof(requested_ap_ssid));
    if (!copy_optional_string(root, "fallback_ap_ssid", requested_ap_ssid,
                              sizeof(requested_ap_ssid), error, error_size)) return false;
    strlcpy(next->fallback_ap_ssid, requested_ap_ssid, sizeof(next->fallback_ap_ssid));

    item = cJSON_GetObjectItemCaseSensitive(root, "fallback_ap_password");
    if (item && !cJSON_IsString(item)) {
        strlcpy(error, "Invalid recovery AP password", error_size);
        return false;
    }
    /* Renaming the AP used to clear its passphrase, which took the network open
     * on the next boot. The stored passphrase is now only ever REPLACED, never
     * cleared: an unsecured always-on AP on an industrial controller is not a
     * state this API is allowed to produce. config_manager_save() refuses one
     * regardless. */
    if (cJSON_IsString(item) && item->valuestring[0] && strcmp(item->valuestring, MASKED_PASSWORD) != 0) {
        size_t length = strlen(item->valuestring);
        if (length < 8 || length > 64) {
            strlcpy(error, "Recovery AP password must contain 8-64 characters", error_size);
            return false;
        }
        strlcpy(next->fallback_ap_password, item->valuestring, sizeof(next->fallback_ap_password));
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "max_retries_per_profile");
    if (item && (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
                 floor(item->valuedouble) != item->valuedouble ||
                 item->valueint < 1 || item->valueint > 20)) {
        strlcpy(error, "Retries must be between 1 and 20", error_size);
        return false;
    }
    if (cJSON_IsNumber(item)) next->max_retries_per_profile = (uint8_t)item->valueint;

    item = cJSON_GetObjectItemCaseSensitive(root, "reconnect_backoff_ms");
    if (item && (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
                 floor(item->valuedouble) != item->valuedouble ||
                 item->valuedouble < 500 || item->valuedouble > 60000)) {
        strlcpy(error, "Reconnect delay must be 500-60000 ms", error_size);
        return false;
    }
    if (cJSON_IsNumber(item)) next->reconnect_backoff_ms = (uint32_t)item->valuedouble;

    if (!next->primary.enabled || !next->primary.ssid[0]) {
        strlcpy(error, "The primary Wi-Fi profile must remain enabled", error_size);
        return false;
    }
    /* THE TWO PROFILES MAY NAME THE SAME NETWORK.
     *
     * This used to be refused, on the reasoning that a fallback identical to the
     * primary adds nothing. That is true of the RETRY behaviour and false of
     * everything else: plenty of sites have exactly one Wi-Fi, and telling their
     * engineer that the controller will not accept it twice is a rule with no
     * safety behind it. It also made the recovery-AP passphrase unwritable on a
     * unit that had somehow acquired two identical profiles, because the whole
     * configuration round-trip was rejected on a field nobody was editing.
     *
     * What still must hold is that the primary is enabled and named -- a
     * controller with no station profile at all has nothing to fall back FROM. */

    if (next->fallback_ap_enabled && !next->fallback_ap_ssid[0]) {
        strlcpy(error, "Recovery AP SSID is required", error_size);
        return false;
    }
    size_t ap_password_length = strlen(next->fallback_ap_password);
    if (ap_password_length > 0 && ap_password_length < 8) {
        strlcpy(error, "Recovery AP password must contain at least 8 characters", error_size);
        return false;
    }
    return true;
}

static esp_err_t wifi_config_post(httpd_req_t *request)
{
    cJSON *root = NULL;
    esp_err_t read_err = http_json_parse_bounded(request, WIFI_CONFIG_MAX_BODY,
                                                  WEB_API_BODY_DEADLINE_MS,
                                                  WEB_API_JSON_MAX_DEPTH,
                                                  &root);
    if (read_err == ESP_ERR_INVALID_SIZE) {
        return send_json_error(request, "413 Payload Too Large",
                               "Wi-Fi configuration body is missing or too large");
    }
    if (read_err == ESP_ERR_TIMEOUT) {
        return send_json_error(request, "408 Request Timeout",
                               "Wi-Fi configuration body timed out");
    }
    if (read_err != ESP_OK) {
        return send_json_error(request, "400 Bad Request",
                               "Wi-Fi configuration must be valid bounded JSON");
    }

    app_config_t *config = malloc(sizeof(*config));
    if (!config) {
        cJSON_Delete(root);
        return httpd_resp_send_500(request);
    }
    esp_err_t err = config_manager_get_snapshot(config);
    if (err != ESP_OK) {
        free(config);
        cJSON_Delete(root);
        return send_json_error(request, "500 Internal Server Error", "Current configuration is unavailable");
    }

    app_wifi_config_t next = {0};
    char validation_error[128] = {0};
    bool valid = parse_wifi_config(root, &config->wifi, &next,
                                   validation_error, sizeof(validation_error));
    cJSON_Delete(root);
    if (!valid) {
        free(config);
        return send_json_error(request, "400 Bad Request", validation_error);
    }

    config->wifi = next;
    err = config_manager_save(config);
    free(config);
    if (err != ESP_OK) {
        return send_json_error(request, "500 Internal Server Error", "Wi-Fi configuration could not be persisted");
    }
    return send_json_text(request, NULL, "{\"saved\":true,\"persisted\":true,\"restart_required\":true}");
}

static const char *scan_auth_name(uint8_t auth_mode)
{
    static const char *names[] = {
        "Open", "WEP", "WPA-PSK", "WPA2-PSK", "WPA/WPA2-PSK",
        "WPA2-Enterprise", "WPA3-PSK", "WPA2/WPA3-PSK", "WAPI-PSK",
        "OWE", "WPA3-Enterprise-192"
    };
    return auth_mode < sizeof(names) / sizeof(names[0]) ? names[auth_mode] : "Unsupported";
}

static esp_err_t wifi_scan_get(httpd_req_t *request)
{
    network_scan_snapshot_t *snapshot = calloc(1, sizeof(*snapshot));
    if (!snapshot) return httpd_resp_send_500(request);
    network_manager_get_scan_snapshot(snapshot);

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(snapshot);
        return httpd_resp_send_500(request);
    }
    cJSON_AddNumberToObject(root, "state", snapshot->state);
    cJSON_AddNumberToObject(root, "generation", snapshot->generation);
    cJSON_AddNumberToObject(root, "started_ms", snapshot->started_ms);
    cJSON_AddNumberToObject(root, "completed_ms", snapshot->completed_ms);
    cJSON_AddNumberToObject(root, "last_error", snapshot->last_error);
    cJSON_AddStringToObject(root, "error_name", esp_err_to_name(snapshot->last_error));
    cJSON *networks = cJSON_AddArrayToObject(root, "networks");

    for (uint16_t index = 0; index < snapshot->count; ++index) {
        const network_scan_ap_t *record = &snapshot->results[index];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", record->ssid);
        cJSON_AddNumberToObject(item, "rssi", record->rssi);
        cJSON_AddNumberToObject(item, "channel", record->channel);
        cJSON_AddNumberToObject(item, "auth_mode", record->auth_mode);
        cJSON_AddStringToObject(item, "security", scan_auth_name(record->auth_mode));
        cJSON_AddBoolToObject(item, "configured_primary", record->configured_primary);
        cJSON_AddBoolToObject(item, "configured_fallback", record->configured_fallback);
        cJSON_AddBoolToObject(item, "connected", record->connected);
        cJSON_AddItemToArray(networks, item);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(snapshot);
    if (!json) return httpd_resp_send_500(request);
    esp_err_t err = send_json_text(request, NULL, json);
    free(json);
    return err;
}

static esp_err_t wifi_scan_post(httpd_req_t *request)
{
    esp_err_t err = network_manager_request_scan();
    if (err == ESP_ERR_INVALID_STATE) {
        return send_json_error(request, "409 Conflict", "Wi-Fi scan is already running or the radio is reconnecting");
    }
    if (err != ESP_OK) {
        return send_json_error(request, "500 Internal Server Error", "Wi-Fi scan could not be started");
    }
    return send_json_text(request, "202 Accepted", "{\"accepted\":true}");
}

static esp_err_t wifi_rescan_post(httpd_req_t *request)
{
    bool accepted = false;
    esp_err_t begin_err = network_manager_operator_reconnect_response_begin(&accepted);
    if (begin_err == ESP_ERR_INVALID_STATE) {
        return send_json_error(request, "409 Conflict",
                               "An operator reconnect is already in progress");
    }
    if (begin_err != ESP_OK) {
        return send_json_error(request, "500 Internal Server Error",
                               "Wi-Fi reconnect could not be scheduled");
    }

    esp_err_t send_err = accepted
        ? send_json_text(request, "202 Accepted", "{\"accepted\":true}")
        : send_json_error(request, "409 Conflict",
                          "An operator reconnect is already in progress");

    network_manager_operator_reconnect_response_complete(accepted, send_err);
    return send_err;
}

static void restart_task(void *argument)
{
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(700));
    esp_restart();
}

static esp_err_t restart_post(httpd_req_t *request)
{
    if (xTaskCreate(restart_task, "api_restart", 2048, NULL, 5, NULL) != pdPASS) {
        return send_json_error(request, "500 Internal Server Error", "Restart scheduling failed");
    }
    return send_json_text(request, NULL, "{\"restarting\":true}");
}


/*
 * OPERATOR NETWORK CONTROLS.
 *
 * The site owner has to be able to move their own controller onto a different
 * Wi-Fi -- they change routers, they change premises, and they do not have the
 * engineering password. Requiring one for that turns a five-minute job into a
 * site visit.
 *
 * SO THE SURFACE IS NARROWED INSTEAD OF THE AUTHENTICATION BEING DROPPED. These
 * two routes do exactly one thing between them: list what is in range, and join
 * one of them with a passphrase. Everything else on the network page stays
 * behind engineering, and each exclusion is a way a controller could be lost or
 * taken:
 *
 *   - The recovery AP passphrase and SSID. That access point is the guaranteed
 *     way back into a unit whose station credentials are wrong. An operator who
 *     could change it could lock the controller away from everyone including
 *     themselves.
 *   - Static IP, gateway, netmask, DNS. Wrong values here strand a unit on a
 *     network it appears to have joined, which is harder to diagnose than a
 *     wrong passphrase.
 *   - Retry and backoff timing, and the fallback profile.
 *
 * WHAT THIS DOES COST, stated rather than glossed: anyone already on the site
 * network can move the controller to a different access point. The mitigations
 * are that the recovery AP stays on air and out of reach, so the unit is never
 * lost -- and that a wrong join is recoverable by an engineer with physical
 * access. Recorded in docs/RELEASE_READINESS.md as a product decision.
 */
static esp_err_t network_scan_get(httpd_req_t *request)
{
    return wifi_scan_get(request);
}

static esp_err_t network_join_post(httpd_req_t *request)
{
    cJSON *root = NULL;
    esp_err_t read_err = http_json_parse_bounded(request, 512U, WEB_API_BODY_DEADLINE_MS,
                                                 WEB_API_JSON_MAX_DEPTH, &root);
    if (read_err != ESP_OK) {
        cJSON_Delete(root);
        return send_json_error(request, "400 Bad Request",
                               "A JSON body with ssid and password is required");
    }

    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    const cJSON *password = cJSON_GetObjectItemCaseSensitive(root, "password");
    if (!cJSON_IsString(ssid) || !ssid->valuestring[0] ||
        strlen(ssid->valuestring) > 32U) {
        cJSON_Delete(root);
        return send_json_error(request, "400 Bad Request", "A network name is required");
    }
    /* An open network is a legitimate choice, so an empty passphrase is
     * accepted; a too-long one is not. WPA2 refuses under 8 characters, and
     * accepting one here would produce a join that silently never succeeds. */
    const char *pass = cJSON_IsString(password) ? password->valuestring : "";
    const size_t pass_len = strlen(pass);
    if (pass_len > 64U || (pass_len > 0U && pass_len < 8U)) {
        cJSON_Delete(root);
        return send_json_error(request, "400 Bad Request",
                               "A Wi-Fi passphrase must be 8-64 characters, or empty for an open network");
    }

    app_config_t *config = malloc(sizeof(*config));
    if (!config) {
        cJSON_Delete(root);
        return httpd_resp_send_500(request);
    }
    if (config_manager_get_snapshot(config) != ESP_OK) {
        cJSON_Delete(root);
        free(config);
        return send_json_error(request, "500 Internal Server Error",
                               "Current configuration is unavailable");
    }

    /* The primary station profile ONLY. Addressing mode is reset to DHCP with
     * the credentials, because a static address belonging to the previous
     * network is the most common way a join appears to work and then does not:
     * the unit associates and is unreachable. An engineer who needs a static
     * address sets it afterwards on the engineering page. */
    strlcpy(config->wifi.primary.ssid, ssid->valuestring, sizeof(config->wifi.primary.ssid));
    strlcpy(config->wifi.primary.password, pass, sizeof(config->wifi.primary.password));
    config->wifi.primary.enabled = true;
    config->wifi.primary.ip_mode = APP_WIFI_IP_DHCP;
    config->wifi.primary.static_ip[0] = 0;
    config->wifi.primary.gateway[0] = 0;
    config->wifi.primary.netmask[0] = 0;
    /* Never touched here, and named so the omission is visible rather than
     * accidental: the recovery access point stays exactly as it is. */
    config->wifi.fallback_ap_enabled = true;

    const esp_err_t saved = config_manager_save(config);
    const bool was_static = false;
    (void)was_static;
    free(config);
    cJSON_Delete(root);
    if (saved != ESP_OK) {
        return send_json_error(request, "500 Internal Server Error",
                               "The network could not be saved");
    }

    char response[320];
    snprintf(response, sizeof(response),
             "{\"saved\":true,\"restart_required\":true,"
             "\"recovery_ap_unchanged\":true,"
             "\"notice\":\"The controller joins this network after a restart. "
             "If it cannot, the setup access point stays available.\"}");
    return send_json_text(request, NULL, response);
}

esp_err_t web_api_register(httpd_handle_t server)
{
    const httpd_uri_t handlers[] = {
        {.uri = "/api/status", .method = HTTP_GET, .handler = status_get},
        {.uri = "/api/config", .method = HTTP_GET, .handler = config_get},
        {.uri = "/api/config", .method = HTTP_POST, .handler = config_post},
        {.uri = "/api/wifi/config", .method = HTTP_POST, .handler = wifi_config_post},
        {.uri = "/api/wifi/scan", .method = HTTP_GET, .handler = wifi_scan_get},
        {.uri = "/api/wifi/scan", .method = HTTP_POST, .handler = wifi_scan_post},
        {.uri = "/api/wifi/rescan", .method = HTTP_POST, .handler = wifi_rescan_post},
        {.uri = "/api/system/restart", .method = HTTP_POST, .handler = restart_post},
        {.uri = "/api/network/scan", .method = HTTP_GET, .handler = network_scan_get},
        {.uri = "/api/network/join", .method = HTTP_POST, .handler = network_join_post}
    };

    for (size_t index = 0; index < sizeof(handlers) / sizeof(handlers[0]); ++index) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &handlers[index]),
                            "web_api", "handler registration failed");
    }
    return ESP_OK;
}
