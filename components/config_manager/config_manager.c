#include "config_manager.h"
#include "esp_check.h"
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#define NS "pvdg"
#define KEY "config"
static app_config_t s_cfg;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static void defaults(app_config_t *c)
{
    memset(c, 0, sizeof(*c));
    c->magic = APP_CONFIG_MAGIC;
    c->version = APP_CONFIG_VERSION;
    strlcpy(c->device_name, CONFIG_PVDG_DEVICE_NAME, sizeof(c->device_name));
    strlcpy(c->wifi.ssid, CONFIG_PVDG_DEFAULT_WIFI_SSID, sizeof(c->wifi.ssid));
    strlcpy(c->wifi.password, CONFIG_PVDG_DEFAULT_WIFI_PASSWORD, sizeof(c->wifi.password));

    c->meter_count = 1;
    meter_config_t *m = &c->meters[0];
    m->enabled = true;
    strlcpy(m->name, "Grid Meter", sizeof(m->name));
    strlcpy(m->endpoint.host, CONFIG_PVDG_DEFAULT_ZLAN_HOST, sizeof(m->endpoint.host));
    m->endpoint.port = CONFIG_PVDG_DEFAULT_ZLAN_PORT;
    m->endpoint.unit_id = 1;
    m->endpoint.timeout_ms = 500;
    m->function_code = 3;
    m->active_power_type = MODBUS_DATA_INT32;
    m->active_power_order = MODBUS_ORDER_ABCD;
    m->active_power_scale = 0.01f;
    m->poll_interval_ms = 250;

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

    c->control.grid_import_target_kw = 5.0f;
    c->control.deadband_kw = 2.0f;
    c->control.kp = 0.30f;
    c->control.ki = 0.05f;
    c->control.ramp_up_percent_per_second = 5.0f;
    c->control.ramp_down_percent_per_second = 20.0f;
    c->control.interval_ms = 250;
    c->control.meter_stale_timeout_ms = 1500;
}

static bool valid(const app_config_t *c)
{
    return c && c->magic == APP_CONFIG_MAGIC && c->version == APP_CONFIG_VERSION &&
           c->meter_count <= APP_MAX_METERS && c->inverter_count <= APP_MAX_INVERTERS &&
           c->control.interval_ms >= 50;
}

esp_err_t config_manager_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), "config", "NVS erase failed");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, "config", "NVS init failed");

    app_config_t loaded;
    size_t size = sizeof(loaded);
    nvs_handle_t h;
    err = nvs_open(NS, NVS_READONLY, &h);
    if (err == ESP_OK) {
        err = nvs_get_blob(h, KEY, &loaded, &size);
        nvs_close(h);
    }
    if (err != ESP_OK || size != sizeof(loaded) || !valid(&loaded)) defaults(&loaded);
    return config_manager_save(&loaded);
}

esp_err_t config_manager_get_snapshot(app_config_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_lock); *out = s_cfg; portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

esp_err_t config_manager_save(const app_config_t *c)
{
    if (!valid(c)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NS, NVS_READWRITE, &h), "config", "NVS open failed");
    esp_err_t err = nvs_set_blob(h, KEY, c, sizeof(*c));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) { portENTER_CRITICAL(&s_lock); s_cfg = *c; portEXIT_CRITICAL(&s_lock); }
    return err;
}

static void endpoint_to_json(cJSON *o, const modbus_endpoint_t *e)
{
    cJSON_AddStringToObject(o, "host", e->host);
    cJSON_AddNumberToObject(o, "port", e->port);
    cJSON_AddNumberToObject(o, "unit_id", e->unit_id);
    cJSON_AddNumberToObject(o, "timeout_ms", e->timeout_ms);
}

esp_err_t config_manager_export_json(char **out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    app_config_t c; config_manager_get_snapshot(&c);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "schema", c.version);
    cJSON_AddStringToObject(r, "device_name", c.device_name);
    cJSON *w = cJSON_AddObjectToObject(r, "wifi");
    cJSON_AddStringToObject(w, "ssid", c.wifi.ssid);
    cJSON_AddStringToObject(w, "password", c.wifi.password);

    cJSON *ma = cJSON_AddArrayToObject(r, "meters");
    for (uint8_t n = 0; n < c.meter_count; n++) {
        meter_config_t *m = &c.meters[n]; cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "enabled", m->enabled); cJSON_AddStringToObject(o, "name", m->name);
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
    for (uint8_t n = 0; n < c.inverter_count; n++) {
        inverter_config_t *i = &c.inverters[n]; cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "enabled", i->enabled); cJSON_AddStringToObject(o, "name", i->name);
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
    cJSON_AddBoolToObject(cc, "enabled", c.control.enabled);
    cJSON_AddNumberToObject(cc, "grid_import_target_kw", c.control.grid_import_target_kw);
    cJSON_AddNumberToObject(cc, "deadband_kw", c.control.deadband_kw);
    cJSON_AddNumberToObject(cc, "kp", c.control.kp); cJSON_AddNumberToObject(cc, "ki", c.control.ki);
    cJSON_AddNumberToObject(cc, "ramp_up_pct_s", c.control.ramp_up_percent_per_second);
    cJSON_AddNumberToObject(cc, "ramp_down_pct_s", c.control.ramp_down_percent_per_second);
    cJSON_AddNumberToObject(cc, "interval_ms", c.control.interval_ms);
    cJSON_AddNumberToObject(cc, "meter_stale_timeout_ms", c.control.meter_stale_timeout_ms);
    *out = cJSON_PrintUnformatted(r); cJSON_Delete(r);
    return *out ? ESP_OK : ESP_ERR_NO_MEM;
}

static void number(cJSON *o, const char *k, float *v) { cJSON *x=cJSON_GetObjectItemCaseSensitive(o,k); if(cJSON_IsNumber(x))*v=x->valuedouble; }

esp_err_t config_manager_import_json(const char *text)
{
    cJSON *r = cJSON_Parse(text); if (!r) return ESP_ERR_INVALID_ARG;
    app_config_t c; config_manager_get_snapshot(&c);
    cJSON *w=cJSON_GetObjectItemCaseSensitive(r,"wifi");
    if(cJSON_IsObject(w)){cJSON *x=cJSON_GetObjectItemCaseSensitive(w,"ssid");if(cJSON_IsString(x))strlcpy(c.wifi.ssid,x->valuestring,sizeof(c.wifi.ssid));x=cJSON_GetObjectItemCaseSensitive(w,"password");if(cJSON_IsString(x))strlcpy(c.wifi.password,x->valuestring,sizeof(c.wifi.password));}
    cJSON *m=cJSON_GetObjectItemCaseSensitive(r,"meters");
    if(cJSON_IsArray(m)&&cJSON_GetArraySize(m)>0){cJSON *o=cJSON_GetArrayItem(m,0),*x;meter_config_t *p=&c.meters[0];x=cJSON_GetObjectItemCaseSensitive(o,"host");if(cJSON_IsString(x))strlcpy(p->endpoint.host,x->valuestring,sizeof(p->endpoint.host));x=cJSON_GetObjectItemCaseSensitive(o,"port");if(cJSON_IsNumber(x))p->endpoint.port=x->valueint;x=cJSON_GetObjectItemCaseSensitive(o,"unit_id");if(cJSON_IsNumber(x))p->endpoint.unit_id=x->valueint;x=cJSON_GetObjectItemCaseSensitive(o,"active_power_address");if(cJSON_IsNumber(x))p->active_power_address=x->valueint;number(o,"scale",&p->active_power_scale);}
    cJSON *cc=cJSON_GetObjectItemCaseSensitive(r,"control");
    if(cJSON_IsObject(cc)){c.control.enabled=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(cc,"enabled"));number(cc,"grid_import_target_kw",&c.control.grid_import_target_kw);number(cc,"deadband_kw",&c.control.deadband_kw);number(cc,"kp",&c.control.kp);number(cc,"ki",&c.control.ki);cJSON *x=cJSON_GetObjectItemCaseSensitive(cc,"interval_ms");if(cJSON_IsNumber(x))c.control.interval_ms=x->valueint;}
    cJSON_Delete(r); return config_manager_save(&c);
}

esp_err_t config_manager_restore_defaults(void)
{
    app_config_t c; defaults(&c); return config_manager_save(&c);
}
