#include "network_manager.h"
#include <stdio.h>
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

    esp_netif_dhcpc_stop(s_sta_netif);
    esp_netif_ip_info_t info = {0};
    if (!parse_ip(profile->static_ip, &info.ip) || !parse_ip(profile->gateway, &info.gw) ||
        !parse_ip(profile->netmask, &info.netmask)) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(esp_netif_set_ip_info(s_sta_netif, &info), TAG, "static IP failed");

    esp_netif_dns_info_t dns = {0};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    if (parse_ip(profile->dns1, &dns.ip.u_addr.ip4)) esp_netif_set_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &dns);
    if (parse_ip(profile->dns2, &dns.ip.u_addr.ip4)) esp_netif_set_dns_info(s_sta_netif, ESP_NETIF_DNS_BACKUP, &dns);
    return ESP_OK;
}

static esp_err_t connect_profile(const app_wifi_sta_profile_t *profile, bool fallback)
{
    if (!profile->enabled || !profile->ssid[0]) return ESP_ERR_INVALID_STATE;
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
    set_state(fallback ? NETWORK_WIFI_CONNECTING_FALLBACK : NETWORK_WIFI_CONNECTING_PRIMARY);
    ESP_LOGI(TAG, "Connecting to %s profile SSID '%s' using %s", fallback ? "fallback" : "primary",
             profile->ssid, profile->ip_mode == APP_WIFI_IP_STATIC ? "static IP" : "DHCP");
    return esp_wifi_connect();
}

static bool scan_has_ssid(const char *ssid)
{
    if (!ssid || !ssid[0]) return false;
    uint16_t count = 0;
    wifi_scan_config_t scan = {.show_hidden = true};
    if (esp_wifi_scan_start(&scan, true) != ESP_OK || esp_wifi_scan_get_ap_num(&count) != ESP_OK || count == 0) return false;
    if (count > 32) count = 32;
    wifi_ap_record_t records[32];
    if (esp_wifi_scan_get_ap_records(&count, records) != ESP_OK) return false;
    for (uint16_t i = 0; i < count; ++i) if (strcmp((char *)records[i].ssid, ssid) == 0) return true;
    return false;
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
    portENTER_CRITICAL(&s_lock);
    s_status.fallback_ap_active = true;
    s_status.state = NETWORK_WIFI_AP_FALLBACK;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGW(TAG, "No configured STA available; fallback AP '%s' active", s_cfg.fallback_ap_ssid);
    return ESP_OK;
}

static void choose_and_connect(void)
{
    set_state(NETWORK_WIFI_SCANNING);
    bool primary_found = !s_cfg.scan_before_connect || scan_has_ssid(s_cfg.primary.ssid);
    bool fallback_found = !s_cfg.scan_before_connect || scan_has_ssid(s_cfg.fallback.ssid);
    if (s_cfg.primary.enabled && primary_found && connect_profile(&s_cfg.primary, false) == ESP_OK) return;
    if (s_cfg.fallback.enabled && fallback_found && connect_profile(&s_cfg.fallback, true) == ESP_OK) return;
    start_fallback_ap();
}

static void manager_task(void *arg)
{
    (void)arg;
    while (true) {
        xEventGroupWaitBits(s_events, CONNECT_REQUEST_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(s_cfg.reconnect_backoff_ms));
        choose_and_connect();
    }
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        xEventGroupSetBits(s_events, CONNECT_REQUEST_BIT);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_events, READY_BIT);
        portENTER_CRITICAL(&s_lock);
        s_status.network_ready = false;
        s_status.disconnect_count++;
        strlcpy(s_status.ip, "0.0.0.0", sizeof(s_status.ip));
        portEXIT_CRITICAL(&s_lock);
        if (++s_retry_count <= s_cfg.max_retries_per_profile) {
            set_state(s_using_fallback ? NETWORK_WIFI_CONNECTING_FALLBACK : NETWORK_WIFI_CONNECTING_PRIMARY);
            esp_wifi_connect();
        } else if (!s_using_fallback && s_cfg.fallback.enabled) {
            connect_profile(&s_cfg.fallback, true);
        } else {
            xEventGroupSetBits(s_events, CONNECT_REQUEST_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        wifi_ap_record_t ap = {0};
        esp_netif_ip_info_t info = event->ip_info;
        esp_wifi_sta_get_ap_info(&ap);
        portENTER_CRITICAL(&s_lock);
        s_status.state = NETWORK_WIFI_CONNECTED;
        s_status.network_ready = true;
        s_status.using_fallback_sta = s_using_fallback;
        s_status.fallback_ap_active = false;
        strlcpy(s_status.ssid, s_using_fallback ? s_cfg.fallback.ssid : s_cfg.primary.ssid, sizeof(s_status.ssid));
        snprintf(s_status.ip, sizeof(s_status.ip), IPSTR, IP2STR(&info.ip));
        snprintf(s_status.gateway, sizeof(s_status.gateway), IPSTR, IP2STR(&info.gw));
        snprintf(s_status.netmask, sizeof(s_status.netmask), IPSTR, IP2STR(&info.netmask));
        s_status.rssi = ap.rssi;
        s_status.reconnect_count++;
        portEXIT_CRITICAL(&s_lock);
        xEventGroupSetBits(s_events, READY_BIT);
        ESP_LOGI(TAG, "Ready: SSID=%s IP=%s GW=%s MASK=%s RSSI=%d", s_status.ssid, s_status.ip,
                 s_status.gateway, s_status.netmask, s_status.rssi);
    }
}

esp_err_t network_manager_init(void)
{
    app_config_t cfg;
    ESP_RETURN_ON_ERROR(config_manager_get_snapshot(&cfg), TAG, "configuration unavailable");
    s_cfg = cfg.wifi;
    memset(&s_status, 0, sizeof(s_status));
    strlcpy(s_status.ip, "0.0.0.0", sizeof(s_status.ip));
    s_events = xEventGroupCreate();
    if (!s_events) return ESP_ERR_NO_MEM;
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");
    esp_err_t err = esp_event_loop_create_default();
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
    if (xTaskCreate(manager_task, "wifi_manager", 4096, NULL, 12, &s_task) != pdPASS) return ESP_ERR_NO_MEM;
    return esp_wifi_start();
}

bool network_manager_is_connected(void){return (xEventGroupGetBits(s_events)&READY_BIT)!=0;}
bool network_manager_wait_ready(uint32_t timeout_ms){return (xEventGroupWaitBits(s_events,READY_BIT,pdFALSE,pdFALSE,pdMS_TO_TICKS(timeout_ms))&READY_BIT)!=0;}
const char *network_manager_get_ip(void){return s_status.ip;}
void network_manager_get_status(network_status_t *out){if(!out)return;portENTER_CRITICAL(&s_lock);*out=s_status;portEXIT_CRITICAL(&s_lock);}
esp_err_t network_manager_rescan_and_connect(void){if(!s_events)return ESP_ERR_INVALID_STATE;esp_wifi_disconnect();xEventGroupSetBits(s_events,CONNECT_REQUEST_BIT);return ESP_OK;}
