#include "web_server.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "web_api.h"
#include "web_assets.h"

static httpd_handle_t s_server;

static esp_err_t index_handler(httpd_req_t *request)
{
    size_t length = 0;
    const char *html = web_assets_index(&length);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, html, length);
}

esp_err_t web_server_start(void)
{
    if (s_server) return ESP_OK;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12;
    config.stack_size = 6144;
    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), "web", "HTTP server start failed");
    httpd_uri_t index = {.uri = "/", .method = HTTP_GET, .handler = index_handler};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &index), "web", "index registration failed");
    return web_api_register(s_server);
}

void web_server_stop(void)
{
    if (s_server) httpd_stop(s_server);
    s_server = NULL;
}
