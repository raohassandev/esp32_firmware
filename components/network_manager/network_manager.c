#include "network_manager.h"

#include "captive_portal.h"
#include "network_mdns.h"
#include "network_scan_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config_manager.h"
#include "device_identity.h"
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
/* The longest a disconnected controller may go without looking again.
 *
 * Was four minutes. The product owner set two: a site whose router has just come
 * back should rejoin promptly, and four minutes of silence on a controller that
 * looks dead is long enough for somebody to power-cycle it and conclude the
 * product is unreliable.
 *
 * The doubling backoff below it is kept. It exists so a VISIBLE but unusable
 * SSID -- wrong passphrase, saturated AP -- is not retried continuously, because
 * the recovery access point shares this radio and every scan takes it off air.
 * Two minutes is the ceiling of that backoff, not the interval: the first retry
 * is still 15 seconds after the sweep fails. */
#define AP_RESCAN_MAX_INTERVAL_MS 120000
#define AP_RESCAN_MAX_SHIFT 4

/* Home channel of the soft AP while the station is not associated. */
#define RECOVERY_AP_HOME_CHANNEL 1

/* Blind (scan-negative) station attempts exist only so a hidden SSID is not
 * permanently skipped. They cost the shared radio, and the recovery AP now
 * lives on that same radio, so they are limited to the first few sweeps after
 * boot or an operator action rather than repeated for ever. */
#define BLIND_ATTEMPT_SWEEPS 3
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
static bool s_force_blind_attempt;
/* True once the recovery AP has actually been configured and put on air. It is
 * the single answer to "may this code select APSTA?", so a unit whose AP was
 * refused (only possible with an unusable passphrase) never has a half-
 * configured, potentially unsecured AP switched back on by a later reconnect. */
static bool s_recovery_ap_on_air;
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

    /* The radio never leaves APSTA while the recovery AP is on air. Dropping to
     * WIFI_MODE_STA to chase a station - which is what this used to do - takes
     * the recovery AP down for the duration of the attempt, which is exactly
     * when an engineer is most likely to be looking for it. */
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(s_recovery_ap_on_air ? WIFI_MODE_APSTA : WIFI_MODE_STA),
                        TAG, "radio mode failed");
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

/* Brings up the recovery access point. Called once, from initialization, before
 * the radio is started - the AP is not a fallback and does not wait for the
 * station to fail.
 *
 * WPA2-PSK unconditionally. There is no branch that produces WIFI_AUTH_OPEN:
 * this network is now permanently on an industrial controller, and an open
 * permanently-on AP would put the web server on air for anyone in range. If the
 * passphrase is somehow too short for WPA2 the AP is refused outright rather
 * than downgraded, and config_manager_save() will not persist such a
 * configuration in the first place. */
static esp_err_t start_recovery_ap(void)
{
    if (!s_cfg.fallback_ap_ssid[0]) return ESP_ERR_INVALID_STATE;
    if (strlen(s_cfg.fallback_ap_password) < DEVICE_IDENTITY_MIN_PASSPHRASE_LENGTH) {
        ESP_LOGE(TAG, "Recovery AP refused: the stored passphrase is shorter than WPA2 allows. "
                      "The AP is never brought up unsecured.");
        return ESP_ERR_INVALID_STATE;
    }

    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, s_cfg.fallback_ap_ssid, sizeof(ap.ap.ssid));
    strlcpy((char *)ap.ap.password, s_cfg.fallback_ap_password, sizeof(ap.ap.password));
    ap.ap.ssid_len = strlen(s_cfg.fallback_ap_ssid);
    ap.ap.channel = RECOVERY_AP_HOME_CHANNEL;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.pmf_cfg.capable = true;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "APSTA mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap), TAG, "recovery AP config failed");

    s_recovery_ap_on_air = true;

    /* The captive portal follows the access point on air, so joining the setup
     * network opens the setup page. Bound to the AP's own address only -- see
     * captive_portal.h for why INADDR_ANY would take the site LAN down.
     *
     * A failure here is logged and not propagated: the recovery AP itself is the
     * guaranteed way back into a controller, and refusing to bring it up because
     * a convenience could not start would turn a smaller problem into the exact
     * lockout the AP exists to prevent. */
    esp_netif_ip_info_t ap_ip = {0};
    if (s_ap_netif && esp_netif_get_ip_info(s_ap_netif, &ap_ip) == ESP_OK &&
        ap_ip.ip.addr != 0U) {
        const esp_err_t portal = captive_portal_start(ap_ip.ip.addr);
        if (portal != ESP_OK) {
            ESP_LOGW(TAG, "Captive portal did not start (%s); the recovery AP is on air "
                          "and reachable by address", esp_err_to_name(portal));
        }
    }

    portENTER_CRITICAL(&s_lock);
    s_status.fallback_ap_active = true;
    s_status.ap_channel = RECOVERY_AP_HOME_CHANNEL;
    strlcpy(s_status.ap_ssid, s_cfg.fallback_ap_ssid, sizeof(s_status.ap_ssid));
    portEXIT_CRITICAL(&s_lock);

    /* The passphrase is deliberately absent from this line. */
    ESP_LOGI(TAG, "Recovery AP '%s' is on air (WPA2, channel %d) and stays on air",
             s_cfg.fallback_ap_ssid, RECOVERY_AP_HOME_CHANNEL);
    return ESP_OK;
}

/* ---------------------------------------------------------------------------
 * DELIBERATE, REASONED EXCEPTION TO THE NO-CREDENTIAL-LOGGING RULE.
 *
 * Do not "fix" this by removing the passphrase from the line below.
 *
 * Every other rule in this firmware holds: the recovery passphrase never
 * appears in an HTTP response, an API payload, the alarm journal, an exported
 * configuration, or any log that a network client can read. This one line is
 * the single exception, and it exists because of a real failure mode.
 *
 * The recovery AP is the guaranteed way into a controller that has been moved
 * to a site whose Wi-Fi it does not know. When its passphrase is generated
 * per device, there is no way to learn that passphrase over the network,
 * because the network you would use to ask is the one you cannot join. An AP
 * secret that is only retrievable over the AP it protects is not a secret, it
 * is a lockout.
 *
 * The serial console is not a network. Reading it requires the enclosure to be
 * open and a cable in the port - physical possession of the board. Anyone with
 * that already has the flash contents, the NVS partition and JTAG. Printing
 * here therefore concedes nothing to a remote attacker, and it is what headless
 * industrial equipment does in place of a printed label.
 *
 * Printed exactly once, at boot, from initialization only.
 * ------------------------------------------------------------------------- */
static void announce_recovery_ap_on_serial(bool build_default_in_use)
{
    if (!s_recovery_ap_on_air) {
        ESP_LOGE(TAG, "RECOVERY ACCESS POINT IS NOT ON AIR. It was refused rather than "
                      "brought up unsecured. Reach this unit on its station address.");
        return;
    }

    ESP_LOGW(TAG, "================ RECOVERY ACCESS POINT ================");
    ESP_LOGW(TAG, "  SSID       : %s", s_cfg.fallback_ap_ssid);
    ESP_LOGW(TAG, "  Passphrase : %s", s_cfg.fallback_ap_password);
    ESP_LOGW(TAG, "  Address    : http://192.168.4.1  or  http://%s.local",
             network_mdns_hostname());
    ESP_LOGW(TAG, "  Serial console only. Never served over HTTP or any API.");
    ESP_LOGW(TAG, "=======================================================");

    if (build_default_in_use) {
        ESP_LOGE(TAG, "***********************************************************");
        ESP_LOGE(TAG, "*** THE RECOVERY AP IS USING THIS BUILD'S DEFAULT       ***");
        ESP_LOGE(TAG, "*** PASSPHRASE. It is identical on every unit built     ***");
        ESP_LOGE(TAG, "*** from this public source, so it is public knowledge. ***");
        ESP_LOGE(TAG, "*** CHANGE IT VIA THE WI-FI PAGE BEFORE DEPLOYING THIS  ***");
        ESP_LOGE(TAG, "*** CONTROLLER TO A SITE.                               ***");
        ESP_LOGE(TAG, "***********************************************************");
    }
}

/* One mutation site for the sweep counter, so the backoff below has a single
 * source of truth. */
static void note_failed_sweep(void)
{
    if (s_failed_sweeps < UINT32_MAX) s_failed_sweeps++;
}

/* One radio serves both interfaces, so the soft AP cannot choose its own
 * channel: the driver drags it onto whatever channel the station associates on,
 * and drags it back when the station drops. Clients already joined to the AP
 * are on the old channel and will not follow - to them the AP simply vanishes
 * and they must reconnect.
 *
 * That is inherent to APSTA on a single-radio part and cannot be prevented,
 * only reported. This records the channel actually in use and says so once per
 * change, so an engineer whose session dropped mid-configuration sees why, and
 * so /api/status can show which channel to look on. */
static void note_ap_channel(void)
{
    if (!s_recovery_ap_on_air) return;

    uint8_t primary = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    if (esp_wifi_get_channel(&primary, &second) != ESP_OK || primary == 0U) return;

    portENTER_CRITICAL(&s_lock);
    const uint8_t previous = s_status.ap_channel;
    s_status.ap_channel = primary;
    portEXIT_CRITICAL(&s_lock);

    if (previous != primary) {
        ESP_LOGW(TAG, "Recovery AP moved to channel %u (was %u) because the radio follows the "
                      "station; clients joined to the AP must reconnect",
                 (unsigned)primary, (unsigned)previous);
    }
}

static void choose_and_connect(void)
{
    set_state(NETWORK_WIFI_SCANNING);

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

    /* Ordered walk of the saved stations. Every enabled profile is offered the
     * radio before the unit settles for the recovery AP, so a controller
     * carried back to a site it already knows rejoins that site on its own.
     *
     * Pass 0 takes the profiles the scan actually saw, strongest configuration
     * first by declaration order. Pass 1 takes the enabled profiles the scan
     * did not see, which is the only way to reach a hidden SSID; it is gated by
     * allow_blind so a permanently absent network does not keep stealing the
     * radio from the AP for ever. */
    const app_wifi_sta_profile_t *const order[] = {&s_cfg.primary, &s_cfg.fallback};
    const bool seen[] = {primary_found, fallback_found};
    const size_t profile_count = sizeof(order) / sizeof(order[0]);

    const bool allow_blind = s_force_blind_attempt || s_failed_sweeps < BLIND_ATTEMPT_SWEEPS;
    s_force_blind_attempt = false;

    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1 && !allow_blind) break;
        for (size_t n = 0; n < profile_count; ++n) {
            if (!order[n]->enabled || !order[n]->ssid[0]) continue;
            if (seen[n] != (pass == 0)) continue;
            if (pass == 1) {
                ESP_LOGW(TAG, "Saved SSID '%s' not visible in scan; attempting it anyway",
                         order[n]->ssid);
            }
            clear_operator_reconnect_if_disconnecting();
            if (connect_profile(order[n], n != 0) == ESP_OK) return;
        }
    }

    clear_operator_reconnect_if_disconnecting();
    note_failed_sweep();
    /* No station was worth attempting. The recovery AP has been serving since
     * boot, so this is a reporting state, not a transition. */
    set_state(NETWORK_WIFI_AP_FALLBACK);
}

/* Retry and backoff policy, stated plainly because a stranded controller at a
 * remote site is a truck roll:
 *
 *  1. The recovery AP is up from esp_wifi_start() and never comes down. The
 *     time to reach a controller that has lost its network is therefore zero -
 *     it is not gated on any retry budget expiring.
 *  2. A disconnected station retries the SAME profile up to
 *     max_retries_per_profile times (default 5) with immediate
 *     re-association.
 *  3. When that budget is spent, the next enabled saved profile is tried.
 *  4. When every saved profile is spent, one failed sweep is counted and the
 *     next full sweep is scheduled after
 *         15 s << min(failed_sweeps, 4), capped at 240 s
 *     so a visible but unusable SSID is not retried continuously and the AP
 *     keeps the radio for the overwhelming majority of the time.
 *  5. A successful association resets the sweep count to zero.
 *  6. An operator rescan, a boot, or a Wi-Fi event queue overflow resets the
 *     backoff and re-enables one blind (scan-negative) pass immediately. */
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
    s_force_blind_attempt = true;

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

    ESP_LOGW(TAG, "All saved STA profiles exhausted (last reason=%u); "
                  "the recovery AP is already serving and the sweep will repeat",
             (unsigned)reason);
    note_failed_sweep();
    note_ap_channel();
    set_state(NETWORK_WIFI_AP_FALLBACK);
    schedule_connect(connect_pending, connect_deadline,
                     ap_retry_delay_ms(), true);
}

static void handle_sta_got_ip(const esp_netif_ip_info_t *ip_info)
{
    if (!ip_info) return;

    wifi_ap_record_t ap = {0};
    esp_err_t ap_info_error = esp_wifi_sta_get_ap_info(&ap);

    /* The recovery AP is NOT stopped here. Joining a site network is exactly
     * when the previous code took the AP down, which is why a controller that
     * later lost that network had no way back in until a retry budget expired.
     * It stays up; only its channel moves. */
    note_ap_channel();

    portENTER_CRITICAL(&s_lock);
    s_status.state = NETWORK_WIFI_CONNECTED;
    s_status.network_ready = true;
    s_status.using_fallback_sta = s_using_fallback;
    s_status.fallback_ap_active = s_recovery_ap_on_air;
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
    s_force_blind_attempt = true;
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
    const bool build_default_secret = config_manager_recovery_ap_is_build_default(cfg);
    free(cfg);

    memset(&s_status, 0, sizeof(s_status));
    strlcpy(s_status.ip, "0.0.0.0", sizeof(s_status.ip));
    s_using_fallback = false;
    s_retry_count = 0;
    s_failed_sweeps = 0;
    /* Boot gets one blind pass so a hidden primary SSID is reached immediately
     * rather than after the first scan comes back negative. */
    s_force_blind_attempt = true;
    s_event_queue_overflow = false;
    s_recovery_ap_on_air = false;
    clear_operator_reconnect();

    ESP_LOGI(TAG, "Saved stations: primary '%s'%s, secondary '%s'%s; recovery AP '%s' (always on)",
             s_cfg.primary.ssid, s_cfg.primary.enabled ? "" : " (disabled)",
             s_cfg.fallback.ssid, s_cfg.fallback.enabled ? "" : " (disabled)",
             s_cfg.fallback_ap_ssid);

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

    /* The device name is free text and contains spaces, so it was never a legal
     * DHCP host name or DNS label. Both interfaces now carry the MAC-derived
     * label instead, which is what makes "<host>.local" work and what a DHCP
     * lease table will show. */
    char hostname[DEVICE_IDENTITY_HOSTNAME_SIZE] = {0};
    if (device_identity_hostname(hostname, sizeof(hostname)) == ESP_OK) {
        esp_netif_set_hostname(s_sta_netif, hostname);
        esp_netif_set_hostname(s_ap_netif, hostname);
        portENTER_CRITICAL(&s_lock);
        strlcpy(s_status.hostname, hostname, sizeof(s_status.hostname));
        portEXIT_CRITICAL(&s_lock);
    } else {
        ESP_LOGW(TAG, "Factory MAC unavailable; no unique host name is published");
    }

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "Wi-Fi init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "Wi-Fi storage mode failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL), TAG, "Wi-Fi handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL), TAG, "IP handler failed");

    /* APSTA before the radio starts, and the recovery AP configured before the
     * first station attempt: the unit is reachable from the moment the radio
     * comes up, not from the moment a retry budget runs out.
     *
     * If the AP cannot be configured the radio is put back to station-only
     * rather than started in APSTA with a half-applied AP configuration. That is
     * the fail-closed direction for requirement "WPA2 always": an AP that could
     * not be given its passphrase must not go on air at all. It is deliberately
     * NOT fatal to init, because dropping the station as well would remove the
     * one remaining way to reach the controller. */
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "APSTA mode failed");
    if (start_recovery_ap() != ESP_OK) {
        ESP_LOGE(TAG, "Recovery AP could not be configured; continuing station-only. "
                      "The AP is NOT brought up unsecured.");
        portENTER_CRITICAL(&s_lock);
        s_status.fallback_ap_active = false;
        s_status.ap_channel = 0;
        s_status.ap_ssid[0] = '\0';
        portEXIT_CRITICAL(&s_lock);
    }

    /* The single expression of the radio-mode rule, shared with connect_profile:
     * concurrent whenever the recovery AP is on air, station-only only when it is
     * not. Written this way so no reader - and no later edit - can select a
     * station-only mode without saying, in the same statement, that the AP is
     * already down. */
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(s_recovery_ap_on_air ? WIFI_MODE_APSTA : WIFI_MODE_STA),
                        TAG, "radio mode failed");

    if (xTaskCreate(manager_task, "wifi_manager", 6144, NULL, 12, &s_task) != pdPASS) return ESP_ERR_NO_MEM;
    ESP_RETURN_ON_ERROR(network_scan_service_init(s_task, MANAGER_WAKE_USER_SCAN),
                        TAG, "scan service init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Wi-Fi start failed");

    /* After the radio is up so the responder attaches to live interfaces. A
     * failure is not fatal: the unit is still reachable by address and over the
     * recovery AP, it is simply harder to find. */
    if (hostname[0]) {
        (void)network_mdns_start(hostname, CONFIG_PVDG_DEVICE_NAME);
    }

    announce_recovery_ap_on_serial(build_default_secret);
    return ESP_OK;
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
