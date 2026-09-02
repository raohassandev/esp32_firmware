#include "web_server.h"

#include <string.h>
#include "commissioning_gate_api.h"
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
#include "esp_log.h"
#include "inverter_config_api.h"
#include "live_api.h"
#include "inverter_profile_api.h"
#include "meter_config_api.h"
#include "meter_read_jobs.h"
#include "operational_api.h"
#include "solar_grid_api.h"
#include "solar_grid_status_api.h"
#include "source_detection_api.h"
#include "system_resource_api.h"
#include "web_api.h"
#include "web_assets.h"

/* DIAGNOSTIC-ONLY branch switch. The exact 01c1c272 hardware candidate still
 * showed the 5-second top-to-bottom RGB sweep. This single-variable image
 * removes the operational history/event subsystem (including its 5-second
 * task and journal open/flush path) while leaving Product Core, native screen,
 * network, live/status APIs and display timing untouched. Never merge this
 * switch into a release branch; its only purpose is physical root-cause
 * isolation. */
#define WAVESHARE_DIAG_SKIP_OPERATIONAL_API 1

static httpd_handle_t s_server;

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

/*
 * THE TWO BUNDLES, PRE-BUILT AND PRE-COMPRESSED.
 *
 * These handlers used to concatenate thirty-seven embedded blobs at request
 * time and send 873 KB of JavaScript uncompressed over Wi-Fi. Measured from the
 * board that is roughly 27 seconds before the interface appears -- long enough
 * that people conclude the controller has crashed, reload, and make it worse.
 *
 * The bundle is fixed the moment the firmware is built, so it is built and
 * gzipped then: see tools/build_bundle.py and the order files web/app.js.order
 * and web/app.css.order. Compressing on the device would spend CPU the control
 * loop needs, every page load, recomputing an answer that cannot change.
 *
 * BOTH FORMS ARE KEPT. Every browser sends Accept-Encoding: gzip, but a
 * commissioning script written with plain curl does not, and answering that
 * with gzip bytes it never asked for hands somebody a file of binary garbage
 * during a site visit. The uncompressed copy costs flash and removes the whole
 * class of surprise.
 */
static bool client_accepts_gzip(httpd_req_t *request)
{
    /* Long enough for the header real clients send; a value longer than this is
     * treated as "not offered" rather than truncated and guessed at, because a
     * truncated match could claim gzip support the client does not have. */
    char encodings[96] = {0};
    if (httpd_req_get_hdr_value_str(request, "Accept-Encoding",
                                    encodings, sizeof(encodings)) != ESP_OK) {
        return false;
    }
    /* A substring match is enough here. The formally correct parse would honour
     * "gzip;q=0" -- a client explicitly refusing gzip while listing it -- which
     * no browser sends and which would only ever cost this device a needless
     * decompression on the client side. */
    return strstr(encodings, "gzip") != NULL;
}

static esp_err_t send_bundle(httpd_req_t *request, const char *content_type,
                             const char *plain, size_t plain_length,
                             const char *compressed, size_t compressed_length)
{
    const bool gzip = client_accepts_gzip(request) && compressed_length > 0;
    set_asset_headers(request, content_type);
    if (gzip) {
        httpd_resp_set_hdr(request, "Content-Encoding", "gzip");
        /* Caches and proxies must not serve the compressed body to a client
         * that asked for the plain one, and vice versa. */
        httpd_resp_set_hdr(request, "Vary", "Accept-Encoding");
        return httpd_resp_send(request, compressed, compressed_length);
    }
    httpd_resp_set_hdr(request, "Vary", "Accept-Encoding");
    return httpd_resp_send(request, plain, plain_length);
}

static esp_err_t css_handler(httpd_req_t *request)
{
    size_t plain_length = 0;
    size_t gz_length = 0;
    const char *plain = web_assets_bundle_css(&plain_length);
    const char *compressed = web_assets_bundle_css_gz(&gz_length);
    return send_bundle(request, "text/css; charset=utf-8",
                       plain, plain_length, compressed, gz_length);
}

static esp_err_t js_handler(httpd_req_t *request)
{
    size_t plain_length = 0;
    size_t gz_length = 0;
    const char *plain = web_assets_bundle_js(&plain_length);
    const char *compressed = web_assets_bundle_js_gz(&gz_length);
    return send_bundle(request, "application/javascript; charset=utf-8",
                       plain, plain_length, compressed, gz_length);
}

esp_err_t web_server_start(void)
{
    if (s_server) return ESP_OK;
    ESP_RETURN_ON_ERROR(engineering_auth_init(), "web", "engineering authentication init failed");
    ESP_RETURN_ON_ERROR(em500_cache_init(), "web", "EM500 acquisition cache init failed");
    ESP_RETURN_ON_ERROR(meter_read_jobs_init(), "web", "meter read-job queue init failed");
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    /* Raised alongside the commissioning-gate and write-confirmation
     * endpoints. httpd refuses to register beyond this limit, and a
     * refused safety endpoint would be a silent loss of visibility. */
    /* Must stay ahead of the routes actually registered: every registration site
     * propagates failure, so an overflow does not drop one endpoint, it aborts
     * web_server start and leaves the unit with no web interface. A slot is a
     * few bytes, headroom is not. tests/uri_handler_capacity_source_contract.py
     * counts the routes and fails if this number stops leading them. */
    config.max_uri_handlers = 62;
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
    ESP_RETURN_ON_ERROR(live_api_register(s_server), "web", "live API registration failed");
#if WAVESHARE_DIAG_SKIP_OPERATIONAL_API
    ESP_LOGW("web", "DIAGNOSTIC: operational history/event subsystem disabled for RGB scanout isolation");
#else
    ESP_RETURN_ON_ERROR(operational_api_register(s_server), "web", "operator history/event API registration failed");
#endif
    ESP_RETURN_ON_ERROR(device_api_register(s_server), "web", "device API registration failed");
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
    ESP_RETURN_ON_ERROR(solar_grid_status_api_register(s_server), "web", "Solar-Grid status API registration failed");
    return commissioning_gate_api_register(s_server);
}

void web_server_stop(void)
{
    if (s_server) httpd_stop(s_server);
    s_server = NULL;
}
