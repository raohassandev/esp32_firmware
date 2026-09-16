#include "pvdg_ui_inverter_config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void set_error(char *error, size_t error_size, const char *text)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", text ? text : "");
}

static size_t bounded_length(const char *text, size_t capacity)
{
    if (!text) return capacity;
    size_t n = 0U;
    while (n < capacity && text[n]) n++;
    return n;
}

void pvdg_ui_inverter_defaults(pvdg_ui_inverter_config_t *inverter, uint8_t index)
{
    if (!inverter) return;
    memset(inverter, 0, sizeof(*inverter));
    snprintf(inverter->name, sizeof(inverter->name), "Inverter %u", (unsigned)index + 1U);
    inverter->port = 502U;
    inverter->unit_id = 1U;
    inverter->timeout_ms = 1000U;
}

static pvdg_ui_inverter_config_result_t validate_one(
    const pvdg_ui_inverter_config_t *inverter, uint8_t index,
    char *error, size_t error_size)
{
    char message[192];
    if (!inverter) return PVDG_UI_INVERTER_CONFIG_INVALID_ARGUMENT;
    size_t name_len = bounded_length(inverter->name, sizeof(inverter->name));
    size_t host_len = bounded_length(inverter->host, sizeof(inverter->host));
    if (name_len == 0U || name_len >= sizeof(inverter->name)) {
        snprintf(message, sizeof(message), "Inverter %u requires a name shorter than 24 bytes.", (unsigned)index + 1U);
        set_error(error, error_size, message);
        return PVDG_UI_INVERTER_CONFIG_NAME_INVALID;
    }
    if (host_len >= sizeof(inverter->host) || inverter->port == 0U ||
        inverter->unit_id == 0U || inverter->unit_id > 247U ||
        inverter->timeout_ms < 100U || inverter->timeout_ms > 60000U) {
        snprintf(message, sizeof(message), "Inverter %u endpoint must use port 1-65535, unit ID 1-247 and timeout 100-60000 ms.", (unsigned)index + 1U);
        set_error(error, error_size, message);
        return PVDG_UI_INVERTER_CONFIG_ENDPOINT_INVALID;
    }
    if (!isfinite(inverter->rated_kw) || inverter->rated_kw < 0.0 ||
        inverter->rated_kw > PVDG_UI_INVERTER_RATED_MAX_KW) {
        snprintf(message, sizeof(message), "Inverter %u rated power must be 0-100000 kW.", (unsigned)index + 1U);
        set_error(error, error_size, message);
        return PVDG_UI_INVERTER_CONFIG_RATED_INVALID;
    }
    if (inverter->enabled && (host_len == 0U || inverter->rated_kw <= 0.0)) {
        snprintf(message, sizeof(message), "Enabled inverter %u requires a host and rated power greater than zero.", (unsigned)index + 1U);
        set_error(error, error_size, message);
        return PVDG_UI_INVERTER_CONFIG_ENDPOINT_INVALID;
    }
    return PVDG_UI_INVERTER_CONFIG_OK;
}

pvdg_ui_inverter_config_result_t pvdg_ui_inverter_list_validate(
    const pvdg_ui_inverter_list_t *list, char *error, size_t error_size)
{
    set_error(error, error_size, "");
    if (!list) return PVDG_UI_INVERTER_CONFIG_INVALID_ARGUMENT;
    if (list->count > PVDG_UI_MAX_INVERTER_CONFIGS) {
        set_error(error, error_size, "Inverter count exceeds the controller limit of twelve.");
        return PVDG_UI_INVERTER_CONFIG_COUNT_INVALID;
    }
    for (uint8_t i = 0U; i < list->count; ++i) {
        pvdg_ui_inverter_config_result_t result = validate_one(&list->inverters[i], i, error, error_size);
        if (result != PVDG_UI_INVERTER_CONFIG_OK) return result;
    }
    for (uint8_t left = 0U; left < list->count; ++left) {
        if (!list->inverters[left].enabled) continue;
        for (uint8_t right = (uint8_t)(left + 1U); right < list->count; ++right) {
            if (!list->inverters[right].enabled) continue;
            const pvdg_ui_inverter_config_t *a = &list->inverters[left];
            const pvdg_ui_inverter_config_t *b = &list->inverters[right];
            if (a->port == b->port && a->unit_id == b->unit_id && strcmp(a->host, b->host) == 0) {
                char message[160];
                snprintf(message, sizeof(message), "Inverters %u and %u use the same enabled host, port and unit ID.", (unsigned)left + 1U, (unsigned)right + 1U);
                set_error(error, error_size, message);
                return PVDG_UI_INVERTER_CONFIG_DUPLICATE_ENDPOINT;
            }
        }
    }
    return PVDG_UI_INVERTER_CONFIG_OK;
}

const pvdg_ui_inverter_profile_t *pvdg_ui_inverter_profile_find(
    const pvdg_ui_inverter_profile_catalog_t *catalog, const char *profile_id)
{
    if (!catalog || !profile_id || !profile_id[0]) return NULL;
    uint8_t count = catalog->count > PVDG_UI_MAX_INVERTER_PROFILES
                        ? PVDG_UI_MAX_INVERTER_PROFILES
                        : catalog->count;
    for (uint8_t i = 0U; i < count; ++i) {
        if (strcmp(catalog->profiles[i].id, profile_id) == 0) return &catalog->profiles[i];
    }
    return NULL;
}

bool pvdg_ui_inverter_profile_assignment_valid(
    const pvdg_ui_inverter_profile_catalog_t *catalog, const char *profile_id)
{
    return pvdg_ui_inverter_profile_find(catalog, profile_id) != NULL;
}
