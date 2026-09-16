#include "pvdg_ui_wifi_config.h"

#include <stdio.h>
#include <string.h>

#define MASKED_PASSWORD "********"

static void set_error(char *error, size_t size, const char *text)
{
    if (!error || size == 0U) return;
    snprintf(error, size, "%s", text ? text : "");
}

static bool password_value_valid(const char *password)
{
    if (!password || !password[0]) return true;
    if (strcmp(password, MASKED_PASSWORD) == 0) return true;
    const size_t length = strlen(password);
    return length >= 8U && length <= 64U;
}

static bool profile_valid(const pvdg_ui_wifi_profile_config_t *profile,
                          char *error, size_t error_size)
{
    if (!profile) {
        set_error(error, error_size, "Wi-Fi profile is missing");
        return false;
    }
    const size_t ssid_length = strlen(profile->ssid);
    if (profile->enabled && ssid_length == 0U) {
        set_error(error, error_size, "Enabled Wi-Fi profile requires an SSID");
        return false;
    }
    if (ssid_length > 32U) {
        set_error(error, error_size, "SSID must contain no more than 32 characters");
        return false;
    }
    if (!password_value_valid(profile->password)) {
        set_error(error, error_size, "Wi-Fi password must contain 8-64 characters");
        return false;
    }
    if (profile->ip_mode != PVDG_UI_WIFI_IP_DHCP &&
        profile->ip_mode != PVDG_UI_WIFI_IP_STATIC) {
        set_error(error, error_size, "Invalid Wi-Fi IP mode");
        return false;
    }
    return true;
}

bool pvdg_ui_wifi_config_valid(const pvdg_ui_wifi_config_t *config,
                               char *error, size_t error_size)
{
    if (!config) {
        set_error(error, error_size, "Wi-Fi configuration is missing");
        return false;
    }
    if (!profile_valid(&config->primary, error, error_size) ||
        !profile_valid(&config->fallback, error, error_size)) return false;
    if (!config->primary.enabled) {
        set_error(error, error_size, "Primary Wi-Fi profile must remain enabled");
        return false;
    }
    if (config->fallback.enabled && config->primary.ssid[0] &&
        strcmp(config->primary.ssid, config->fallback.ssid) == 0) {
        set_error(error, error_size, "Primary and fallback SSIDs must be different");
        return false;
    }
    const size_t recovery_ssid_length = strlen(config->fallback_ap_ssid);
    if (config->fallback_ap_enabled && recovery_ssid_length == 0U) {
        set_error(error, error_size, "Recovery AP SSID is required");
        return false;
    }
    if (recovery_ssid_length > 32U) {
        set_error(error, error_size, "Recovery AP SSID must contain no more than 32 characters");
        return false;
    }
    if (!password_value_valid(config->fallback_ap_password)) {
        set_error(error, error_size, "Recovery AP password must contain 8-64 characters");
        return false;
    }
    if (config->max_retries_per_profile < 1U ||
        config->max_retries_per_profile > 20U) {
        set_error(error, error_size, "Retries must be between 1 and 20");
        return false;
    }
    if (config->reconnect_backoff_ms < 500U ||
        config->reconnect_backoff_ms > 60000U) {
        set_error(error, error_size, "Reconnect delay must be 500-60000 ms");
        return false;
    }
    set_error(error, error_size, "");
    return true;
}

pvdg_ui_wifi_config_result_t pvdg_ui_wifi_prepare_primary(
    const pvdg_ui_wifi_config_t *current,
    const char *ssid,
    const char *password,
    bool secure_network,
    pvdg_ui_wifi_config_t *out,
    bool *primary_changed,
    char *error,
    size_t error_size)
{
    if (!current || !ssid || !out) {
        set_error(error, error_size, "Wi-Fi selection is incomplete");
        return PVDG_UI_WIFI_CONFIG_INVALID_ARGUMENT;
    }
    if (!pvdg_ui_wifi_config_valid(current, error, error_size)) {
        return PVDG_UI_WIFI_CONFIG_INVALID_BASELINE;
    }
    const size_t ssid_length = strlen(ssid);
    if (ssid_length == 0U || ssid_length > 32U) {
        set_error(error, error_size, "Selected SSID must contain 1-32 characters");
        return PVDG_UI_WIFI_CONFIG_INVALID_SSID;
    }

    const bool changed = strcmp(current->primary.ssid, ssid) != 0;
    const char *supplied = password ? password : "";
    const size_t password_length = strlen(supplied);
    if (password_length > 0U &&
        (password_length < 8U || password_length > 64U)) {
        set_error(error, error_size, "Wi-Fi password must contain 8-64 characters");
        return PVDG_UI_WIFI_CONFIG_INVALID_PASSWORD;
    }
    if (changed && secure_network && password_length == 0U) {
        set_error(error, error_size,
                  "Enter the password for the newly selected secured network");
        return PVDG_UI_WIFI_CONFIG_PASSWORD_REQUIRED;
    }

    *out = *current;
    out->primary.enabled = true;
    snprintf(out->primary.ssid, sizeof(out->primary.ssid), "%s", ssid);
    out->primary.clear_password = false;

    if (password_length > 0U) {
        snprintf(out->primary.password, sizeof(out->primary.password), "%s", supplied);
    } else if (changed && !secure_network) {
        out->primary.password[0] = '\0';
        out->primary.clear_password = true;
    }

    if (!pvdg_ui_wifi_config_valid(out, error, error_size)) {
        return PVDG_UI_WIFI_CONFIG_INVALID_BASELINE;
    }
    if (primary_changed) *primary_changed = changed;
    set_error(error, error_size, "");
    return PVDG_UI_WIFI_CONFIG_OK;
}
