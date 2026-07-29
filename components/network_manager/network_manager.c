#include "network_manager.h"
#include "network_scan_internal.h"
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
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "sdkconfig.h"

#define READY_BIT BIT0
#define MAX_SCAN_RESULTS 32
#define AP_RESCAN_INTERVAL_MS 15000
#define AP_RESCAN_MAX_INTERVAL_MS 240000
#define AP_RESCAN_MAX_SHIFT 4
#define OPERATOR_RESPONSE_DRAIN_MS 500
#define OPERATOR_ADMISSION_QUIET_MS 500
#define MANAGER_EVENT_QUEUE_LENGTH 8
#define MANAGER_WAKE_RADIO_EVENT BIT0
#define MANAGER_WAKE_OPERATOR BIT1
#define MANAGER_WAKE_CONNECT BIT2
#define MANAGER_WAKE_USER_SCAN BIT3

typedef enum {
    OPERATOR_RECONNECT_IDLE = 0,
    OPERATOR_RECONNECT_DRAINING,
    OPERATOR_RECONNECT_QUIESCING,
    OPERATOR_RECONNECT_DISCONNECTING
} operator_reconnect_phase_t;

typedef enum {
    OPERATOR_GATE_NO_CHANGE = 0,
    OPERATOR_GATE_STARTED_QUIET,
    OPERATOR_GATE_COMMITTED
} operator_gate_action_t;

typedef enum {
    MANAGER_EVENT_STA_START = 0,
    MANAGER_EVENT_STA_DISCONNECTED,
    MANAGER_EVENT_STA_GOT_IP
} manager_event_type_t;

typedef struct {
    manager_event_type_t type;
    uint8_t disconnect_reason;
    esp_netif_ip_info_t ip_info;
} manager_event_t;

static const char *TAG = "wifi_manager";
static EventGroupHandle_t s_events;
static QueueHandle_t s_event_queue;
static TaskHandle_t s_task;
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static app_wifi_config_t s_cfg;
static network_status_t s_status;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

/* These connection/recovery fields are owned exclusively by manager_task after
 * initialization. Event callbacks and HTTP handlers only enqueue/notify. */
static bool s_using_fallback;
static uint8_t s_retry_count;
static uint32_t s_failed_sweeps;
static bool s_force_primary_attempt;
static bool s_event_queue_overflow;

/* The HTTP task and Wi-Fi manager share this admission state. The same lock
 * serializes a late response handler against the final transition to
 * DISCONNECTING, eliminating the former read-then-act window. */
static bool s_operator_reconnect_pending;
static bool s_operator_reconnect_armed;
static uint16_t s_operator_response_inflight;
static TickType_t s_operator_last_response_complete_tick;
static TickType_t s_operator_quiescing_start_tick;
static operator_reconnect_phase_t s_operator_phase;

static void wake_manager(uint32_t bits)
{
    TaskHandle_t task = s_task;
    if (task) xTaskNotify(task, bits, eSetBits);
}

static void reset_operator_reconnect_locked(void)
{
    s_operator_reconnect_pending = false;
    s_operator_reconnect_armed = false;
    s_operator_response_inflight = 0;
    s_operator_last_response_complete_tick = 0;
    s_operator_quiescing_start_tick = 0;
    s_operator_phase = OPERATOR_RECONNECT_IDLE;
}

static void clear_operator_reconnect(void)
{
    portENTER_CRITICAL(&s_lock);
    reset_operator_reconnect_locked();
    portEXIT_CRITICAL(&s_lock);
}

static bool clear_operator_reconnect_if_disconnecting(void)
{
    bool cleared = false;
    portENTER_CRITICAL(&s_lock);
    if (s_operator_phase == OPERATOR_RECONNECT_DISCONNECTING) {
        reset_operator_reconnect_locked();
        cleared = true;
    }
    portEXIT_CRITICAL(&s_lock);
    return cleared;
}

static bool operator_disconnect_is_intentional(void)
{
    portENTER_CRITICAL(&s_lock);
    bool intentional = s_operator_reconnect_pending &&
                       s_operator_phase == OPERATOR_RECONNECT_DISCONNECTING;
    portEXIT_CRITICAL(&s_lock);
    return intentional;
}

static operator_reconnect_phase_t operator_phase(void)
{
    portENTER_CRITICAL(&s_lock);
    operator_reconnect_phase_t phase = s_operator_phase;
    portEXIT_CRITICAL(&s_lock);
    return phase;
}

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

    portENTER_CRITICAL(&s_lock);
    bool keep_ap = s_status.fallback_ap_active;
    portEXIT_CRITICAL(&s_lock);

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(keep_ap ? WIFI_MODE_APSTA : WIFI_MODE_STA), TAG, "STA mode failed");
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
        ESP_LOGI(TAG, "Scan found %u APs; primary '%s' %s, fallback '%s' %s",
                 (unsigned)count,
                 s_cfg.primary.ssid, *primary_found ? "visible" : "not visible",
                 s_cfg.fallback.enabled ? s_cfg.fallback.ssid : "(disabled)",
                 *fallback_found ? "visible" : "not visible");
    }
    free(records);
    return err;
}

static esp_err_t start_fallback_ap(void)
{
    if (!s_cfg.fallback_ap_enabled || !s_cfg.fallback_ap_ssid[0]) return ESP_ERR_INVALID_STATE;

    portENTER_CRITICAL(&s_lock);
    bool already_active = s_status.fallback_ap_active;
    portEXIT_CRITICAL(&s_lock);
    if (already_active) return ESP_OK;

    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, s_cfg.fallback_ap_ssid, sizeof(ap.ap.ssid));
    strlcpy((char *)ap.ap.password, s_cfg.fallback_ap_password, sizeof(ap.ap.password));
    ap.ap.ssid_len = strlen(s_cfg.fallback_ap_ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = strlen(s_cfg.fallback_ap_password) >= 8 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

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
    network_status_t status;
    network_manager_get_status(&status);
    const bool ap_active = status.fallback_ap_active;

    set_state(ap_active ? NETWORK_WIFI_AP_FALLBACK : NETWORK_WIFI_SCANNING);

    bool primary_found = true;
    bool fallback_found = true;
    if (s_cfg.scan_before_connect) {
        esp_err_t scan_err = scan_configured_networks(&primary_found, &fallback_found);
        if (scan_err != ESP_OK) {
            ESP_LOGW(TAG, "Wi-Fi scan unavailable: %s; attempting configured profiles", esp_err_to_name(scan_err));
            primary_found = true;
            fallback_found = true;
        }
    }

    /* Before the recovery AP is up, the primary profile is attempted even when
     * the scan did not see it (hidden SSID or marginal signal). Once the
     * recovery AP is serving, a blind attempt on an absent SSID is skipped so
     * the radio spends its time on the AP instead of doomed connect attempts;
     * boot and an operator rescan still force the attempt. */
    const bool force_primary = s_force_primary_attempt;
    s_force_primary_attempt = false;
    if (s_cfg.primary.enabled && (primary_found || !ap_active || force_primary)) {
        if (!primary_found) {
            ESP_LOGW(TAG, "Primary SSID '%s' not visible in scan; attempting it anyway", s_cfg.primary.ssid);
        }
        clear_operator_reconnect_if_disconnecting();
        if (connect_profile(&s_cfg.primary, false) == ESP_OK) return;
    }
    if (s_cfg.fallback.enabled && fallback_found) {
        clear_operator_reconnect_if_disconnecting();
        if (connect_profile(&s_cfg.fallback, true) == ESP_OK) return;
    }

    clear_operator_reconnect_if_disconnecting();
    if (ap_active) {
        set_state(NETWORK_WIFI_AP_FALLBACK);
        return;
    }

    esp_err_t ap_err = start_fallback_ap();
    if (ap_err != ESP_OK) {
        set_state(NETWORK_WIFI_DISCONNECTED);
        ESP_LOGE(TAG, "No usable Wi-Fi profile and fallback AP failed: %s", esp_err_to_name(ap_err));
    }
}

/* Bounded exponential backoff between STA sweeps while the recovery AP is up,
 * so a visible but permanently unusable SSID is not retried continuously. */
static uint32_t ap_retry_delay_ms(void)
{
    uint32_t shift = s_failed_sweeps < AP_RESCAN_MAX_SHIFT ? s_failed_sweeps : AP_RESCAN_MAX_SHIFT;
    uint32_t delay = AP_RESCAN_INTERVAL_MS << shift;
    return delay > AP_RESCAN_MAX_INTERVAL_MS ? AP_RESCAN_MAX_INTERVAL_MS : delay;
}

static TickType_t operator_gate_timeout_ticks(void)
{
    TickType_t timeout = portMAX_DELAY;
    TickType_t now = xTaskGetTickCount();
    const TickType_t drain_ticks = pdMS_TO_TICKS(OPERATOR_RESPONSE_DRAIN_MS);
    const TickType_t quiet_ticks = pdMS_TO_TICKS(OPERATOR_ADMISSION_QUIET_MS);

    portENTER_CRITICAL(&s_lock);
    if (s_operator_phase == OPERATOR_RECONNECT_DRAINING &&
        s_operator_reconnect_pending && s_operator_reconnect_armed &&
        s_operator_response_inflight == 0) {
        TickType_t elapsed = now - s_operator_last_response_complete_tick;
        timeout = elapsed >= drain_ticks ? 0 : drain_ticks - elapsed;
    } else if (s_operator_phase == OPERATOR_RECONNECT_QUIESCING &&
               s_operator_reconnect_pending && s_operator_reconnect_armed &&
               s_operator_response_inflight == 0) {
        TickType_t elapsed = now - s_operator_quiescing_start_tick;
        timeout = elapsed >= quiet_ticks ? 0 : quiet_ticks - elapsed;
    }
    portEXIT_CRITICAL(&s_lock);
    return timeout;
}

static operator_gate_action_t operator_gate_step(void)
{
    operator_gate_action_t action = OPERATOR_GATE_NO_CHANGE;
    TickType_t now = xTaskGetTickCount();
    const TickType_t drain_ticks = pdMS_TO_TICKS(OPERATOR_RESPONSE_DRAIN_MS);
    const TickType_t quiet_ticks = pdMS_TO_TICKS(OPERATOR_ADMISSION_QUIET_MS);

    portENTER_CRITICAL(&s_lock);
    if (s_operator_phase == OPERATOR_RECONNECT_DRAINING &&
        s_operator_reconnect_pending && s_operator_reconnect_armed &&
        s_operator_response_inflight == 0 &&
        (TickType_t)(now - s_operator_last_response_complete_tick) >= drain_ticks) {
        s_operator_phase = OPERATOR_RECONNECT_QUIESCING;
        s_operator_quiescing_start_tick = now;
        action = OPERATOR_GATE_STARTED_QUIET;
    } else if (s_operator_phase == OPERATOR_RECONNECT_QUIESCING &&
               s_operator_reconnect_pending && s_operator_reconnect_armed &&
               s_operator_response_inflight == 0 &&
               (TickType_t)(now - s_operator_quiescing_start_tick) >= quiet_ticks) {
        /* This phase transition and response_begin() serialize on s_lock. A
         * request either reopens DRAINING first, or observes DISCONNECTING and is
         * not admitted. There is no read-then-act admission window. */
        s_operator_phase = OPERATOR_RECONNECT_DISCONNECTING;
        action = OPERATOR_GATE_COMMITTED;
    }
    portEXIT_CRITICAL(&s_lock);

    if (action == OPERATOR_GATE_STARTED_QUIET) {
        ESP_LOGI(TAG, "Operator reconnect response drain complete; admission quiet started");
    } else if (action == OPERATOR_GATE_COMMITTED) {
        ESP_LOGI(TAG, "Operator reconnect admission gate closed; radio transition committed");
    }
    return action;
}

static bool tick_due(TickType_t now, TickType_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static TickType_t ticks_until(TickType_t now, TickType_t deadline)
{
    return tick_due(now, deadline) ? 0 : deadline - now;
}

static TickType_t earlier_timeout(TickType_t left, TickType_t right)
{
    if (left == portMAX_DELAY) return right;
    if (right == portMAX_DELAY) return left;
    return left < right ? left : right;
}

static void schedule_connect(bool *pending, TickType_t *deadline,
                             uint32_t delay_ms, bool replace)
{
    if (!pending || !deadline) return;
    TickType_t candidate = xTaskGetTickCount() + pdMS_TO_TICKS(delay_ms);
    if (!*pending || replace || (int32_t)(candidate - *deadline) < 0) {
        *pending = true;
        *deadline = candidate;
    }
    wake_manager(MANAGER_WAKE_CONNECT);
}

static void begin_operator_reconnect(bool *connect_pending,
                                     TickType_t *connect_deadline)
{
    if (operator_phase() != OPERATOR_RECONNECT_DISCONNECTING) return;

    s_failed_sweeps = 0;
    s_retry_count = 0;
    s_force_primary_attempt = true;

    esp_err_t scan_stop = esp_wifi_scan_stop();
    if (scan_stop != ESP_OK && scan_stop != ESP_ERR_WIFI_STATE) {
        ESP_LOGW(TAG, "Scan stop before operator reconnect returned %s", esp_err_to_name(scan_stop));
    }

    const bool associated = (xEventGroupGetBits(s_events) & READY_BIT) != 0;
    set_state(NETWORK_WIFI_SCANNING);

    if (!associated) {
        schedule_connect(connect_pending, connect_deadline,
                         s_cfg.reconnect_backoff_ms, true);
        return;
    }

    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK) {
        clear_operator_reconnect();
        set_state(NETWORK_WIFI_CONNECTED);
        ESP_LOGE(TAG, "Operator reconnect could not disconnect the station: %s", esp_err_to_name(err));
    }
}

static void perform_connect_cycle(bool *connect_pending,
                                  TickType_t *connect_deadline)
{
    choose_and_connect();

    network_status_t status;
    network_manager_get_status(&status);
    if (status.state == NETWORK_WIFI_AP_FALLBACK) {
        schedule_connect(connect_pending, connect_deadline,
                         ap_retry_delay_ms(), true);
    }
}

static bool normal_connect_allowed(void)
{
    operator_reconnect_phase_t phase = operator_phase();
    bool ready = (xEventGroupGetBits(s_events) & READY_BIT) != 0;
    return phase == OPERATOR_RECONNECT_IDLE ||
           (phase == OPERATOR_RECONNECT_DISCONNECTING && !ready);
}

static void handle_sta_disconnected(uint8_t reason,
                                    bool *connect_pending,
                                    TickType_t *connect_deadline)
{
    xEventGroupClearBits(s_events, READY_BIT);
    portENTER_CRITICAL(&s_lock);
    s_status.network_ready = false;
    s_status.disconnect_count++;
    strlcpy(s_status.ip, "0.0.0.0", sizeof(s_status.ip));
    portEXIT_CRITICAL(&s_lock);

    if (operator_disconnect_is_intentional()) {
        ESP_LOGI(TAG, "Intentional disconnect acknowledged (reason=%u); operator reconnect in progress",
                 (unsigned)reason);
        set_state(NETWORK_WIFI_SCANNING);
        schedule_connect(connect_pending, connect_deadline,
                         s_cfg.reconnect_backoff_ms, true);
        return;
    }

    s_retry_count++;
    if (s_retry_count <= s_cfg.max_retries_per_profile) {
        ESP_LOGW(TAG, "Disconnected from %s profile, reason=%u; retry %u/%u",
                 s_using_fallback ? "fallback" : "primary",
                 (unsigned)reason,
                 (unsigned)s_retry_count,
                 (unsigned)s_cfg.max_retries_per_profile);
        set_state(s_using_fallback ? NETWORK_WIFI_CONNECTING_FALLBACK
                                   : NETWORK_WIFI_CONNECTING_PRIMARY);
        esp_err_t retry_error = esp_wifi_connect();
        if (retry_error != ESP_OK) {
            ESP_LOGW(TAG, "Immediate station retry failed: %s", esp_err_to_name(retry_error));
            schedule_connect(connect_pending, connect_deadline,
                             s_cfg.reconnect_backoff_ms, true);
        }
        return;
    }

    if (!s_using_fallback && s_cfg.fallback.enabled) {
        ESP_LOGW(TAG, "Primary profile exhausted %u attempts (last reason=%u); switching to fallback SSID '%s'",
                 (unsigned)s_cfg.max_retries_per_profile,
                 (unsigned)reason,
                 s_cfg.fallback.ssid);
        if (connect_profile(&s_cfg.fallback, true) != ESP_OK) {
            schedule_connect(connect_pending, connect_deadline,
                             s_cfg.reconnect_backoff_ms, true);
        }
        return;
    }

    ESP_LOGW(TAG, "All configured STA profiles exhausted (last reason=%u); enabling recovery AP",
             (unsigned)reason);
    if (s_failed_sweeps < UINT32_MAX) s_failed_sweeps++;
    esp_err_t ap_error = start_fallback_ap();
    if (ap_error != ESP_OK) {
        set_state(NETWORK_WIFI_DISCONNECTED);
        ESP_LOGE(TAG, "Recovery AP start failed: %s", esp_err_to_name(ap_error));
    }
    schedule_connect(connect_pending, connect_deadline,
                     ap_retry_delay_ms(), true);
}

static void handle_sta_got_ip(const esp_netif_ip_info_t *ip_info)
{
    if (!ip_info) return;

    wifi_ap_record_t ap = {0};
    esp_err_t ap_info_error = esp_wifi_sta_get_ap_info(&ap);

    portENTER_CRITICAL(&s_lock);
    bool ap_was_active = s_status.fallback_ap_active;
    portEXIT_CRITICAL(&s_lock);

    bool ap_stopped = !ap_was_active;
    if (ap_was_active) {
        ESP_LOGI(TAG, "STA connected; stopping recovery AP");
        esp_err_t mode_error = esp_wifi_set_mode(WIFI_MODE_STA);
        ap_stopped = mode_error == ESP_OK;
        if (mode_error != ESP_OK) {
            ESP_LOGW(TAG, "Recovery AP could not be stopped: %s", esp_err_to_name(mode_error));
        }
    }

    portENTER_CRITICAL(&s_lock);
    s_status.state = NETWORK_WIFI_CONNECTED;
    s_status.network_ready = true;
    s_status.using_fallback_sta = s_using_fallback;
    s_status.fallback_ap_active = !ap_stopped;
    strlcpy(s_status.ssid,
            s_using_fallback ? s_cfg.fallback.ssid : s_cfg.primary.ssid,
            sizeof(s_status.ssid));
    snprintf(s_status.ip, sizeof(s_status.ip), IPSTR, IP2STR(&ip_info->ip));
    snprintf(s_status.gateway, sizeof(s_status.gateway), IPSTR, IP2STR(&ip_info->gw));
    snprintf(s_status.netmask, sizeof(s_status.netmask), IPSTR, IP2STR(&ip_info->netmask));
    s_status.rssi = ap_info_error == ESP_OK ? ap.rssi : 0;
    s_status.reconnect_count++;
    portEXIT_CRITICAL(&s_lock);

    s_failed_sweeps = 0;
    s_retry_count = 0;
    clear_operator_reconnect_if_disconnecting();
    xEventGroupSetBits(s_events, READY_BIT);

    network_status_t status;
    network_manager_get_status(&status);
    ESP_LOGI(TAG, "Ready: SSID=%s IP=%s GW=%s MASK=%s RSSI=%d",
             status.ssid, status.ip, status.gateway, status.netmask, status.rssi);
}

static void recover_event_queue_overflow(bool *connect_pending,
                                         TickType_t *connect_deadline)
{
    bool overflow = false;
    portENTER_CRITICAL(&s_lock);
    overflow = s_event_queue_overflow;
    s_event_queue_overflow = false;
    portEXIT_CRITICAL(&s_lock);
    if (!overflow) return;

    ESP_LOGE(TAG, "Wi-Fi event queue overflow; forcing a fail-safe reconnect");
    xEventGroupClearBits(s_events, READY_BIT);
    portENTER_CRITICAL(&s_lock);
    s_status.network_ready = false;
    strlcpy(s_status.ip, "0.0.0.0", sizeof(s_status.ip));
    portEXIT_CRITICAL(&s_lock);
    s_retry_count = 0;
    s_failed_sweeps = 0;
    s_force_primary_attempt = true;
    esp_err_t error = esp_wifi_disconnect();
    if (error != ESP_OK && error != ESP_ERR_WIFI_NOT_CONNECT) {
        ESP_LOGW(TAG, "Fail-safe disconnect returned %s", esp_err_to_name(error));
    }
    schedule_connect(connect_pending, connect_deadline, 0, true);
}

static void drain_radio_events(bool *connect_pending,
                               TickType_t *connect_deadline)
{
    manager_event_t event;
    while (xQueueReceive(s_event_queue, &event, 0) == pdTRUE) {
        switch (event.type) {
            case MANAGER_EVENT_STA_START:
                schedule_connect(connect_pending, connect_deadline,
                                 s_cfg.reconnect_backoff_ms, false);
                break;
            case MANAGER_EVENT_STA_DISCONNECTED:
                handle_sta_disconnected(event.disconnect_reason,
                                        connect_pending, connect_deadline);
                break;
            case MANAGER_EVENT_STA_GOT_IP:
                *connect_pending = false;
                handle_sta_got_ip(&event.ip_info);
                break;
            default:
                break;
        }
    }
    recover_event_queue_overflow(connect_pending, connect_deadline);
}

static void manager_task(void *arg)
{
    (void)arg;
    bool connect_pending = false;
    TickType_t connect_deadline = 0;

    while (true) {
        TickType_t now = xTaskGetTickCount();
        TickType_t reconnect_timeout = connect_pending
                                           ? ticks_until(now, connect_deadline)
                                           : portMAX_DELAY;
        TickType_t timeout = earlier_timeout(operator_gate_timeout_ticks(),
                                             reconnect_timeout);
        uint32_t notifications = 0;
        xTaskNotifyWait(0, UINT32_MAX, &notifications, timeout);

        if ((notifications & MANAGER_WAKE_RADIO_EVENT) != 0U) {
            drain_radio_events(&connect_pending, &connect_deadline);
        }

        operator_gate_action_t action = operator_gate_step();
        if (action == OPERATOR_GATE_COMMITTED) {
            connect_pending = false;
            begin_operator_reconnect(&connect_pending, &connect_deadline);
        }

        if ((notifications & MANAGER_WAKE_USER_SCAN) != 0U) {
            if (operator_phase() == OPERATOR_RECONNECT_IDLE) {
                network_scan_service_execute();
            } else {
                network_scan_service_reject(ESP_ERR_INVALID_STATE);
            }
        }

        now = xTaskGetTickCount();
        if (connect_pending && tick_due(now, connect_deadline) &&
            normal_connect_allowed()) {
            connect_pending = false;
            perform_connect_cycle(&connect_pending, &connect_deadline);
        }
    }
}

static void enqueue_manager_event(const manager_event_t *event)
{
    if (!event || !s_event_queue) return;
    if (xQueueSend(s_event_queue, event, 0) != pdTRUE) {
        portENTER_CRITICAL(&s_lock);
        s_event_queue_overflow = true;
        portEXIT_CRITICAL(&s_lock);
    }
    wake_manager(MANAGER_WAKE_RADIO_EVENT);
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    manager_event_t event = {0};

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        event.type = MANAGER_EVENT_STA_START;
        enqueue_manager_event(&event);
        return;
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disconnected = data;
        event.type = MANAGER_EVENT_STA_DISCONNECTED;
        event.disconnect_reason = disconnected ? disconnected->reason : 0;
        enqueue_manager_event(&event);
        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *got_ip = data;
        if (!got_ip) return;
        event.type = MANAGER_EVENT_STA_GOT_IP;
        event.ip_info = got_ip->ip_info;
        enqueue_manager_event(&event);
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
    s_using_fallback = false;
    s_retry_count = 0;
    s_failed_sweeps = 0;
    s_force_primary_attempt = false;
    s_event_queue_overflow = false;
    clear_operator_reconnect();

    ESP_LOGI(TAG, "Profiles: primary '%s'%s, fallback '%s'%s, recovery AP '%s'%s",
             s_cfg.primary.ssid, s_cfg.primary.enabled ? "" : " (disabled)",
             s_cfg.fallback.ssid, s_cfg.fallback.enabled ? "" : " (disabled)",
             s_cfg.fallback_ap_ssid, s_cfg.fallback_ap_enabled ? "" : " (disabled)");

    s_events = xEventGroupCreate();
    s_event_queue = xQueueCreate(MANAGER_EVENT_QUEUE_LENGTH,
                                 sizeof(manager_event_t));
    if (!s_events || !s_event_queue) return ESP_ERR_NO_MEM;

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_sta_netif || !s_ap_netif) return ESP_ERR_NO_MEM;
    esp_netif_set_hostname(s_sta_netif, CONFIG_PVDG_DEVICE_NAME);

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "Wi-Fi init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "Wi-Fi storage mode failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL), TAG, "Wi-Fi handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL), TAG, "IP handler failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Wi-Fi mode failed");

    if (xTaskCreate(manager_task, "wifi_manager", 6144, NULL, 12, &s_task) != pdPASS) return ESP_ERR_NO_MEM;
    ESP_RETURN_ON_ERROR(network_scan_service_init(s_task, MANAGER_WAKE_USER_SCAN),
                        TAG, "scan service init failed");
    return esp_wifi_start();
}

bool network_manager_is_connected(void)
{
    return s_events && (xEventGroupGetBits(s_events) & READY_BIT) != 0;
}

bool network_manager_wait_ready(uint32_t timeout_ms)
{
    if (!s_events) return false;
    return (xEventGroupWaitBits(s_events, READY_BIT, pdFALSE, pdFALSE,
                                pdMS_TO_TICKS(timeout_ms)) & READY_BIT) != 0;
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

esp_err_t network_manager_operator_reconnect_response_begin(bool *accepted)
{
    if (!accepted) return ESP_ERR_INVALID_ARG;
    *accepted = false;
    if (!s_events || !s_task) return ESP_ERR_INVALID_STATE;

    esp_err_t result = ESP_OK;
    bool reopened = false;

    portENTER_CRITICAL(&s_lock);
    if (s_operator_phase == OPERATOR_RECONNECT_DISCONNECTING) {
        result = ESP_ERR_INVALID_STATE;
    } else if (s_operator_response_inflight == UINT16_MAX) {
        result = ESP_ERR_NO_MEM;
    } else {
        if (s_operator_phase == OPERATOR_RECONNECT_IDLE) {
            s_operator_reconnect_pending = true;
            s_operator_reconnect_armed = false;
            s_operator_last_response_complete_tick = 0;
            s_operator_quiescing_start_tick = 0;
            s_operator_phase = OPERATOR_RECONNECT_DRAINING;
            *accepted = true;
        } else if (s_operator_phase == OPERATOR_RECONNECT_QUIESCING) {
            s_operator_phase = OPERATOR_RECONNECT_DRAINING;
            s_operator_quiescing_start_tick = 0;
            reopened = true;
        }
        s_operator_response_inflight++;
    }
    portEXIT_CRITICAL(&s_lock);

    if (result == ESP_OK) {
        wake_manager(MANAGER_WAKE_OPERATOR);
        if (reopened) ESP_LOGI(TAG, "Late reconnect request reopened the response drain");
    }
    return result;
}

void network_manager_operator_reconnect_response_complete(bool accepted,
                                                          esp_err_t send_result)
{
    bool underflow = false;
    bool wake = false;
    TickType_t completed_at = xTaskGetTickCount();

    portENTER_CRITICAL(&s_lock);
    if (s_operator_response_inflight == 0) {
        underflow = true;
    } else {
        s_operator_response_inflight--;
        s_operator_last_response_complete_tick = completed_at;
        if (accepted && s_operator_reconnect_pending &&
            s_operator_phase != OPERATOR_RECONNECT_DISCONNECTING) {
            s_operator_reconnect_armed = true;
        }
        wake = s_operator_reconnect_pending;
    }
    portEXIT_CRITICAL(&s_lock);

    if (underflow) {
        ESP_LOGE(TAG, "Operator reconnect response completion underflow");
        return;
    }
    if (send_result != ESP_OK) {
        ESP_LOGW(TAG, "Operator reconnect HTTP acknowledgement was not delivered: %s",
                 esp_err_to_name(send_result));
    }
    if (wake) wake_manager(MANAGER_WAKE_OPERATOR);
}

esp_err_t network_manager_rescan_and_connect(void)
{
    bool accepted = false;
    esp_err_t err = network_manager_operator_reconnect_response_begin(&accepted);
    if (err != ESP_OK) return err;
    network_manager_operator_reconnect_response_complete(accepted, ESP_OK);
    return accepted ? ESP_OK : ESP_ERR_INVALID_STATE;
}
