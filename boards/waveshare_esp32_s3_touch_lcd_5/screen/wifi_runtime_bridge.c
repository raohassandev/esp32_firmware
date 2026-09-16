#include "wifi_runtime_bridge.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "network_manager.h"

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
    case 0:
        name = "Open";
        is_secure = false;
        is_supported = true;
        break;
    case 1:
        name = "WEP";
        break;
    case 2:
        name = "WPA-PSK";
        is_supported = true;
        break;
    case 3:
        name = "WPA2-PSK";
        is_supported = true;
        break;
    case 4:
        name = "WPA/WPA2-PSK";
        is_supported = true;
        break;
    case 5:
        name = "WPA2-Enterprise";
        break;
    case 6:
        name = "WPA3-PSK";
        is_supported = true;
        break;
    case 7:
        name = "WPA2/WPA3-PSK";
        is_supported = true;
        break;
    case 8:
        name = "WAPI-PSK";
        break;
    case 9:
        name = "OWE";
        is_secure = false;
        is_supported = true;
        break;
    case 10:
        name = "WPA3-Enterprise";
        break;
    default:
        break;
    }

    if (label) *label = name;
    if (secure) *secure = is_secure;
    if (supported) *supported = is_supported;
}

void wifi_runtime_bridge_refresh(pvdg_ui_model_t *model,
                                 pvdg_ui_wifi_scan_t *scan)
{
    if (!model || !scan) return;

    network_status_t status = {0};
    network_manager_get_status(&status);
    model->network.online = status.network_ready;
    model->network.rssi = status.rssi;
    copy_text(model->network.ssid, sizeof(model->network.ssid), status.ssid);
    copy_text(model->network.ip, sizeof(model->network.ip), status.ip);

    network_scan_snapshot_t snapshot = {0};
    network_manager_get_scan_snapshot(&snapshot);

    memset(scan, 0, sizeof(*scan));
    scan->scan_running = snapshot.state == NETWORK_SCAN_RUNNING;

    uint16_t count = snapshot.count;
    if (count > PVDG_UI_WIFI_MAX_NETWORKS) count = PVDG_UI_WIFI_MAX_NETWORKS;
    scan->count = (uint8_t)count;

    for (uint16_t i = 0U; i < count; ++i) {
        const network_scan_ap_t *source = &snapshot.results[i];
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

    if (snapshot.state == NETWORK_SCAN_RUNNING) {
        pvdg_ui_wifi_set_action_state(true, "Scanning for Wi-Fi networks...");
    } else if (snapshot.state == NETWORK_SCAN_COMPLETE) {
        char message[64];
        snprintf(message, sizeof(message), "%u network%s found.",
                 (unsigned)scan->count, scan->count == 1U ? "" : "s");
        pvdg_ui_wifi_set_action_state(false, message);
    } else if (snapshot.state == NETWORK_SCAN_FAILED) {
        pvdg_ui_wifi_set_action_state(false, "Wi-Fi scan failed. Tap Scan to retry.");
    }
}

void wifi_runtime_bridge_request_scan(void *user)
{
    (void)user;
    esp_err_t err = network_manager_request_scan();
    if (err == ESP_OK) {
        pvdg_ui_wifi_set_action_state(true, "Scanning for Wi-Fi networks...");
    } else if (err == ESP_ERR_INVALID_STATE) {
        pvdg_ui_wifi_set_action_state(false,
                                      "Scan unavailable while Wi-Fi is reconnecting. Retry shortly.");
    } else {
        pvdg_ui_wifi_set_action_state(false, "Unable to start Wi-Fi scan.");
    }
}

void wifi_runtime_bridge_request_reconnect(void *user)
{
    (void)user;
    esp_err_t err = network_manager_rescan_and_connect();
    if (err == ESP_OK) {
        pvdg_ui_wifi_set_action_state(true, "Wi-Fi reconnect requested...");
    } else {
        pvdg_ui_wifi_set_action_state(false, "Unable to start Wi-Fi reconnect.");
    }
}
