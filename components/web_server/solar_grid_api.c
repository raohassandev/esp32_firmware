#include "solar_grid_api.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "cJSON.h"
#include "commissioning_gate.h"
#include "config_manager.h"
#include "control_engine.h"
#include "esp_check.h"
#include "generator_fleet_limit.h"
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

/* Enumerated ceilings, derived from the enums rather than written as literals so
 * that appending a sharing mode or an engine role cannot leave this API silently
 * refusing the new value. solar_grid_config_valid() applies the same ceilings. */
#define SOLAR_GRID_SHARING_MODE_MAX ((uint32_t)SOLAR_GRID_LOAD_SHARING_COUNT - 1U)
#define SOLAR_GRID_ENGINE_ROLE_MAX ((uint32_t)SOLAR_GRID_ENGINE_ROLE_COUNT - 1U)

/* The two enums this API reports across are kept numerically identical by
 * _Static_asserts in the control engine, which is what lets one slug vocabulary
 * and one "is this mode supported" predicate serve both layers. Restated here
 * because this file reads the persisted value and asks the control engine's
 * predicate about it: if they ever diverged, this API would report a mode as
 * supported while the controller refused it, which is the one answer an
 * interface must never give. */
_Static_assert((int)SOLAR_GRID_LOAD_SHARING_UNSET == (int)GENERATOR_SHARING_UNSET &&
                   (int)SOLAR_GRID_LOAD_SHARING_ISOCHRONOUS == (int)GENERATOR_SHARING_ISOCHRONOUS &&
                   (int)SOLAR_GRID_LOAD_SHARING_BASE_LOAD == (int)GENERATOR_SHARING_BASE_LOAD &&
                   (int)SOLAR_GRID_LOAD_SHARING_DROOP == (int)GENERATOR_SHARING_DROOP &&
                   (int)SOLAR_GRID_LOAD_SHARING_COUNT == (int)GENERATOR_SHARING_MODE_COUNT,
               "the persisted load-sharing enum and the control engine's must agree "
               "value for value, or this API would report a refused mode as supported");
_Static_assert((int)SOLAR_GRID_ENGINE_ROLE_UNSET == (int)GENERATOR_ENGINE_ROLE_UNSET &&
                   (int)SOLAR_GRID_ENGINE_ROLE_SWING == (int)GENERATOR_ENGINE_ROLE_SWING &&
                   (int)SOLAR_GRID_ENGINE_ROLE_BASE_LOAD == (int)GENERATOR_ENGINE_ROLE_BASE_LOAD &&
                   (int)SOLAR_GRID_ENGINE_ROLE_COUNT == (int)GENERATOR_ENGINE_ROLE_COUNT,
               "the persisted engine-role enum and the control engine's must agree "
               "value for value");

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

/*
 * WHY A REFUSED MODE MUST BE REPORTED AS REFUSED, AND WITH THE FIRMWARE'S REASON.
 *
 * The control engine computes a minimum-loading floor for isochronous and base-load
 * sharing and REFUSES droop, permanently and deliberately. An interface that offered
 * the four stored values as four equally selectable options would let an engineer
 * commission droop, get a stored configuration that validates, and then find the
 * plant will not command PV with nothing on screen connecting the two.
 *
 * So each mode is published with `supported` from the control engine's own predicate
 * -- never from a list maintained here -- and, when it is not supported, with the
 * sentence the commissioning gate itself would print. Nothing on this path
 * paraphrases a safety decision: generator_sharing_mode_supported() decides, and
 * commissioning_reason_message() supplies the words.
 */
static const char *sharing_mode_refusal(uint8_t mode)
{
    if (generator_sharing_mode_supported(mode)) return "";
    if (mode == (uint8_t)SOLAR_GRID_LOAD_SHARING_UNSET) {
        return commissioning_reason_message(
            (uint8_t)COMMISSIONING_REASON_GENERATOR_SHARING_MODE_UNSET);
    }
    return commissioning_reason_message(
        (uint8_t)COMMISSIONING_REASON_GENERATOR_SHARING_MODE_UNSUPPORTED);
}

static void sharing_modes_json(cJSON *parent, uint8_t selected)
{
    cJSON *modes = cJSON_AddArrayToObject(parent, "load_sharing_modes");
    if (!modes) return;
    for (uint8_t mode = 0U; mode < (uint8_t)SOLAR_GRID_LOAD_SHARING_COUNT; ++mode) {
        cJSON *entry = cJSON_CreateObject();
        if (!entry) continue;
        cJSON_AddNumberToObject(entry, "value", mode);
        cJSON_AddStringToObject(entry, "id", solar_grid_load_sharing_name(mode));
        /* The control engine's own predicate. Never a list kept in this file: a
         * mode added there and forgotten here would be offered as if selecting it
         * would work. */
        const bool supported = generator_sharing_mode_supported(mode);
        cJSON_AddBoolToObject(entry, "supported", supported);
        cJSON_AddBoolToObject(entry, "refused", !supported);
        cJSON_AddBoolToObject(entry, "selected", mode == selected);
        cJSON_AddStringToObject(entry, "reason", sharing_mode_refusal(mode));
        cJSON_AddItemToArray(modes, entry);
    }
}

/* One engine slot, as the uniform sequence solar_grid_config_generator() presents
 * rather than as the two halves it is stored in. `in_service_derived` marks slot 0,
 * whose in-service flag is not a stored field: it is in service exactly when its
 * rating is positive, which is what "commissioned" has meant for the
 * single-generator configuration since schema 2. Saying so in the payload is what
 * lets an interface show the slot-0 control as derived instead of offering a
 * checkbox that cannot be honoured. */
static void engines_json(cJSON *parent, const solar_grid_config_t *config)
{
    cJSON *engines = cJSON_AddArrayToObject(parent, "engines");
    if (!engines) return;
    for (uint8_t slot = 0U; slot < SOLAR_GRID_MAX_GENERATORS; ++slot) {
        const solar_grid_generator_limits_t limits =
            solar_grid_config_generator(config, slot);
        cJSON *entry = cJSON_CreateObject();
        if (!entry) continue;
        cJSON_AddNumberToObject(entry, "slot", slot);
        cJSON_AddBoolToObject(entry, "in_service", limits.enabled);
        cJSON_AddBoolToObject(entry, "in_service_derived", slot == 0U);
        cJSON_AddNumberToObject(entry, "rated_kw", limits.rated_kw);
        cJSON_AddNumberToObject(entry, "minimum_loading_percent",
                                limits.minimum_loading_percent);
        cJSON_AddNumberToObject(entry, "reserve_kw", limits.reserve_kw);
        cJSON_AddNumberToObject(entry, "reverse_power_margin_kw",
                                limits.reverse_power_margin_kw);
        /* This engine's OWN minimum in kW. Arithmetic on two commissioned numbers,
         * not a new commissioned quantity, and it is the figure a base-load setpoint
         * has to reach: a setpoint below it is a commissioning fault no plant load
         * can correct, because a base-loaded engine's load does not follow the
         * total. */
        cJSON_AddNumberToObject(entry, "minimum_loading_kw",
                                limits.rated_kw * limits.minimum_loading_percent / 100.0f);
        const uint8_t role = solar_grid_config_engine_role(config, slot);
        cJSON_AddNumberToObject(entry, "role", role);
        cJSON_AddStringToObject(entry, "role_name", solar_grid_engine_role_name(role));
        /* Zero means "not commissioned", reported as-is. No setpoint is invented:
         * a guessed fixed kW would be a commissioning fact nobody supplied. */
        cJSON_AddNumberToObject(entry, "base_load_kw",
                                solar_grid_config_engine_base_load_kw(config, slot));
        cJSON_AddItemToArray(engines, entry);
    }
}

/* The multi-engine generator policy: the kW load-sharing mode, which modes the
 * firmware will actually compute a floor for, and every engine slot.
 *
 * Until this existed the API carried engine slot 0 only, so a plant that can run
 * two or three gensets in parallel could not be commissioned through the product at
 * all: the commissioning gate correctly refused to command PV and no engineer had
 * any way to supply what it was asking for. */
static void generator_policy_json(cJSON *root, const solar_grid_config_t *config)
{
    const uint8_t mode = solar_grid_config_load_sharing_mode(config);
    cJSON_AddNumberToObject(root, "load_sharing_mode", mode);
    cJSON_AddStringToObject(root, "load_sharing_mode_name",
                            solar_grid_load_sharing_name(mode));
    cJSON_AddBoolToObject(root, "load_sharing_mode_supported",
                          generator_sharing_mode_supported(mode));
    cJSON_AddStringToObject(root, "load_sharing_mode_reason", sharing_mode_refusal(mode));
    sharing_modes_json(root, mode);
    cJSON_AddNumberToObject(root, "engine_slot_count", SOLAR_GRID_MAX_GENERATORS);
    cJSON_AddNumberToObject(root, "engines_in_service",
                            solar_grid_config_enabled_generator_count(config));
    engines_json(root, config);
    /* Two consequences an interface must be able to state without composing them
     * itself, both in the firmware's own words.
     *
     * A sharing mode is required only once the plant can run two or more engines:
     * with one engine on the bus there is no load to share and every sharing law
     * gives it the whole plant load, which is the exemption that keeps a
     * commissioned single-generator site behaving exactly as it did before. */
    cJSON_AddBoolToObject(root, "load_sharing_mode_required",
                          solar_grid_config_enabled_generator_count(config) > 1U);
    cJSON_AddStringToObject(root, "load_sharing_unset_reason",
                            commissioning_reason_message(
                                (uint8_t)COMMISSIONING_REASON_GENERATOR_SHARING_MODE_UNSET));
    cJSON_AddStringToObject(root, "base_load_unknown_reason",
                            commissioning_reason_message(
                                (uint8_t)COMMISSIONING_REASON_GENERATOR_BASE_LOAD_UNKNOWN));
    /* A base-loaded engine held below its own minimum loading is a commissioning
     * fault, not a floor to compute around. Published so the interface states the
     * consequence with the gate's own sentence. */
    cJSON_AddStringToObject(root, "base_load_below_minimum_reason",
                            commissioning_reason_message(
                                (uint8_t)COMMISSIONING_REASON_GENERATOR_BASE_LOAD_BELOW_MINIMUM));
    cJSON_AddStringToObject(root, "no_swing_engine_reason",
                            commissioning_reason_message(
                                (uint8_t)COMMISSIONING_REASON_GENERATOR_NO_SWING_ENGINE));
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
    /* Generator limits (schema 2). They are persisted, validated and consumed by
     * the control engine, so a field this API never serialises can never be read
     * back, never be edited and therefore never be commissioned: it would stay
     * zero for the life of the unit. A generator_rated_kw of zero is reported
     * as-is and means "not commissioned"; it holds PV at zero while a generator
     * carries the plant, which is the safe state rather than an error. */
    cJSON_AddNumberToObject(root, "generator_rated_kw", config->generator_rated_kw);
    cJSON_AddNumberToObject(root, "generator_minimum_loading_percent",
                            config->generator_minimum_loading_percent);
    cJSON_AddNumberToObject(root, "generator_reserve_kw",
                            config->generator_reserve_kw);
    cJSON_AddNumberToObject(root, "generator_reverse_power_margin_kw",
                            config->generator_reverse_power_margin_kw);
    cJSON_AddBoolToObject(root, "generator_commissioned",
                          config->generator_rated_kw > 0.0f);
    /* Engine slots 1.. and the kW load-sharing mode (schema 3 and 4). The four
     * scalars above remain engine slot 0 and keep meaning exactly what they meant
     * before, so a client that reads and writes only them is unaffected. */
    generator_policy_json(root, config);
    cJSON_AddBoolToObject(root, "evidence_complete",
                          solar_grid_config_evidence_complete(config));
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

/* Read an optional non-negative float with an upper bound, naming the field in
 * the caller's error buffer when it is rejected. An absent key leaves the stored
 * value untouched, so a partial POST cannot silently zero a commissioned limit.
 *
 * Zero is accepted for every field that uses this helper. For
 * generator_rated_kw that is deliberate and load-bearing: zero is the
 * "not commissioned" state that keeps PV at zero whenever a generator carries
 * the plant. It must never be rejected, and no non-zero default may be
 * substituted, because guessing a machine's rating would let PV be commanded
 * against a generator of unknown capacity. Zero is the safe state, not an error. */
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
    /*
     * ABSENT MEANS UNCHANGED, like every other key this handler reads.
     *
     * It did not. A missing object was rejected, and the generator-policy form
     * posts only {load_sharing_mode, engines} -- it has no evidence fields to
     * send and never did. So every attempt to change a generator rating,
     * minimum loading, reserve or margin came back "Solar-Grid configuration
     * validation failed", with no field named, and the fleet editor could not
     * save at all.
     *
     * The handler's own comment states the contract: "Every key here is
     * optional and absent means unchanged". This is the one reader that broke
     * it.
     *
     * A key that is PRESENT but not an object is still refused: that is a
     * malformed request rather than an omission, and silently ignoring it would
     * discard grid evidence somebody meant to write.
     */
    if (!object) return true;
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

/* read_limit, with the engine slot named in the rejection. Two engines carry the
 * same field names, so "rated_kw is out of range" without a slot number sends an
 * engineer to the wrong machine. */
static bool engine_limit(cJSON *object, const char *key, float maximum, float *value,
                         uint8_t slot, char *error, size_t error_size)
{
    char detail[SOLAR_GRID_FIELD_ERROR_MAX] = {0};
    if (read_limit(object, key, maximum, value, detail, sizeof(detail))) return true;
    snprintf(error, error_size, "Solar-Grid engine %u rejected: %s", (unsigned)slot, detail);
    return false;
}

/*
 * One engine slot from the request body.
 *
 * ABSENT MEANS UNCHANGED, everywhere, for the same reason it does for the engine-0
 * scalars: silently zeroing a generator rating would close the commissioning gate on
 * a working plant, and silently zeroing a base-load setpoint would do it for a
 * base-loaded engine. Every read below goes through a helper that returns success
 * without touching the stored value when the key is missing.
 *
 * SLOT 0 IS NOT AN ORDINARY SLOT. Its numbers are the four legacy scalars and its
 * in-service flag is not stored at all -- it is in service exactly when its rating
 * is positive. A body asking for anything else is refused with the reason, never
 * accepted and quietly ignored: an interface that appeared to take an instruction
 * the firmware discarded would show a slot out of service that the controller still
 * counts.
 */
static bool parse_engine(cJSON *item, solar_grid_config_t *next, char *error,
                         size_t error_size)
{
    if (!cJSON_IsObject(item)) {
        snprintf(error, error_size,
                 "Each entry of Solar-Grid 'engines' must be an object carrying a 'slot'");
        return false;
    }
    uint32_t raw = 0U;
    if (!cJSON_GetObjectItemCaseSensitive(item, "slot") ||
        !read_uint(item, "slot", SOLAR_GRID_MAX_GENERATORS - 1U, &raw)) {
        snprintf(error, error_size,
                 "Each Solar-Grid engine needs a whole-number 'slot' between 0 and %u",
                 (unsigned)(SOLAR_GRID_MAX_GENERATORS - 1U));
        return false;
    }
    const uint8_t slot = (uint8_t)raw;

    float *rated = NULL;
    float *loading = NULL;
    float *reserve = NULL;
    float *margin = NULL;
    bool *stored_in_service = NULL;
    if (slot == 0U) {
        rated = &next->generator_rated_kw;
        loading = &next->generator_minimum_loading_percent;
        reserve = &next->generator_reserve_kw;
        margin = &next->generator_reverse_power_margin_kw;
    } else {
        /* Index i of the appended array is engine slot i + 1; see the schema 3
         * comment in solar_grid_config.h for why slot 0 is not in it. */
        solar_grid_generator_limits_t *extra = &next->generator_extra[slot - 1U];
        rated = &extra->rated_kw;
        loading = &extra->minimum_loading_percent;
        reserve = &extra->reserve_kw;
        margin = &extra->reverse_power_margin_kw;
        stored_in_service = &extra->enabled;
    }

    /* Bounded by exactly the ceilings the engine-0 scalars use, and zero stays a
     * legal value for every one of them: a zero rating is the uncommissioned state
     * that holds PV at zero while a generator carries the plant. Zero is the safe
     * state, not an error. */
    if (!engine_limit(item, "rated_kw", SOLAR_GRID_KW_MAX, rated, slot, error, error_size) ||
        !engine_limit(item, "minimum_loading_percent", SOLAR_GRID_PERCENT_MAX, loading,
                      slot, error, error_size) ||
        !engine_limit(item, "reserve_kw", SOLAR_GRID_KW_MAX, reserve, slot, error,
                      error_size) ||
        !engine_limit(item, "reverse_power_margin_kw", SOLAR_GRID_KW_MAX, margin, slot,
                      error, error_size) ||
        !engine_limit(item, "base_load_kw", SOLAR_GRID_KW_MAX,
                      &next->engine_base_load_kw[slot], slot, error, error_size)) {
        return false;
    }

    raw = next->engine_role[slot];
    if (!read_uint(item, "role", SOLAR_GRID_ENGINE_ROLE_MAX, &raw)) {
        snprintf(error, error_size,
                 "Solar-Grid engine %u: 'role' must be 0 (unset), 1 (swing) or 2 (base load)",
                 (unsigned)slot);
        return false;
    }
    next->engine_role[slot] = (uint8_t)raw;

    /* The in-service flag last, so slot 0's consistency check sees the rating this
     * same request settled on rather than the one it started with. */
    if (!cJSON_GetObjectItemCaseSensitive(item, "in_service")) return true;
    const bool derived = isfinite(*rated) && *rated > 0.0f;
    bool requested = stored_in_service ? *stored_in_service : derived;
    if (!read_bool(item, "in_service", &requested)) {
        snprintf(error, error_size,
                 "Solar-Grid engine %u: 'in_service' must be true or false",
                 (unsigned)slot);
        return false;
    }
    if (!stored_in_service) {
        if (requested != derived) {
            snprintf(error, error_size,
                     "Solar-Grid engine 0 is in service exactly when its rated_kw is "
                     "above zero; set rated_kw instead of 'in_service'");
            return false;
        }
        return true;
    }
    *stored_in_service = requested;
    return true;
}

/* The kW load-sharing mode and the engine slots. Both optional: a body carrying
 * only the engine-0 scalars this API has always accepted leaves every value here
 * untouched and means exactly what it meant before. */
static bool parse_generator_policy(cJSON *root, solar_grid_config_t *next, char *error,
                                   size_t error_size)
{
    uint32_t raw = next->load_sharing_mode;
    if (!read_uint(root, "load_sharing_mode", SOLAR_GRID_SHARING_MODE_MAX, &raw)) {
        snprintf(error, error_size,
                 "Solar-Grid 'load_sharing_mode' must be 0 (unset), 1 (isochronous), "
                 "2 (base load) or 3 (droop, which the controller refuses to model)");
        return false;
    }
    /* Droop is STORED and REPORTED, and refused by the control engine. It is
     * accepted here rather than rejected so that an engineer who states the plant is
     * droop-shared has that fact recorded instead of overwritten -- the GET response
     * reports it as unsupported with the firmware's own reason, and the gate stays
     * closed. Silently substituting a mode the controller can compute would be
     * writing a commissioning fact nobody supplied. */
    next->load_sharing_mode = (uint8_t)raw;

    cJSON *engines = cJSON_GetObjectItemCaseSensitive(root, "engines");
    if (!engines) return true;
    if (!cJSON_IsArray(engines)) {
        snprintf(error, error_size, "Solar-Grid 'engines' must be an array of engine slots");
        return false;
    }
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, engines) {
        if (!parse_engine(item, next, error, error_size)) return false;
    }
    return true;
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

    /* Generator limits (schema 2). Accepting them here is what makes them
     * commissionable at all: until now nothing could ever write them, so they
     * were permanently zero. Zero remains a legal value for all four - notably a
     * zero generator_rated_kw, which is the uncommissioned state that holds PV at
     * zero while a generator carries the plant. It is accepted, not rejected, and
     * no rating is invented on the operator's behalf. */
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

    /* Engine slots 1.. and the kW load-sharing mode (schema 3 and 4). Applied AFTER
     * the engine-0 scalars above so that a body carrying both the legacy scalar and
     * an engines[] entry for slot 0 settles on the per-engine value, which is the
     * more specific statement of the same fact.
     *
     * BACKWARD COMPATIBILITY. Every key here is optional and absent means unchanged,
     * so a body carrying only today's engine-0 fields writes exactly what it wrote
     * before: no engine slot changes state, no rating is zeroed, no sharing mode is
     * invented. Until this existed a multi-engine plant could not be commissioned
     * through the product at all. */
    parsed = parsed && parse_generator_policy(root, &next, field_error,
                                              sizeof(field_error));
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
