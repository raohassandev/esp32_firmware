#include "wifi_runtime_bridge.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config_manager.h"
#include "control_engine.h"
#include "engineering_runtime_bridge.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_manager.h"

#define MASKED_PASSWORD "********"

static const char *TAG = "wifi_panel";

/* Wi-Fi rendering runs inside the LVGL task. app_config_t and the 24-entry
 * network scan snapshot are too large to stack together with the runtime UI
 * model, so keep the bridge's serialized scratch snapshots in module storage. */
static app_config_t s_config_snapshot;
static app_config_t s_save_snapshot;
static pvdg_ui_wifi_config_t s_baseline_snapshot;
static network_scan_snapshot_t s_scan_snapshot;
static uint32_t s_logged_scan_generation;
static network_scan_state_t s_logged_scan_state = NETWORK_SCAN_IDLE;
static bool s_logged_scan_valid;

static void copy_text(char *target, size_t capacity, const char *source)
{
    if (!target || capacity == 0U) return;
    snprintf(target, capacity, "%s", source ? source : "");
}

static void security_info(uint8_t auth_mode,
                          const char **label,
                          bool *secure,
                          bool *supported)
{
    const char *name = "Unknown";
    bool is_secure = true;
    bool is_supported = false;

    switch (auth_mode) {
    case 0: name = "Open"; is_secure = false; is_supported = true; break;
    case 1: name = "WEP"; break;
    case 2: name = "WPA-PSK"; is_supported = true; break;
    case 3: name = "WPA2-PSK"; is_supported = true; break;
    case 4: name = "WPA/WPA2-PSK"; is_supported = true; break;
    case 5: name = "WPA2-Enterprise"; break;
    case 6: name = "WPA3-PSK"; is_supported = true; break;
    case 7: name = "WPA2/WPA3-PSK"; is_supported = true; break;
    case 8: name = "WAPI-PSK"; break;
    case 9: name = "OWE"; is_secure = false; is_supported = true; break;
    case 10: name = "WPA3-Enterprise"; break;
    default: break;
    }

    if (label) *label = name;
    if (secure) *secure = is_secure;
    if (supported) *supported = is_supported;
}

static void fill_live_sta_identity(pvdg_ui_model_t *model)
{
    if (!model || !model->network.online) return;

    if (!model->network.ssid[0]) {
        wifi_ap_record_t ap = {0};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK && ap.ssid[0]) {
            copy_text(model->network.ssid, sizeof(model->network.ssid),
                      (const char *)ap.ssid);
            model->network.rssi = ap.rssi;
        }
    }

    if (!model->network.ip[0]) {
        esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t info = {0};
        if (sta && esp_netif_get_ip_info(sta, &info) == ESP_OK && info.ip.addr != 0U) {
            snprintf(model->network.ip, sizeof(model->network.ip),
                     IPSTR, IP2STR(&info.ip));
        }
    }
}

static void profile_to_ui(const app_wifi_sta_profile_t *source,
                          pvdg_ui_wifi_profile_config_t *target)
{
    memset(target, 0, sizeof(*target));
    target->enabled = source->enabled;
    copy_text(target->ssid, sizeof(target->ssid), source->ssid);
    if (source->password[0]) copy_text(target->password, sizeof(target->password), MASKED_PASSWORD);
    target->ip_mode = source->ip_mode == APP_WIFI_IP_STATIC
                          ? PVDG_UI_WIFI_IP_STATIC : PVDG_UI_WIFI_IP_DHCP;
    copy_text(target->static_ip, sizeof(target->static_ip), source->static_ip);
    copy_text(target->gateway, sizeof(target->gateway), source->gateway);
    copy_text(target->netmask, sizeof(target->netmask), source->netmask);
    copy_text(target->dns1, sizeof(target->dns1), source->dns1);
    copy_text(target->dns2, sizeof(target->dns2), source->dns2);
}

static void ui_to_profile(const pvdg_ui_wifi_profile_config_t *source,
                          const app_wifi_sta_profile_t *current,
                          app_wifi_sta_profile_t *target)
{
    *target = *current;
    target->enabled = source->enabled;
    copy_text(target->ssid, sizeof(target->ssid), source->ssid);
    target->ip_mode = source->ip_mode == PVDG_UI_WIFI_IP_STATIC
                          ? APP_WIFI_IP_STATIC : APP_WIFI_IP_DHCP;
    copy_text(target->static_ip, sizeof(target->static_ip), source->static_ip);
    copy_text(target->gateway, sizeof(target->gateway), source->gateway);
    copy_text(target->netmask, sizeof(target->netmask), source->netmask);
    copy_text(target->dns1, sizeof(target->dns1), source->dns1);
    copy_text(target->dns2, sizeof(target->dns2), source->dns2);

    if (source->clear_password) {
        target->password[0] = '\0';
    } else if (strcmp(source->password, MASKED_PASSWORD) != 0 && source->password[0]) {
        copy_text(target->password, sizeof(target->password), source->password);
    }
}

bool wifi_runtime_bridge_load_config(pvdg_ui_wifi_config_t *out)
{
    if (!out) return false;
    memset(&s_config_snapshot, 0, sizeof(s_config_snapshot));
    if (config_manager_get_snapshot(&s_config_snapshot) != ESP_OK) return false;

    memset(out, 0, sizeof(*out));
    profile_to_ui(&s_config_snapshot.wifi.primary, &out->primary);
    profile_to_ui(&s_config_snapshot.wifi.fallback, &out->fallback);
    out->scan_before_connect = s_config_snapshot.wifi.scan_before_connect;
    out->fallback_ap_enabled = s_config_snapshot.wifi.fallback_ap_enabled;
    copy_text(out->fallback_ap_ssid, sizeof(out->fallback_ap_ssid),
              s_config_snapshot.wifi.fallback_ap_ssid);
    if (s_config_snapshot.wifi.fallback_ap_password[0]) {
        copy_text(out->fallback_ap_password, sizeof(out->fallback_ap_password), MASKED_PASSWORD);
    }
    out->max_retries_per_profile = s_config_snapshot.wifi.max_retries_per_profile;
    out->reconnect_backoff_ms = s_config_snapshot.wifi.reconnect_backoff_ms;
    return true;
}

static void restart_task(void *argument)
{
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

void wifi_runtime_bridge_submit_config(const pvdg_ui_wifi_config_t *config,
                                       bool primary_changed,
                                       void *user)
{
    (void)user;
    if (!config) return;

    if (!engineering_runtime_bridge_is_authorized()) {
        ESP_LOGW(TAG, "Save & Connect rejected: Engineering session is not authorized");
        pvdg_ui_wifi_set_write_authorized(false);
        pvdg_ui_wifi_set_action_state(false,
            "Engineering login required. Tap the gear icon, sign in, then return and tap Save & Connect.");
        return;
    }
    pvdg_ui_wifi_set_write_authorized(true);

    ESP_LOGI(TAG, "Save & Connect requested for SSID '%s' (%s profile, %s)",
             config->primary.ssid,
             primary_changed ? "new" : "existing",
             config->primary.ip_mode == PVDG_UI_WIFI_IP_STATIC ? "static IP" : "DHCP");

    char validation_error[160] = {0};
    if (!pvdg_ui_wifi_config_valid(config, validation_error, sizeof(validation_error))) {
        ESP_LOGW(TAG, "Wi-Fi configuration validation failed: %s",
                 validation_error[0] ? validation_error : "unknown validation error");
        pvdg_ui_wifi_set_action_state(false,
            validation_error[0] ? validation_error : "Wi-Fi configuration is invalid.");
        return;
    }

    memset(&s_save_snapshot, 0, sizeof(s_save_snapshot));
    if (config_manager_get_snapshot(&s_save_snapshot) != ESP_OK) {
        ESP_LOGE(TAG, "Current configuration snapshot unavailable during Save & Connect");
        pvdg_ui_wifi_set_action_state(false, "Current controller configuration is unavailable.");
        return;
    }

    ui_to_profile(&config->primary, &s_save_snapshot.wifi.primary, &s_save_snapshot.wifi.primary);
    ui_to_profile(&config->fallback, &s_save_snapshot.wifi.fallback, &s_save_snapshot.wifi.fallback);
    s_save_snapshot.wifi.scan_before_connect = config->scan_before_connect;
    s_save_snapshot.wifi.fallback_ap_enabled = config->fallback_ap_enabled;
    copy_text(s_save_snapshot.wifi.fallback_ap_ssid, sizeof(s_save_snapshot.wifi.fallback_ap_ssid),
              config->fallback_ap_ssid);
    if (config->fallback_ap_password[0] &&
        strcmp(config->fallback_ap_password, MASKED_PASSWORD) != 0) {
        copy_text(s_save_snapshot.wifi.fallback_ap_password,
                  sizeof(s_save_snapshot.wifi.fallback_ap_password),
                  config->fallback_ap_password);
    }
    s_save_snapshot.wifi.max_retries_per_profile = config->max_retries_per_profile;
    s_save_snapshot.wifi.reconnect_backoff_ms = config->reconnect_backoff_ms;

    /* A network change can invalidate every meter/inverter transport. Match the
     * web commissioning safety contract: remove command authority first, then
     * persist control.enabled=false together with the new network settings. */
    s_save_snapshot.control.enabled = false;
    control_engine_force_disable();
    if (config_manager_save(&s_save_snapshot) != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi configuration persistence failed for SSID '%s'",
                 config->primary.ssid);
        pvdg_ui_wifi_set_action_state(false,
            "Wi-Fi configuration could not be persisted. Control remains disabled.");
        return;
    }

    ESP_LOGI(TAG, "Wi-Fi configuration persisted for SSID '%s'; restarting to reload network profile",
             config->primary.ssid);

    /* Clear the entered credential only after the protected configuration write
     * has actually succeeded. The restart reloads network_manager's cached
     * profile from NVS and applies the new SSID/password cleanly. */
    pvdg_ui_wifi_clear_password_input();
    pvdg_ui_wifi_set_action_state(true,
        "Wi-Fi saved. Control disabled; restarting now to connect to the selected network...");
    if (xTaskCreate(restart_task, "wifi_restart", 2048, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Restart task could not be created after Wi-Fi save");
        pvdg_ui_wifi_set_action_state(false,
            "Wi-Fi saved. Restart controller manually to connect; control remains disabled.");
    }
}

static void log_scan_transition(void)
{
    if (s_logged_scan_valid &&
        s_logged_scan_generation == s_scan_snapshot.generation &&
        s_logged_scan_state == s_scan_snapshot.state) {
        return;
    }

    s_logged_scan_valid = true;
    s_logged_scan_generation = s_scan_snapshot.generation;
    s_logged_scan_state = s_scan_snapshot.state;

    switch (s_scan_snapshot.state) {
    case NETWORK_SCAN_RUNNING:
        ESP_LOGI(TAG, "Panel scan generation %u running",
                 (unsigned)s_scan_snapshot.generation);
        break;
    case NETWORK_SCAN_COMPLETE:
        ESP_LOGI(TAG, "Panel scan generation %u complete: %u unique network(s)",
                 (unsigned)s_scan_snapshot.generation,
                 (unsigned)s_scan_snapshot.count);
        break;
    case NETWORK_SCAN_FAILED:
        ESP_LOGW(TAG, "Panel scan generation %u failed: %s",
                 (unsigned)s_scan_snapshot.generation,
                 esp_err_to_name(s_scan_snapshot.last_error));
        break;
    case NETWORK_SCAN_IDLE:
    default:
        break;
    }
}

void wifi_runtime_bridge_refresh(pvdg_ui_model_t *model,
                                 pvdg_ui_wifi_scan_t *scan)
{
    if (!model || !scan) return;

    /* screen_app initializes the generic Wi-Fi surface fail-closed. Restore the
     * real Product Core baseline whenever the Wi-Fi page is rendered so Save is
     * enabled only against a validated current configuration. */
    memset(&s_baseline_snapshot, 0, sizeof(s_baseline_snapshot));
    if (wifi_runtime_bridge_load_config(&s_baseline_snapshot)) {
        pvdg_ui_wifi_set_config_snapshot(&s_baseline_snapshot, true);
    }

    network_status_t status = {0};
    network_manager_get_status(&status);
    model->network.online = status.network_ready;
    model->network.rssi = status.rssi;
    copy_text(model->network.ssid, sizeof(model->network.ssid), status.ssid);
    copy_text(model->network.ip, sizeof(model->network.ip), status.ip);
    fill_live_sta_identity(model);

    memset(&s_scan_snapshot, 0, sizeof(s_scan_snapshot));
    network_manager_get_scan_snapshot(&s_scan_snapshot);
    log_scan_transition();

    memset(scan, 0, sizeof(*scan));
    scan->scan_running = s_scan_snapshot.state == NETWORK_SCAN_RUNNING;
    scan->scan_failed = s_scan_snapshot.state == NETWORK_SCAN_FAILED;
    scan->generation = s_scan_snapshot.generation;

    uint16_t count = s_scan_snapshot.count;
    if (count > PVDG_UI_WIFI_MAX_NETWORKS) count = PVDG_UI_WIFI_MAX_NETWORKS;
    scan->count = (uint8_t)count;

    for (uint16_t i = 0U; i < count; ++i) {
        const network_scan_ap_t *source = &s_scan_snapshot.results[i];
        pvdg_ui_wifi_network_t *target = &scan->networks[i];
        const char *security = NULL;
        bool secure = true;
        bool supported = false;

        security_info(source->auth_mode, &security, &secure, &supported);
        copy_text(target->ssid, sizeof(target->ssid), source->ssid);
        target->rssi = source->rssi;
        target->channel = source->channel;
        target->auth_mode = source->auth_mode;
        copy_text(target->security, sizeof(target->security), security);
        target->connected = source->connected;
        target->configured_primary = source->configured_primary;
        target->configured_fallback = source->configured_fallback;
        target->secure = secure;
        target->supported = supported;
    }

    if (s_scan_snapshot.state == NETWORK_SCAN_RUNNING) {
        pvdg_ui_wifi_set_action_state(true, "Scanning for Wi-Fi networks...");
    } else if (s_scan_snapshot.state == NETWORK_SCAN_COMPLETE) {
        char message[80];
        if (s_scan_snapshot.count > PVDG_UI_WIFI_MAX_NETWORKS) {
            snprintf(message, sizeof(message), "%u networks found; showing strongest %u.",
                     (unsigned)s_scan_snapshot.count,
                     (unsigned)PVDG_UI_WIFI_MAX_NETWORKS);
        } else {
            snprintf(message, sizeof(message), "%u network%s found.",
                     (unsigned)scan->count, scan->count == 1U ? "" : "s");
        }
        pvdg_ui_wifi_set_action_state(false, message);
    } else if (s_scan_snapshot.state == NETWORK_SCAN_FAILED) {
        pvdg_ui_wifi_set_action_state(false, "Wi-Fi scan failed. Previous results kept; tap Scan to retry.");
    }

    /* Authorization is intentionally checked live. An expired Engineering
     * session immediately disables Save & Connect; no network write bypasses
     * the existing commissioning security contract. */
    pvdg_ui_wifi_set_write_authorized(engineering_runtime_bridge_is_authorized());
}

void wifi_runtime_bridge_request_scan(void *user)
{
    (void)user;
    esp_err_t err = network_manager_request_scan();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Panel requested Wi-Fi scan");
        pvdg_ui_wifi_set_action_state(true, "Scanning for Wi-Fi networks...");
    } else if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Panel scan rejected because Wi-Fi radio is busy");
        pvdg_ui_wifi_set_action_state(false,
                                      "Wi-Fi radio is busy connecting. Retry Scan shortly.");
    } else {
        ESP_LOGW(TAG, "Panel scan request failed: %s", esp_err_to_name(err));
        pvdg_ui_wifi_set_action_state(false, "Unable to start Wi-Fi scan.");
    }
}

void wifi_runtime_bridge_request_reconnect(void *user)
{
    (void)user;
    esp_err_t err = network_manager_rescan_and_connect();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Panel requested reconnect using the persisted profile");
        pvdg_ui_wifi_set_action_state(true, "Wi-Fi reconnect requested...");
    } else {
        ESP_LOGW(TAG, "Panel reconnect request failed: %s", esp_err_to_name(err));
        pvdg_ui_wifi_set_action_state(false, "Unable to start Wi-Fi reconnect.");
    }
}

/* Optional hooks consumed by pvdg_runtime_ui. Keeping these in the product host
 * preserves the runtime UI component's independence from Product Core APIs. */
void pvdg_ui_wifi_host_refresh(pvdg_ui_model_t *model,
                               pvdg_ui_wifi_scan_t *scan)
{
    wifi_runtime_bridge_refresh(model, scan);
}

void pvdg_ui_wifi_host_request_scan(void *user)
{
    wifi_runtime_bridge_request_scan(user);
}

void pvdg_ui_wifi_host_request_reconnect(void *user)
{
    wifi_runtime_bridge_request_reconnect(user);
}

bool pvdg_ui_wifi_host_load_config(pvdg_ui_wifi_config_t *config)
{
    return wifi_runtime_bridge_load_config(config);
}

void pvdg_ui_wifi_host_submit_config(const pvdg_ui_wifi_config_t *config,
                                     bool primary_changed,
                                     void *user)
{
    wifi_runtime_bridge_submit_config(config, primary_changed, user);
}
