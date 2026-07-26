#include "config_manager.h"
#include "esp_check.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#define NS "pvdg"
#define KEY "config"
#define MASKED_PASSWORD "********"

static const char *TAG = "config";
static app_config_t s_cfg;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    char ssid[33];
    char password[65];
} legacy_wifi_config_v1_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    char device_name[32];
    legacy_wifi_config_v1_t wifi;
    uint8_t meter_count;
    meter_config_t meters[APP_MAX_METERS];
    uint8_t inverter_count;
    inverter_config_t inverters[APP_MAX_INVERTERS];
    control_config_t control;
} legacy_app_config_v1_t;

/* Schema 2 is the current layout without the trailing provisioning id, so it is
 * an exact prefix of app_config_t and migrates with a single copy. */
typedef struct {
    uint32_t magic;
    uint16_t version;
    char device_name[32];
    app_wifi_config_t wifi;
    uint8_t meter_count;
    meter_config_t meters[APP_MAX_METERS];
    uint8_t inverter_count;
    inverter_config_t inverters[APP_MAX_INVERTERS];
    control_config_t control;
} legacy_app_config_v2_t;

_Static_assert(sizeof(app_config_t) == sizeof(legacy_app_config_v2_t) + sizeof(uint32_t),
               "schema 2 must remain a byte-exact prefix of the current layout");

static void defaults(app_config_t *c)
{
    memset(c, 0, sizeof(*c));
    c->magic = APP_CONFIG_MAGIC;
    c->version = APP_CONFIG_VERSION;
    strlcpy(c->device_name, CONFIG_PVDG_DEVICE_NAME, sizeof(c->device_name));

    c->wifi.primary.enabled = true;
    strlcpy(c->wifi.primary.ssid, CONFIG_PVDG_PRIMARY_WIFI_SSID, sizeof(c->wifi.primary.ssid));
    strlcpy(c->wifi.primary.password, CONFIG_PVDG_PRIMARY_WIFI_PASSWORD, sizeof(c->wifi.primary.password));
    c->wifi.primary.ip_mode = APP_WIFI_IP_DHCP;

    const char *default_ssid = CONFIG_PVDG_DEFAULT_WIFI_SSID;
    c->wifi.fallback.enabled = default_ssid[0] != '\0' && strcmp(default_ssid, c->wifi.primary.ssid) != 0;
    strlcpy(c->wifi.fallback.ssid, default_ssid, sizeof(c->wifi.fallback.ssid));
    strlcpy(c->wifi.fallback.password, CONFIG_PVDG_DEFAULT_WIFI_PASSWORD, sizeof(c->wifi.fallback.password));
    c->wifi.fallback.ip_mode = APP_WIFI_IP_DHCP;

    c->wifi.scan_before_connect = true;
    c->wifi.fallback_ap_enabled = true;
    strlcpy(c->wifi.fallback_ap_ssid, "Automatrix-PVDG-Setup", sizeof(c->wifi.fallback_ap_ssid));
    strlcpy(c->wifi.fallback_ap_password, "automatrix123", sizeof(c->wifi.fallback_ap_password));
    c->wifi.max_retries_per_profile = 5;
    c->wifi.reconnect_backoff_ms = 2000;

    c->meter_count = 1;
    meter_config_t *m = &c->meters[0];
    m->enabled = true;
    strlcpy(m->name, "Grid Meter", sizeof(m->name));
    strlcpy(m->endpoint.host, CONFIG_PVDG_DEFAULT_ZLAN_HOST, sizeof(m->endpoint.host));
    m->endpoint.port = CONFIG_PVDG_DEFAULT_ZLAN_PORT;
    m->endpoint.unit_id = 1;
    m->endpoint.timeout_ms = 1500;
    m->function_code = 3;
    m->active_power_address = 57;
    m->active_power_type = MODBUS_DATA_INT32;
    m->active_power_order = MODBUS_ORDER_ABCD;
    m->active_power_scale = 0.00001f;
    m->poll_interval_ms = 1000;

    c->inverter_count = 1;
    inverter_config_t *i = &c->inverters[0];
    strlcpy(i->name, "Inverter 1", sizeof(i->name));
    strlcpy(i->endpoint.host, "192.168.1.210", sizeof(i->endpoint.host));
    i->endpoint.port = 502;
    i->endpoint.unit_id = 1;
    i->endpoint.timeout_ms = 500;
    i->rated_power_kw = 100.0f;
    i->power_limit_function = 6;
    i->raw_units_per_percent = 100.0f;
    i->maximum_percent = 100.0f;

    c->control.enabled = false;
    c->control.grid_import_target_kw = 5.0f;
    c->control.deadband_kw = 2.0f;
    c->control.kp = 0.30f;
    c->control.ki = 0.05f;
    c->control.ramp_up_percent_per_second = 5.0f;
    c->control.ramp_down_percent_per_second = 20.0f;
    c->control.interval_ms = 250;
    c->control.meter_stale_timeout_ms = 3000;

    c->wifi_provision_id = CONFIG_PVDG_WIFI_PROVISION_ID;
}

/* Applies the credentials compiled into this build, once per provisioning id.
 * Returns true when the configuration was changed and must be persisted.
 *
 * A disabled Kconfig bool is not defined at all, so the opt-in switch has to be
 * tested with #ifdef rather than a numeric comparison. */
#ifndef CONFIG_PVDG_APPLY_BUILD_WIFI_PROVISIONING

/* Build provisioning is opt-in and off by default. Changing nothing here is
 * what stops an ordinary build - including one from a fresh worktree whose
 * sdkconfig was regenerated from Kconfig defaults - from replacing the
 * credentials of a commissioned device and persisting them to NVS. */
static bool apply_build_provisioning(app_config_t *c)
{
    (void)c;
    return false;
}

#else

static bool apply_build_provisioning(app_config_t *c)
{
    /* Strictly monotonic: only a genuinely newer generation may apply. An equal
     * or lower id - including the default 0 of a fresh worktree - must never
     * reapply credentials nor roll a device back to an older set. */
    if (CONFIG_PVDG_WIFI_PROVISION_ID <= 0) return false;
    if ((uint32_t)CONFIG_PVDG_WIFI_PROVISION_ID <= c->wifi_provision_id) return false;

    const char *ssid = CONFIG_PVDG_PRIMARY_WIFI_SSID;
    if (!ssid[0]) {
        /* Nothing to provision. Leaving the stored id untouched keeps this a
         * no-op, so a build with a blank SSID can neither erase the stored
         * profile nor consume the provisioning generation. */
        ESP_LOGW(TAG, "Build provisioning %d ignored: no primary SSID compiled in",
                 CONFIG_PVDG_WIFI_PROVISION_ID);
        return false;
    }

    strlcpy(c->wifi.primary.ssid, ssid, sizeof(c->wifi.primary.ssid));
    strlcpy(c->wifi.primary.password, CONFIG_PVDG_PRIMARY_WIFI_PASSWORD, sizeof(c->wifi.primary.password));
    c->wifi.primary.enabled = true;
    c->wifi.primary.ip_mode = APP_WIFI_IP_DHCP;

    const char *fallback_ssid = CONFIG_PVDG_DEFAULT_WIFI_SSID;
    strlcpy(c->wifi.fallback.ssid, fallback_ssid, sizeof(c->wifi.fallback.ssid));
    strlcpy(c->wifi.fallback.password, CONFIG_PVDG_DEFAULT_WIFI_PASSWORD, sizeof(c->wifi.fallback.password));
    c->wifi.fallback.enabled = fallback_ssid[0] != '\0' && strcmp(fallback_ssid, ssid) != 0;

    c->wifi_provision_id = CONFIG_PVDG_WIFI_PROVISION_ID;
    ESP_LOGW(TAG, "Applied build provisioning %d: primary SSID '%s', fallback '%s'%s",
             CONFIG_PVDG_WIFI_PROVISION_ID, c->wifi.primary.ssid,
             c->wifi.fallback.ssid, c->wifi.fallback.enabled ? "" : " (disabled)");
    return true;
}

#endif /* CONFIG_PVDG_APPLY_BUILD_WIFI_PROVISIONING */

static bool profile_valid(const app_wifi_sta_profile_t *p)
{
    if (!p->enabled) return true;
    if (!p->ssid[0] || p->ip_mode > APP_WIFI_IP_STATIC) return false;
    if (p->ip_mode == APP_WIFI_IP_STATIC &&
        (!p->static_ip[0] || !p->gateway[0] || !p->netmask[0])) return false;
    return true;
}

static bool valid(const app_config_t *c)
{
    return c && c->magic == APP_CONFIG_MAGIC && c->version == APP_CONFIG_VERSION &&
           profile_valid(&c->wifi.primary) && profile_valid(&c->wifi.fallback) &&
           c->wifi.primary.enabled && c->wifi.max_retries_per_profile > 0 &&
           c->meter_count <= APP_MAX_METERS && c->inverter_count <= APP_MAX_INVERTERS &&
           c->control.interval_ms >= 50;
}

static void migrate_v1(const legacy_app_config_v1_t *old, app_config_t *next)
{
    defaults(next);
    if (!old || old->magic != APP_CONFIG_MAGIC || old->version != 1) return;

    strlcpy(next->device_name, old->device_name, sizeof(next->device_name));
    if (old->wifi.ssid[0]) {
        strlcpy(next->wifi.primary.ssid, old->wifi.ssid, sizeof(next->wifi.primary.ssid));
        strlcpy(next->wifi.primary.password, old->wifi.password, sizeof(next->wifi.primary.password));
    }

    next->meter_count = old->meter_count <= APP_MAX_METERS ? old->meter_count : APP_MAX_METERS;
    memcpy(next->meters, old->meters, sizeof(next->meters));
    next->inverter_count = old->inverter_count <= APP_MAX_INVERTERS ? old->inverter_count : APP_MAX_INVERTERS;
    memcpy(next->inverters, old->inverters, sizeof(next->inverters));
    next->control = old->control;
    next->control.enabled = false;

    if (strcmp(next->wifi.fallback.ssid, next->wifi.primary.ssid) == 0) {
        next->wifi.fallback.enabled = false;
    }
}

static void set_active(const app_config_t *c)
{
    portENTER_CRITICAL(&s_lock);
    s_cfg = *c;
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t config_manager_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition unusable (%s); erasing only the NVS partition", esp_err_to_name(err));
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "NVS erase failed");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "NVS init failed");

    app_config_t *loaded = calloc(1, sizeof(*loaded));
    if (!loaded) return ESP_ERR_NO_MEM;

    bool have_valid_config = false;
    bool stored_matches = false;
    nvs_handle_t h;
    err = nvs_open(NS, NVS_READONLY, &h);
    if (err == ESP_OK) {
        size_t stored_size = 0;
        err = nvs_get_blob(h, KEY, NULL, &stored_size);
        if (err == ESP_OK && stored_size == sizeof(*loaded)) {
            size_t size = sizeof(*loaded);
            err = nvs_get_blob(h, KEY, loaded, &size);
            have_valid_config = err == ESP_OK && valid(loaded);
            stored_matches = have_valid_config;
            if (!have_valid_config) ESP_LOGW(TAG, "Stored configuration rejected by validation");
        } else if (err == ESP_OK && stored_size == sizeof(legacy_app_config_v2_t)) {
            legacy_app_config_v2_t *legacy = malloc(sizeof(*legacy));
            if (legacy) {
                size_t size = sizeof(*legacy);
                if (nvs_get_blob(h, KEY, legacy, &size) == ESP_OK &&
                    legacy->magic == APP_CONFIG_MAGIC && legacy->version == 2) {
                    memcpy(loaded, legacy, sizeof(*legacy));
                    loaded->version = APP_CONFIG_VERSION;
                    loaded->wifi_provision_id = 0;
                    loaded->control.enabled = false;
                    have_valid_config = valid(loaded);
                    if (have_valid_config) ESP_LOGI(TAG, "Migrated configuration schema 2 to schema %u", APP_CONFIG_VERSION);
                    else ESP_LOGW(TAG, "Schema 2 migration produced an invalid configuration; discarding it");
                }
                free(legacy);
            }
        } else if (err == ESP_OK && stored_size == sizeof(legacy_app_config_v1_t)) {
            legacy_app_config_v1_t *legacy = malloc(sizeof(*legacy));
            if (legacy) {
                size_t size = sizeof(*legacy);
                if (nvs_get_blob(h, KEY, legacy, &size) == ESP_OK) {
                    migrate_v1(legacy, loaded);
                    have_valid_config = valid(loaded);
                    if (have_valid_config) ESP_LOGI(TAG, "Migrated configuration schema 1 to schema %u", APP_CONFIG_VERSION);
                    else ESP_LOGW(TAG, "Schema 1 migration produced an invalid configuration; discarding it");
                }
                free(legacy);
            }
        } else if (err == ESP_OK) {
            ESP_LOGW(TAG, "Stored configuration blob is %u bytes but no known schema matches; discarding it",
                     (unsigned)stored_size);
        }
        nvs_close(h);
    }

    if (!have_valid_config) {
        defaults(loaded);
        ESP_LOGW(TAG, "No valid stored configuration; safe defaults loaded (primary SSID '%s')",
                 loaded->wifi.primary.ssid);
    } else if (apply_build_provisioning(loaded)) {
        stored_matches = false;
    }

    set_active(loaded);
    if (!stored_matches) {
        err = config_manager_save(loaded);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Configuration persistence failed (%s); continuing with in-memory configuration",
                     esp_err_to_name(err));
        }
    }
    free(loaded);
    return ESP_OK;
}

esp_err_t config_manager_get_snapshot(app_config_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_lock);
    *out = s_cfg;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

esp_err_t config_manager_save(const app_config_t *c)
{
    if (!valid(c)) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NS, NVS_READWRITE, &h), TAG, "NVS open failed");
    esp_err_t err = nvs_set_blob(h, KEY, c, sizeof(*c));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) return err;

    app_config_t *verify = malloc(sizeof(*verify));
    if (!verify) return ESP_ERR_NO_MEM;
    size_t verify_size = sizeof(*verify);
    err = nvs_open(NS, NVS_READONLY, &h);
    if (err == ESP_OK) {
        err = nvs_get_blob(h, KEY, verify, &verify_size);
        nvs_close(h);
    }
    bool verified = err == ESP_OK && verify_size == sizeof(*verify) && memcmp(verify, c, sizeof(*c)) == 0;
    free(verify);
    if (!verified) return err == ESP_OK ? ESP_ERR_INVALID_CRC : err;

    portENTER_CRITICAL(&s_lock);
    s_cfg = *c;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

static void endpoint_to_json(cJSON *o, const modbus_endpoint_t *e)
{
    cJSON_AddStringToObject(o, "host", e->host);
    cJSON_AddNumberToObject(o, "port", e->port);
    cJSON_AddNumberToObject(o, "unit_id", e->unit_id);
    cJSON_AddNumberToObject(o, "timeout_ms", e->timeout_ms);
}

static void wifi_profile_to_json(cJSON *parent, const char *name, const app_wifi_sta_profile_t *p)
{
    cJSON *o = cJSON_AddObjectToObject(parent, name);
    cJSON_AddBoolToObject(o, "enabled", p->enabled);
    cJSON_AddStringToObject(o, "ssid", p->ssid);
    cJSON_AddStringToObject(o, "password", p->password[0] ? MASKED_PASSWORD : "");
    cJSON_AddNumberToObject(o, "ip_mode", p->ip_mode);
    cJSON_AddStringToObject(o, "static_ip", p->static_ip);
    cJSON_AddStringToObject(o, "gateway", p->gateway);
    cJSON_AddStringToObject(o, "netmask", p->netmask);
    cJSON_AddStringToObject(o, "dns1", p->dns1);
    cJSON_AddStringToObject(o, "dns2", p->dns2);
}

esp_err_t config_manager_export_json(char **out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL;

    app_config_t *c = malloc(sizeof(*c));
    if (!c) return ESP_ERR_NO_MEM;
    esp_err_t err = config_manager_get_snapshot(c);
    if (err != ESP_OK) {
        free(c);
        return err;
    }

    cJSON *r = cJSON_CreateObject();
    if (!r) {
        free(c);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddNumberToObject(r, "schema", c->version);
    cJSON_AddStringToObject(r, "device_name", c->device_name);
    cJSON *w = cJSON_AddObjectToObject(r, "wifi");
    wifi_profile_to_json(w, "primary", &c->wifi.primary);
    wifi_profile_to_json(w, "fallback", &c->wifi.fallback);
    cJSON_AddBoolToObject(w, "scan_before_connect", c->wifi.scan_before_connect);
    cJSON_AddBoolToObject(w, "fallback_ap_enabled", c->wifi.fallback_ap_enabled);
    cJSON_AddStringToObject(w, "fallback_ap_ssid", c->wifi.fallback_ap_ssid);
    cJSON_AddStringToObject(w, "fallback_ap_password", c->wifi.fallback_ap_password[0] ? MASKED_PASSWORD : "");
    cJSON_AddNumberToObject(w, "max_retries_per_profile", c->wifi.max_retries_per_profile);
    cJSON_AddNumberToObject(w, "reconnect_backoff_ms", c->wifi.reconnect_backoff_ms);

    cJSON *ma = cJSON_AddArrayToObject(r, "meters");
    for (uint8_t n = 0; n < c->meter_count; ++n) {
        meter_config_t *m = &c->meters[n];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "enabled", m->enabled);
        cJSON_AddStringToObject(o, "name", m->name);
        endpoint_to_json(o, &m->endpoint);
        cJSON_AddNumberToObject(o, "function", m->function_code);
        cJSON_AddNumberToObject(o, "active_power_address", m->active_power_address);
        cJSON_AddNumberToObject(o, "data_type", m->active_power_type);
        cJSON_AddNumberToObject(o, "word_order", m->active_power_order);
        cJSON_AddNumberToObject(o, "scale", m->active_power_scale);
        cJSON_AddNumberToObject(o, "poll_ms", m->poll_interval_ms);
        cJSON_AddItemToArray(ma, o);
    }

    cJSON *ia = cJSON_AddArrayToObject(r, "inverters");
    for (uint8_t n = 0; n < c->inverter_count; ++n) {
        inverter_config_t *i = &c->inverters[n];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "enabled", i->enabled);
        cJSON_AddStringToObject(o, "name", i->name);
        endpoint_to_json(o, &i->endpoint);
        cJSON_AddNumberToObject(o, "rated_kw", i->rated_power_kw);
        cJSON_AddNumberToObject(o, "limit_address", i->power_limit_address);
        cJSON_AddNumberToObject(o, "limit_function", i->power_limit_function);
        cJSON_AddNumberToObject(o, "raw_units_per_percent", i->raw_units_per_percent);
        cJSON_AddNumberToObject(o, "min_percent", i->minimum_percent);
        cJSON_AddNumberToObject(o, "max_percent", i->maximum_percent);
        cJSON_AddItemToArray(ia, o);
    }

    cJSON *cc = cJSON_AddObjectToObject(r, "control");
    cJSON_AddBoolToObject(cc, "enabled", c->control.enabled);
    cJSON_AddNumberToObject(cc, "grid_import_target_kw", c->control.grid_import_target_kw);
    cJSON_AddNumberToObject(cc, "deadband_kw", c->control.deadband_kw);
    cJSON_AddNumberToObject(cc, "kp", c->control.kp);
    cJSON_AddNumberToObject(cc, "ki", c->control.ki);
    cJSON_AddNumberToObject(cc, "ramp_up_pct_s", c->control.ramp_up_percent_per_second);
    cJSON_AddNumberToObject(cc, "ramp_down_pct_s", c->control.ramp_down_percent_per_second);
    cJSON_AddNumberToObject(cc, "interval_ms", c->control.interval_ms);
    cJSON_AddNumberToObject(cc, "meter_stale_timeout_ms", c->control.meter_stale_timeout_ms);

    *out = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    free(c);
    return *out ? ESP_OK : ESP_ERR_NO_MEM;
}

static void read_string(cJSON *o, const char *key, char *dst, size_t size)
{
    cJSON *x = cJSON_GetObjectItemCaseSensitive(o, key);
    if (cJSON_IsString(x)) strlcpy(dst, x->valuestring, size);
}

static void read_password(cJSON *o, const char *key, char *dst, size_t size)
{
    cJSON *x = cJSON_GetObjectItemCaseSensitive(o, key);
    if (cJSON_IsString(x) && x->valuestring[0] && strcmp(x->valuestring, MASKED_PASSWORD) != 0) {
        strlcpy(dst, x->valuestring, size);
    }
}

static void read_float(cJSON *o, const char *key, float *value)
{
    cJSON *x = cJSON_GetObjectItemCaseSensitive(o, key);
    if (cJSON_IsNumber(x)) *value = (float)x->valuedouble;
}

static void parse_wifi_profile(cJSON *o, app_wifi_sta_profile_t *p)
{
    if (!cJSON_IsObject(o)) return;
    cJSON *x = cJSON_GetObjectItemCaseSensitive(o, "enabled");
    if (cJSON_IsBool(x)) p->enabled = cJSON_IsTrue(x);
    read_string(o, "ssid", p->ssid, sizeof(p->ssid));
    read_password(o, "password", p->password, sizeof(p->password));
    x = cJSON_GetObjectItemCaseSensitive(o, "ip_mode");
    if (cJSON_IsNumber(x)) p->ip_mode = (app_wifi_ip_mode_t)x->valueint;
    read_string(o, "static_ip", p->static_ip, sizeof(p->static_ip));
    read_string(o, "gateway", p->gateway, sizeof(p->gateway));
    read_string(o, "netmask", p->netmask, sizeof(p->netmask));
    read_string(o, "dns1", p->dns1, sizeof(p->dns1));
    read_string(o, "dns2", p->dns2, sizeof(p->dns2));
}

esp_err_t config_manager_import_json(const char *text)
{
    if (!text) return ESP_ERR_INVALID_ARG;
    cJSON *r = cJSON_Parse(text);
    if (!r) return ESP_ERR_INVALID_ARG;

    app_config_t *c = malloc(sizeof(*c));
    if (!c) {
        cJSON_Delete(r);
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = config_manager_get_snapshot(c);
    if (err != ESP_OK) {
        free(c);
        cJSON_Delete(r);
        return err;
    }

    cJSON *w = cJSON_GetObjectItemCaseSensitive(r, "wifi");
    if (cJSON_IsObject(w)) {
        parse_wifi_profile(cJSON_GetObjectItemCaseSensitive(w, "primary"), &c->wifi.primary);
        parse_wifi_profile(cJSON_GetObjectItemCaseSensitive(w, "fallback"), &c->wifi.fallback);
        cJSON *x = cJSON_GetObjectItemCaseSensitive(w, "scan_before_connect");
        if (cJSON_IsBool(x)) c->wifi.scan_before_connect = cJSON_IsTrue(x);
        x = cJSON_GetObjectItemCaseSensitive(w, "fallback_ap_enabled");
        if (cJSON_IsBool(x)) c->wifi.fallback_ap_enabled = cJSON_IsTrue(x);
        read_string(w, "fallback_ap_ssid", c->wifi.fallback_ap_ssid, sizeof(c->wifi.fallback_ap_ssid));
        read_password(w, "fallback_ap_password", c->wifi.fallback_ap_password, sizeof(c->wifi.fallback_ap_password));
        x = cJSON_GetObjectItemCaseSensitive(w, "max_retries_per_profile");
        if (cJSON_IsNumber(x)) c->wifi.max_retries_per_profile = (uint8_t)x->valueint;
        x = cJSON_GetObjectItemCaseSensitive(w, "reconnect_backoff_ms");
        if (cJSON_IsNumber(x)) c->wifi.reconnect_backoff_ms = (uint32_t)x->valuedouble;
    }

    cJSON *meters = cJSON_GetObjectItemCaseSensitive(r, "meters");
    if (cJSON_IsArray(meters) && cJSON_GetArraySize(meters) > 0) {
        cJSON *o = cJSON_GetArrayItem(meters, 0);
        meter_config_t *m = &c->meters[0];
        read_string(o, "host", m->endpoint.host, sizeof(m->endpoint.host));
        cJSON *x = cJSON_GetObjectItemCaseSensitive(o, "port");
        if (cJSON_IsNumber(x)) m->endpoint.port = (uint16_t)x->valueint;
        x = cJSON_GetObjectItemCaseSensitive(o, "unit_id");
        if (cJSON_IsNumber(x)) m->endpoint.unit_id = (uint8_t)x->valueint;
        x = cJSON_GetObjectItemCaseSensitive(o, "active_power_address");
        if (cJSON_IsNumber(x)) m->active_power_address = (uint16_t)x->valueint;
        read_float(o, "scale", &m->active_power_scale);
    }

    cJSON *cc = cJSON_GetObjectItemCaseSensitive(r, "control");
    if (cJSON_IsObject(cc)) {
        cJSON *x = cJSON_GetObjectItemCaseSensitive(cc, "enabled");
        if (cJSON_IsBool(x)) c->control.enabled = cJSON_IsTrue(x);
        read_float(cc, "grid_import_target_kw", &c->control.grid_import_target_kw);
        read_float(cc, "deadband_kw", &c->control.deadband_kw);
        read_float(cc, "kp", &c->control.kp);
        read_float(cc, "ki", &c->control.ki);
        x = cJSON_GetObjectItemCaseSensitive(cc, "interval_ms");
        if (cJSON_IsNumber(x)) c->control.interval_ms = (uint32_t)x->valuedouble;
    }

    cJSON_Delete(r);
    err = config_manager_save(c);
    free(c);
    return err;
}

esp_err_t config_manager_restore_defaults(void)
{
    app_config_t *c = malloc(sizeof(*c));
    if (!c) return ESP_ERR_NO_MEM;
    defaults(c);
    esp_err_t err = config_manager_save(c);
    free(c);
    return err;
}
