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
#include "sdkconfig.h"

#define READY_BIT BIT0
#define CONNECT_REQUEST_BIT BIT1
#define OPERATOR_RECONNECT_BIT BIT2
#define MAX_SCAN_RESULTS 32
#define AP_RESCAN_INTERVAL_MS 15000
#define AP_RESCAN_MAX_INTERVAL_MS 240000
#define AP_RESCAN_MAX_SHIFT 4
#define OPERATOR_RESPONSE_DRAIN_MS 500
#define OPERATOR_ADMISSION_QUIET_MS 500

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
static uint32_t s_failed_sweeps;
static volatile bool s_force_primary_attempt;

/* The HTTP task and Wi-Fi manager share this admission state. The same lock
 * serializes a late response handler against the final transition to
 * DISCONNECTING, eliminating the former read-then-act window. */
static bool s_operator_reconnect_pending;
static bool s_operator_reconnect_armed;
static uint16_t s_operator_response_inflight;
static TickType_t s_operator_last_response_complete_tick;
static TickType_t s_operator_quiescing_start_tick;
static operator_reconnect_phase_t s_operator_phase;

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
     * boot and an operator rescan still force the attempt.
     *
     * Only a reconnect that has already committed to DISCONNECTING is handed
     * back to the normal retry state machine here. A newly admitted HTTP request
     * may coexist with a normal connection sweep and must not be cleared. */
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

    /* No profile could be started, so this is a genuine recovery situation. */
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
 * so a visible but permanently unusable SSID (wrong credentials, reason 210) is
 * not retried every few seconds forever, flooding the log and consuming the
 * airtime the AP needs to serve its clients. */
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

static void begin_operator_reconnect(void)
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
        xEventGroupSetBits(s_events, CONNECT_REQUEST_BIT);
        return;
    }

    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK) {
        clear_operator_reconnect();
        set_state(NETWORK_WIFI_CONNECTED);
        ESP_LOGE(TAG, "Operator reconnect could not disconnect the station: %s", esp_err_to_name(err));
    }
}

static void run_connect_cycle(void)
{
    network_status_t status;
    network_manager_get_status(&status);
    uint32_t delay_ms = status.fallback_ap_active ? ap_retry_delay_ms() : s_cfg.reconnect_backoff_ms;
    if (status.fallback_ap_active) {
        wifi_mode_t mode = WIFI_MODE_NULL;
        esp_wifi_get_mode(&mode);
        ESP_LOGI(TAG, "Recovery AP '%s' serving on 192.168.4.1 (radio mode %d); next STA sweep in %u ms",
                 s_cfg.fallback_ap_ssid, (int)mode, (unsigned)delay_ms);
    }
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    choose_and_connect();

    network_manager_get_status(&status);
    if (status.state == NETWORK_WIFI_AP_FALLBACK) {
        xEventGroupSetBits(s_events, CONNECT_REQUEST_BIT);
    }
}

static bool normal_connect_allowed(void)
{
    operator_reconnect_phase_t phase = operator_phase();
    bool ready = (xEventGroupGetBits(s_events) & READY_BIT) != 0;
    return phase == OPERATOR_RECONNECT_IDLE ||
           (phase == OPERATOR_RECONNECT_DISCONNECTING && !ready);
}

static void manager_task(void *arg)
{
    (void)arg;
    bool connect_request_pending = false;

    while (true) {
        TickType_t timeout = operator_gate_timeout_ticks();
        EventBits_t bits = xEventGroupWaitBits(
            s_events,
            CONNECT_REQUEST_BIT | OPERATOR_RECONNECT_BIT,
            pdTRUE,
            pdFALSE,
            timeout);

        if ((bits & CONNECT_REQUEST_BIT) != 0) connect_request_pending = true;

        operator_gate_action_t action = operator_gate_step();
        if (action == OPERATOR_GATE_COMMITTED) {
            /* Any normal bit consumed before commitment is stale. An associated
             * STA produces a fresh bit from its intentional disconnect event;
             * an unassociated STA is signalled by begin_operator_reconnect(). */
            connect_request_pending = false;
            begin_operator_reconnect();
        }

        if (connect_request_pending && normal_connect_allowed()) {
            connect_request_pending = false;
            run_connect_cycle();
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

        /* Only a transition that atomically committed to DISCONNECTING owns this
         * event. A merely admitted/draining HTTP request must not suppress a
         * genuine link-loss retry that happened before radio teardown. */
        if (operator_disconnect_is_intentional()) {
            ESP_LOGI(TAG, "Intentional disconnect acknowledged (reason=%u); operator reconnect in progress",
                     event ? event->reason : 0);
            set_state(NETWORK_WIFI_SCANNING);
            xEventGroupSetBits(s_events, CONNECT_REQUEST_BIT);
            return;
        }

        s_retry_count++;
        if (s_retry_count <= s_cfg.max_retries_per_profile) {
            ESP_LOGW(TAG, "Disconnected from %s profile, reason=%u; retry %u/%u",
                     s_using_fallback ? "fallback" : "primary",
                     event ? event->reason : 0,
                     (unsigned)s_retry_count,
                     (unsigned)s_cfg.max_retries_per_profile);
            set_state(s_using_fallback ? NETWORK_WIFI_CONNECTING_FALLBACK : NETWORK_WIFI_CONNECTING_PRIMARY);
            esp_wifi_connect();
        } else if (!s_using_fallback && s_cfg.fallback.enabled) {
            ESP_LOGW(TAG, "Primary profile exhausted %u attempts (last reason=%u); switching to fallback SSID '%s'",
                     (unsigned)s_cfg.max_retries_per_profile,
                     event ? event->reason : 0,
                     s_cfg.fallback.ssid);
            if (connect_profile(&s_cfg.fallback, true) != ESP_OK) {
                xEventGroupSetBits(s_events, CONNECT_REQUEST_BIT);
            }
        } else {
            ESP_LOGW(TAG, "All configured STA profiles exhausted (last reason=%u); enabling recovery AP",
                     event ? event->reason : 0);
            if (s_failed_sweeps < UINT32_MAX) s_failed_sweeps++;
            if (start_fallback_ap() != ESP_OK) set_state(NETWORK_WIFI_DISCONNECTED);
            xEventGroupSetBits(s_events, CONNECT_REQUEST_BIT);
        }
        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        wifi_ap_record_t ap = {0};
        esp_wifi_sta_get_ap_info(&ap);

        portENTER_CRITICAL(&s_lock);
        bool ap_was_active = s_status.fallback_ap_active;
        portEXIT_CRITICAL(&s_lock);
        if (ap_was_active) {
            ESP_LOGI(TAG, "STA connected; stopping recovery AP");
            esp_wifi_set_mode(WIFI_MODE_STA);
        }

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

        s_failed_sweeps = 0;
        /* A successful association completes an operator transition only after
         * that transition committed to DISCONNECTING. A response still draining
         * during an unrelated connection attempt remains pending. */
        clear_operator_reconnect_if_disconnecting();
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
    clear_operator_reconnect();

    ESP_LOGI(TAG, "Profiles: primary '%s'%s, fallback '%s'%s, recovery AP '%s'%s",
             s_cfg.primary.ssid, s_cfg.primary.enabled ? "" : " (disabled)",
             s_cfg.fallback.ssid, s_cfg.fallback.enabled ? "" : " (disabled)",
             s_cfg.fallback_ap_ssid, s_cfg.fallback_ap_enabled ? "" : " (disabled)");

    s_events = xEventGroupCreate();
    if (!s_events) return ESP_ERR_NO_MEM;

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

esp_err_t network_manager_operator_reconnect_response_begin(bool *accepted)
{
    if (!accepted) return ESP_ERR_INVALID_ARG;
    *accepted = false;
    if (!s_events) return ESP_ERR_INVALID_STATE;

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
            /* The admission decision and the manager's final commit use this
             * same lock. Entering first atomically reopens the full drain. */
            s_operator_phase = OPERATOR_RECONNECT_DRAINING;
            s_operator_quiescing_start_tick = 0;
            reopened = true;
        }
        s_operator_response_inflight++;
    }
    portEXIT_CRITICAL(&s_lock);

    if (result == ESP_OK) {
        xEventGroupSetBits(s_events, OPERATOR_RECONNECT_BIT);
        if (reopened) ESP_LOGI(TAG, "Late reconnect request reopened the response drain");
    }
    return result;
}

void network_manager_operator_reconnect_response_complete(bool accepted, esp_err_t send_result)
{
    bool underflow = false;
    bool wake_manager = false;
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
        wake_manager = s_operator_reconnect_pending;
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
    if (wake_manager) xEventGroupSetBits(s_events, OPERATOR_RECONNECT_BIT);
}

esp_err_t network_manager_rescan_and_connect(void)
{
    bool accepted = false;
    esp_err_t err = network_manager_operator_reconnect_response_begin(&accepted);
    if (err != ESP_OK) return err;
    network_manager_operator_reconnect_response_complete(accepted, ESP_OK);
    return accepted ? ESP_OK : ESP_ERR_INVALID_STATE;
}
