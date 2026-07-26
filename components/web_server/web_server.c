#include "web_server.h"
#include "device_api.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "telemetry_profile_api.h"
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

static esp_err_t send_asset(httpd_req_t *request, const char *content_type,
                             const char *content, size_t length)
{
    set_asset_headers(request, content_type);
    return httpd_resp_send(request, content, length);
}

static esp_err_t send_asset_parts(httpd_req_t *request, const char *content_type,
                                   const asset_getter_t *getters, size_t count)
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

static esp_err_t css_handler(httpd_req_t *request)
{
    static const asset_getter_t assets[] = {
        web_assets_css,
        web_assets_wifi_css,
        web_assets_devices_css,
        web_assets_inverter_telemetry_css
    };
    return send_asset_parts(request, "text/css; charset=utf-8",
                            assets, sizeof(assets) / sizeof(assets[0]));
}

static esp_err_t js_handler(httpd_req_t *request)
{
    static const asset_getter_t assets[] = {
        web_assets_js,
        web_assets_wifi_utils_js,
        web_assets_wifi_guard_js,
        web_assets_wifi_js,
        web_assets_devices_utils_js,
        web_assets_devices_js,
        web_assets_inverter_telemetry_utils_js,
        web_assets_inverter_telemetry_js,
        web_assets_inverter_telemetry_sync_js
    };
    return send_asset_parts(request, "application/javascript; charset=utf-8",
                            assets, sizeof(assets) / sizeof(assets[0]));
}

esp_err_t web_server_start(void)
{
    if (s_server) return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.stack_size = 7168;
    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), "web", "HTTP server start failed");

    const httpd_uri_t assets[] = {
        {.uri = "/", .method = HTTP_GET, .handler = index_handler},
        {.uri = "/app.css", .method = HTTP_GET, .handler = css_handler},
        {.uri = "/app.js", .method = HTTP_GET, .handler = js_handler}
    };

    for (size_t index = 0; index < sizeof(assets) / sizeof(assets[0]); ++index) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &assets[index]),
                            "web", "asset registration failed");
    }

    ESP_RETURN_ON_ERROR(web_api_register(s_server), "web", "core API registration failed");
    ESP_RETURN_ON_ERROR(device_api_register(s_server), "web", "device API registration failed");
    return telemetry_profile_api_register(s_server);
}

void web_server_stop(void)
{
    if (s_server) httpd_stop(s_server);
    s_server = NULL;
}
