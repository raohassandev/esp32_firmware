#include "network_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config_manager.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/inet.h"

#define READY_BIT BIT0
#define CONNECT_REQUEST_BIT BIT1
#define MAX_SCAN_RESULTS 32
#define AP_RESCAN_INTERVAL_MS 15000

static const char *TAG = "wifi_manager";
static EventGroupHandle_t s_events;
static TaskHandle_t s_task;
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static app_wifi_config_t s_cfg;
static network_status_t s_status;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_using_fallback;
static uint8_t s_retry_count;

static void set_state(network_wifi_state_t state)
{
    portENTER_CRITICAL(&s_lock);
    s_status.state = state;
    portEXIT_CRITICAL(&s_lock);
}

static bool parse_ip(const char *text, esp_ip4_addr_t *out)
{
    if (!text || !text[0] || !out) return false;
    ip4_addr_t parsed = {0};
    if (ip4addr_aton(text, &parsed) == 0) return false;
    out->addr = parsed.addr;
    return true;
}

static esp_err_t apply_ip_profile(const app_wifi_sta_profile_t *profile)
{
    if (profile->ip_mode == APP_WIFI_IP_DHCP) {
        esp_err_t err = esp_netif_dhcpc_start(s_sta_netif);
        return (err == ESP_OK || err == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) ? ESP_OK : err;
    }

    esp_err_t err = esp_netif_dhcpc_stop(s_sta_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) return err;

    esp_netif_ip_info_t info = {0};
    if (!parse_ip(profile->static_ip, &info.ip) ||
        !parse_ip(profile->gateway, &info.gw) ||
        !parse_ip(profile->netmask, &info.netmask)) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(esp_netif_set_ip_info(s_sta_netif, &info), TAG, "static IP failed");

    esp_netif_dns_info_t dns = {0};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    if (parse_ip(profile->dns1, &dns.ip.u_addr.ip4)) {
        ESP_RETURN_ON_ERROR(esp_netif_set_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &dns), TAG, "primary DNS failed");
    }
    if (parse_ip(profile->dns2, &dns.ip.u_addr.ip4)) {
        ESP_RETURN_ON_ERROR(esp_netif_set_dns_info(s_sta_netif, ESP_NETIF_DNS_BACKUP, &dns), TAG, "backup DNS failed");
    }
    return ESP_OK;
}

static esp_err_t connect_profile(const app_wifi_sta_profile_t *profile, bool fallback)
{
    if (!profile || !profile->enabled || !profile->ssid[0]) return ESP_ERR_INVALID_STATE;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "STA mode failed");
    ESP_RETURN_ON_ERROR(apply_ip_profile(profile), TAG, "IP configuration failed");

    wifi_config_t wifi = {0};
    strlcpy((char *)wifi.sta.ssid, profile->ssid, sizeof(wifi.sta.ssid));
    strlcpy((char *)wifi.sta.password, profile->password, sizeof(wifi.sta.password));
    wifi.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi.sta.threshold.authmode = WIFI_AUTH_OPEN;
    wifi.sta.pmf_cfg.capable = true;
    wifi.sta.pmf_cfg.required = false;
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi), TAG, "STA config failed");

    s_using_fallback = fallback;
    s_retry_count = 0;
    portENTER_CRITICAL(&s_lock);
    s_status.fallback_ap_active = false;
    portEXIT_CRITICAL(&s_lock);
    set_state(fallback ? NETWORK_WIFI_CONNECTING_FALLBACK : NETWORK_WIFI_CONNECTING_PRIMARY);

    ESP_LOGI(TAG, "Connecting to %s SSID '%s' using %s",
             fallback ? "fallback" : "primary",
             profile->ssid,
             profile->ip_mode == APP_WIFI_IP_STATIC ? "static IP" : "DHCP");
    return esp_wifi_connect();
}

static esp_err_t scan_configured_networks(bool *primary_found, bool *fallback_found)
{
    if (!primary_found || !fallback_found) return ESP_ERR_INVALID_ARG;
    *primary_found = false;
    *fallback_found = false;

    wifi_scan_config_t scan = {.show_hidden = true};
    ESP_RETURN_ON_ERROR(esp_wifi_scan_start(&scan, true), TAG, "Wi-Fi scan failed");

    uint16_t count = 0;
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_num(&count), TAG, "scan count failed");
    if (count == 0) return ESP_OK;
    if (count > MAX_SCAN_RESULTS) count = MAX_SCAN_RESULTS;

    wifi_ap_record_t *records = calloc(count, sizeof(*records));
    if (!records) return ESP_ERR_NO_MEM;

    esp_err_t err = esp_wifi_scan_get_ap_records(&count, records);
    if (err == ESP_OK) {
        for (uint16_t i = 0; i < count; ++i) {
            const char *ssid = (const char *)records[i].ssid;
            if (s_cfg.primary.enabled && strcmp(ssid, s_cfg.primary.ssid) == 0) *primary_found = true;
            if (s_cfg.fallback.enabled && strcmp(ssid, s_cfg.fallback.ssid) == 0) *fallback_found = true;
        }
    }
    free(records);
    return err;
}

static esp_err_t start_fallback_ap(void)
{
    if (!s_cfg.fallback_ap_enabled || !s_cfg.fallback_ap_ssid[0]) return ESP_ERR_INVALID_STATE;

    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, s_cfg.fallback_ap_ssid, sizeof(ap.ap.ssid));
    strlcpy((char *)ap.ap.password, s_cfg.fallback_ap_password, sizeof(ap.ap.password));
    ap.ap.ssid_len = strlen((char *)ap.ap.ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = strlen((char *)ap.ap.password) >= 8 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "APSTA mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap), TAG, "fallback AP config failed");

    xEventGroupClearBits(s_events, READY_BIT);
    portENTER_CRITICAL(&s_lock);
    s_status.network_ready = false;
    s_status.fallback_ap_active = true;
    s_status.state = NETWORK_WIFI_AP_FALLBACK;
    strlcpy(s_status.ssid, s_cfg.fallback_ap_ssid, sizeof(s_status.ssid));
    portEXIT_CRITICAL(&s_lock);

    ESP_LOGW(TAG, "No configured STA available; setup AP '%s' is active", s_cfg.fallback_ap_ssid);
    return ESP_OK;
}

static void choose_and_connect(void)
{
    set_state(NETWORK_WIFI_SCANNING);

    bool primary_found = !s_cfg.scan_before_connect;
    bool fallback_found = !s_cfg.scan_before_connect;
    if (s_cfg.scan_before_connect) {
        esp_err_t scan_err = scan_configured_networks(&primary_found, &fallback_found);
        if (scan_err != ESP_OK) {
            ESP_LOGW(TAG, "Wi-Fi scan unavailable: %s; attempting configured profiles", esp_err_to_name(scan_err));
            primary_found = s_cfg.primary.enabled;
            fallback_found = s_cfg.fallback.enabled;
        }
    }

    if (s_cfg.primary.enabled && primary_found) {
        if (connect_profile(&s_cfg.primary, false) == ESP_OK) return;
    }
    if (s_cfg.fallback.enabled && fallback_found) {
        if (connect_profile(&s_cfg.fallback, true) == ESP_OK) return;
    }

    esp_err_t ap_err = start_fallback_ap();
    if (ap_err != ESP_OK) {
        set_state(NETWORK_WIFI_DISCONNECTED);
        ESP_LOGE(TAG, "No usable Wi-Fi profile and fallback AP failed: %s", esp_err_to_name(ap_err));
    }
}

static void manager_task(void *arg)
{
    (void)arg;
    while (true) {
        xEventGroupWaitBits(s_events, CONNECT_REQUEST_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(s_cfg.reconnect_backoff_ms));
        choose_and_connect();

        network_status_t status;
        network_manager_get_status(&status);
        if (status.state == NETWORK_WIFI_AP_FALLBACK) {
            vTaskDelay(pdMS_TO_TICKS(AP_RESCAN_INTERVAL_MS));
            xEventGroupSetBits(s_events, CONNECT_REQUEST_BIT);
        }
    }
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        xEventGroupSetBits(s_events, CONNECT_REQUEST_BIT);
        return;
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = data;
        xEventGroupClearBits(s_events, READY_BIT);
        portENTER_CRITICAL(&s_lock);
        s_status.network_ready = false;
        s_status.disconnect_count++;
        strlcpy(s_status.ip, "0.0.0.0", sizeof(s_status.ip));
        portEXIT_CRITICAL(&s_lock);

        ESP_LOGW(TAG, "Disconnected from %s profile, reason=%u, retry=%u/%u",
                 s_using_fallback ? "fallback" : "primary",
                 event ? event->reason : 0,
                 (unsigned)(s_retry_count + 1),
                 (unsigned)s_cfg.max_retries_per_profile);

        if (++s_retry_count <= s_cfg.max_retries_per_profile) {
            set_state(s_using_fallback ? NETWORK_WIFI_CONNECTING_FALLBACK : NETWORK_WIFI_CONNECTING_PRIMARY);
            esp_wifi_connect();
        } else if (!s_using_fallback && s_cfg.fallback.enabled) {
            if (connect_profile(&s_cfg.fallback, true) != ESP_OK) {
                xEventGroupSetBits(s_events, CONNECT_REQUEST_BIT);
            }
        } else {
            xEventGroupSetBits(s_events, CONNECT_REQUEST_BIT);
        }
        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        wifi_ap_record_t ap = {0};
        esp_wifi_sta_get_ap_info(&ap);

        portENTER_CRITICAL(&s_lock);
        s_status.state = NETWORK_WIFI_CONNECTED;
        s_status.network_ready = true;
        s_status.using_fallback_sta = s_using_fallback;
        s_status.fallback_ap_active = false;
        strlcpy(s_status.ssid, s_using_fallback ? s_cfg.fallback.ssid : s_cfg.primary.ssid, sizeof(s_status.ssid));
        snprintf(s_status.ip, sizeof(s_status.ip), IPSTR, IP2STR(&event->ip_info.ip));
        snprintf(s_status.gateway, sizeof(s_status.gateway), IPSTR, IP2STR(&event->ip_info.gw));
        snprintf(s_status.netmask, sizeof(s_status.netmask), IPSTR, IP2STR(&event->ip_info.netmask));
        s_status.rssi = ap.rssi;
        s_status.reconnect_count++;
        portEXIT_CRITICAL(&s_lock);

        xEventGroupSetBits(s_events, READY_BIT);
        ESP_LOGI(TAG, "Ready: SSID=%s IP=%s GW=%s MASK=%s RSSI=%d",
                 s_status.ssid, s_status.ip, s_status.gateway, s_status.netmask, s_status.rssi);
    }
}

esp_err_t network_manager_init(void)
{
    app_config_t *cfg = malloc(sizeof(*cfg));
    if (!cfg) return ESP_ERR_NO_MEM;
    esp_err_t err = config_manager_get_snapshot(cfg);
    if (err != ESP_OK) {
        free(cfg);
        return err;
    }
    s_cfg = cfg->wifi;
    free(cfg);

    memset(&s_status, 0, sizeof(s_status));
    strlcpy(s_status.ip, "0.0.0.0", sizeof(s_status.ip));

    s_events = xEventGroupCreate();
    if (!s_events) return ESP_ERR_NO_MEM;

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_sta_netif || !s_ap_netif) return ESP_ERR_NO_MEM;

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "Wi-Fi init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "Wi-Fi storage mode failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL), TAG, "Wi-Fi handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL), TAG, "IP handler failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Wi-Fi mode failed");

    if (xTaskCreate(manager_task, "wifi_manager", 6144, NULL, 12, &s_task) != pdPASS) return ESP_ERR_NO_MEM;
    return esp_wifi_start();
}

bool network_manager_is_connected(void)
{
    return s_events && (xEventGroupGetBits(s_events) & READY_BIT) != 0;
}

bool network_manager_wait_ready(uint32_t timeout_ms)
{
    if (!s_events) return false;
    return (xEventGroupWaitBits(s_events, READY_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms)) & READY_BIT) != 0;
}

const char *network_manager_get_ip(void)
{
    return s_status.ip;
}

void network_manager_get_status(network_status_t *out)
{
    if (!out) return;
    portENTER_CRITICAL(&s_lock);
    *out = s_status;
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t network_manager_rescan_and_connect(void)
{
    if (!s_events) return ESP_ERR_INVALID_STATE;
    esp_wifi_disconnect();
    xEventGroupSetBits(s_events, CONNECT_REQUEST_BIT);
    return ESP_OK;
}
