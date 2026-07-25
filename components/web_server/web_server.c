#include "web_server.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "web_api.h"
#include "web_assets.h"

static httpd_handle_t s_server;

static esp_err_t send_asset(httpd_req_t *request, const char *content_type,
                            const char *content, size_t length)
{
    httpd_resp_set_type(request, content_type);
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    return httpd_resp_send(request, content, length);
}

static esp_err_t index_handler(httpd_req_t *request)
{
    size_t length = 0;
    const char *content = web_assets_index(&length);
    return send_asset(request, "text/html; charset=utf-8", content, length);
}

static esp_err_t css_handler(httpd_req_t *request)
{
    size_t length = 0;
    const char *content = web_assets_css(&length);
    return send_asset(request, "text/css; charset=utf-8", content, length);
}

static esp_err_t js_handler(httpd_req_t *request)
{
    size_t length = 0;
    const char *content = web_assets_js(&length);
    return send_asset(request, "application/javascript; charset=utf-8", content, length);
}

esp_err_t web_server_start(void)
{
    if (s_server) return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.stack_size = 6144;
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

    return web_api_register(s_server);
}

void web_server_stop(void)
{
    if (s_server) httpd_stop(s_server);
    s_server = NULL;
}
