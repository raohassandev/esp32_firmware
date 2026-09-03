#include "web_server.h"
#include "device_api.h"
#include "em500_api.h"
#include "em500_cache.h"
#include "em500_cache_api.h"
#include "em500_history_api.h"
#include "em500_settings_api.h"
#include "em500_settings_plan_api.h"
#include "engineering_auth.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "inverter_config_api.h"
#include "inverter_profile_api.h"
#include "meter_config_api.h"
#include "meter_read_jobs.h"
#include "modbus_connection_api.h"
#include "operational_api.h"
#include "solar_grid_api.h"
#include "solar_grid_status_api.h"
#include "source_detection_api.h"
#include "system_resource_api.h"
#include "web_api.h"
#include "web_assets.h"

static httpd_handle_t s_server;
typedef const char *(*asset_getter_t)(size_t *length);

static void set_asset_headers(httpd_req_t *request, const char *content_type)
{
    httpd_resp_set_type(request, content_type);
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
}

static esp_err_t send_asset(httpd_req_t *request, const char *content_type, const char *content, size_t length)
{
    set_asset_headers(request, content_type);
    return httpd_resp_send(request, content, length);
}

static esp_err_t send_asset_parts(httpd_req_t *request, const char *content_type, const asset_getter_t *getters, size_t count)
{
    set_asset_headers(request, content_type);
    for (size_t index = 0; index < count; ++index) {
        size_t length = 0;
        const char *content = getters[index](&length);
        esp_err_t err = httpd_resp_send_chunk(request, content, length);
        if (err != ESP_OK) {
            httpd_resp_send_chunk(request, NULL, 0);
            return err;
        }
    }
    return httpd_resp_send_chunk(request, NULL, 0);
}

static esp_err_t index_handler(httpd_req_t *request)
{
    size_t length = 0;
    const char *content = web_assets_index(&length);
    return send_asset(request, "text/html; charset=utf-8", content, length);
}

static esp_err_t favicon_handler(httpd_req_t *request)
{
    httpd_resp_set_status(request, "204 No Content");
    httpd_resp_set_hdr(request, "Cache-Control", "public, max-age=86400");
    return httpd_resp_send(request, NULL, 0);
}

static esp_err_t css_handler(httpd_req_t *request)
{
    static const asset_getter_t assets[] = {
        web_assets_css,
        web_assets_wifi_css,
        web_assets_devices_css,
        web_assets_em500_css,
        web_assets_theme_css,
        web_assets_product_mode_css,
        web_assets_operator_operations_css,
        web_assets_operator_product_suite_css,
        web_assets_prelab_readiness_css,
        web_assets_mobile_prelab_fixes_css,
        web_assets_product_shell_v2_css,
        web_assets_product_experience_v2_css,
        web_assets_commissioning_wizard_v2_css,
        web_assets_commissioning_release_v3_css
    };
    return send_asset_parts(request, "text/css; charset=utf-8", assets, sizeof(assets) / sizeof(assets[0]));
}

static esp_err_t js_handler(httpd_req_t *request)
{
    static const asset_getter_t assets[] = {
        web_assets_theme_js,
        web_assets_product_mode_js,
        web_assets_js,
        web_assets_wifi_utils_js,
        web_assets_wifi_guard_js,
        web_assets_wifi_js,
        web_assets_network_commissioning_fix_js,
        web_assets_devices_utils_js,
        web_assets_devices_js,
        web_assets_devices_refresh_js,
        web_assets_inverter_profiles_js,
        web_assets_inverter_config_js,
        web_assets_inverter_telemetry_js,
        web_assets_em500_utils_js,
        web_assets_em500_core_js,
        web_assets_em500_quality_js,
        web_assets_em500_profiles_js,
        web_assets_em500_plan_js,
        web_assets_source_detection_js,
        web_assets_solar_grid_js,
        web_assets_operator_view_js,
        web_assets_operator_operations_js,
        web_assets_operator_product_suite_js,
        web_assets_prelab_readiness_js,
        web_assets_commissioning_route_js,
        web_assets_commissioning_wizard_v2_js,
        web_assets_engineering_errors_js,
        web_assets_ui_enhancements_js,
        web_assets_product_shell_v2_js,
        web_assets_product_experience_v2_js,
        web_assets_commissioning_release_v3_js
    };
    return send_asset_parts(request, "application/javascript; charset=utf-8", assets, sizeof(assets) / sizeof(assets[0]));
}

esp_err_t web_server_start(void)
{
    if (s_server) return ESP_OK;
    ESP_RETURN_ON_ERROR(engineering_auth_init(), "web", "engineering authentication init failed");
    ESP_RETURN_ON_ERROR(em500_cache_init(), "web", "EM500 acquisition cache init failed");
    ESP_RETURN_ON_ERROR(meter_read_jobs_init(), "web", "meter read-job queue init failed");
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 40;
    config.stack_size = 8192;
    /* The default of 7 leaves only 4 client sockets once httpd takes its 3
     * internal ones, and a browser opens up to 6 keep-alive connections per
     * origin. One tab could therefore hold the pool and lock every other client
     * out - including the operator UI and a second engineer. Observed on
     * hardware: with a browser attached, unrelated requests timed out entirely
     * and recovered the instant the browser closed.
     * httpd enforces max_open_sockets + 3 <= CONFIG_LWIP_MAX_SOCKETS and
     * refuses to start otherwise. The lwIP pool is shared with everything else
     * on the device, so this must not consume all of it: the Modbus TCP client
     * that polls the meters needs sockets too, as do DHCP and DNS. With
     * CONFIG_LWIP_MAX_SOCKETS raised to 16 (see sdkconfig.defaults), 10 leaves
     * httpd using 13 and three spare for the rest of the system. */
    config.max_open_sockets = 10;
    /* Without this the pool is never reclaimed: once full, httpd simply stops
     * accepting instead of closing the least recently used connection, so a
     * single stale client can wedge the server indefinitely. */
    config.lru_purge_enable = true;
    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), "web", "HTTP server start failed");
    const httpd_uri_t assets[] = {
        {.uri = "/", .method = HTTP_GET, .handler = index_handler},
        {.uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_handler},
        {.uri = "/app.css", .method = HTTP_GET, .handler = css_handler},
        {.uri = "/app.js", .method = HTTP_GET, .handler = js_handler}
    };
    for (size_t index = 0; index < sizeof(assets) / sizeof(assets[0]); ++index) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &assets[index]), "web", "asset registration failed");
    }
    ESP_RETURN_ON_ERROR(engineering_auth_register(s_server), "web", "engineering auth API registration failed");
    ESP_RETURN_ON_ERROR(web_api_register(s_server), "web", "core API registration failed");
    ESP_RETURN_ON_ERROR(operational_api_register(s_server), "web", "operator history/event API registration failed");
    ESP_RETURN_ON_ERROR(device_api_register(s_server), "web", "device API registration failed");
    ESP_RETURN_ON_ERROR(modbus_connection_api_register(s_server), "web", "Modbus connection diagnostics registration failed");
    ESP_RETURN_ON_ERROR(inverter_profile_api_register(s_server), "web", "inverter profile API registration failed");
    ESP_RETURN_ON_ERROR(inverter_config_api_register(s_server), "web", "inverter configuration API registration failed");
    ESP_RETURN_ON_ERROR(meter_config_api_register(s_server), "web", "meter configuration API registration failed");
    ESP_RETURN_ON_ERROR(source_detection_api_register(s_server), "web", "source detection API registration failed");
    ESP_RETURN_ON_ERROR(system_resource_api_register(s_server), "web", "system resource API registration failed");
    ESP_RETURN_ON_ERROR(em500_cache_api_register(s_server), "web", "EM500 cache API registration failed");
    ESP_RETURN_ON_ERROR(em500_api_register(s_server), "web", "EM500 snapshot API registration failed");
    ESP_RETURN_ON_ERROR(em500_history_api_register(s_server), "web", "EM500 history API registration failed");
    ESP_RETURN_ON_ERROR(em500_settings_api_register(s_server), "web", "EM500 settings API registration failed");
    ESP_RETURN_ON_ERROR(em500_settings_plan_api_register(s_server), "web", "EM500 settings plan API registration failed");
    ESP_RETURN_ON_ERROR(solar_grid_api_register(s_server), "web", "Solar-Grid configuration API registration failed");
    return solar_grid_status_api_register(s_server);
}

void web_server_stop(void)
{
    if (s_server) httpd_stop(s_server);
    s_server = NULL;
}
