/* live_socket.c — one connection per browser instead of a pool of seven.
 *
 * WHY THIS EXISTS, measured rather than assumed.
 *
 * The interface polls seven endpoints: status every two seconds, meters,
 * inverters, inverter telemetry and the derived telemetry every four, the event
 * ring and the trend every ten. A browser does not open one connection for that
 * and reuse it. It opens its keep-alive pool and holds it.
 *
 *   one tab on the plant overview     10 established connections
 *   two tabs                          10 established -- the ceiling
 *
 * max_open_sockets is 10, and httpd takes three more internally. So ONE browser
 * consumes the controller's entire client budget, and a second engineer opening
 * the interface has nothing left. That is not a bandwidth problem and it does
 * not respond to a bandwidth fix: after cutting the Wi-Fi page from twenty
 * requests per twenty-one seconds to eleven, it still held ten sockets. The pool
 * is held because the browser opens it, not because the page is busy.
 *
 * A single long-lived socket is the only thing that changes that number.
 *
 * WHAT THIS IS NOT. It is not a speed improvement. The data behind those seven
 * endpoints changes about once a second -- the meter is polled at 400 ms, the
 * control loop decides at 1 Hz -- so pushing it faster would deliver nothing an
 * operator can act on. Every claim here is about sockets.
 *
 * WHAT GOES OVER IT. The live frame the operator screens render, and nothing
 * else. Configuration, engineering diagnostics, authentication and every action
 * stay on their own endpoints: they are asked for by a person, once, and putting
 * them here would mean inventing a request/response protocol inside a push
 * channel that does not need one.
 *
 * The frame is deliberately small. The seven endpoints together are about 6 kB;
 * sent every second that would be MORE traffic than the polling it replaces, so
 * this carries what the screens display and leaves the detail where it was.
 */
#include "live_socket.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "control_engine.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "inverter_manager.h"
#include "meter_manager.h"
#include "source_detection.h"

static const char *TAG = "live_ws";

/* Matches config.max_open_sockets in web_server.c. httpd_get_client_list()
 * writes at most this many, and asking for more than the server can hold is how
 * a stack buffer becomes a bug that only appears on a busy controller. */
#define LIVE_MAX_CLIENTS 10

/* One second, matching the rate the data behind it actually changes. Faster
 * would send the same numbers again; slower would make the screen lag the
 * control loop it is reporting on. */
#define LIVE_PUSH_PERIOD_MS 1000

#define LIVE_TASK_STACK_BYTES 5120
#define LIVE_TASK_PRIORITY 4

static httpd_handle_t s_server;
static TaskHandle_t s_task;

/*
 * The handshake, and nothing else.
 *
 * A GET on this URI with the upgrade headers is completed by httpd itself when
 * is_websocket is set; this handler is called once the connection is open. It
 * reads nothing: the channel is one-way by design. A client that needs to ASK
 * for something uses the ordinary endpoints, where the request has a method, a
 * status code and an error body -- none of which a push frame has.
 *
 * Frames that arrive anyway are drained and discarded rather than ignored,
 * because an unread frame stays in the socket buffer and the next read finds a
 * fragment of it.
 */
static esp_err_t live_handler(httpd_req_t *request)
{
    if (request->method == HTTP_GET) {
        ESP_LOGI(TAG, "live socket opened (fd %d)", httpd_req_to_sockfd(request));
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {0};
    frame.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t err = httpd_ws_recv_frame(request, &frame, 0);
    if (err != ESP_OK) return err;
    if (frame.len == 0U || frame.len > 256U) return ESP_OK;
    uint8_t *buffer = calloc(1, frame.len + 1U);
    if (!buffer) return ESP_ERR_NO_MEM;
    frame.payload = buffer;
    err = httpd_ws_recv_frame(request, &frame, frame.len);
    free(buffer);
    return err;
}

/* The live frame: what the operator screens draw, in one object.
 *
 * Named for what each value IS rather than which endpoint it used to come from,
 * because the endpoints are an accident of how this grew and the screens are
 * not. */
/* Rounded to 10 W, in DOUBLE.
 *
 * A float printed at full precision spends eighteen characters on a figure this
 * product displays to one decimal -- a fifth of the frame, every second, for
 * digits nobody reads. Rounded in single precision it comes back as
 * 310.079986572266 anyway, because cJSON prints the double: the same trap the
 * history series fell into. */
static void add_kw(cJSON *object, const char *name, float value)
{
    if (isfinite(value)) {
        cJSON_AddNumberToObject(object, name, round((double)value * 100.0) / 100.0);
    } else {
        cJSON_AddNullToObject(object, name);
    }
}

static char *build_frame(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    /* Returns void and always fills the struct; there is no failure to test. */
    control_status_t control = {0};
    control_engine_get_status(&control);
    {
        cJSON_AddBoolToObject(root, "control_enabled", control.enabled);
        add_kw(root, "grid_kw", control.grid_power_kw);
        /* REQUESTED is what the controller worked out, APPLIED is what it is
         * driving. They differ by permission alone and must not be merged. */
        add_kw(root, "requested_pv_kw", control.requested_pv_kw);
        add_kw(root, "applied_pv_kw", control.applied_pv_kw);
        cJSON_AddNumberToObject(root, "mode", control.mode);
    }

    source_detection_status_t source = {0};
    if (source_detection_get_status(&source) == ESP_OK) {
        cJSON_AddStringToObject(root, "source", source_detection_state_name(source.state));
    }

    /* Meters and inverters as the two short lists the screens iterate, not the
     * full diagnostic records: those carry register maps, error counters and
     * per-phase measurements that no live screen draws. */
    cJSON *meters = cJSON_AddArrayToObject(root, "meters");
    const uint8_t meter_count = meter_manager_get_count();
    for (uint8_t i = 0; meters && i < meter_count; ++i) {
        meter_data_t data = {0};
        if (!meter_manager_get_data(i, &data)) continue;
        cJSON *item = cJSON_CreateObject();
        if (!item) break;
        cJSON_AddBoolToObject(item, "online", data.online);
        add_kw(item, "kw", data.active_power_kw);
        cJSON_AddItemToArray(meters, item);
    }

    cJSON *inverters = cJSON_AddArrayToObject(root, "inverters");
    const uint8_t inverter_count = inverter_manager_get_count();
    for (uint8_t i = 0; inverters && i < inverter_count; ++i) {
        inverter_data_t data = {0};
        if (!inverter_manager_get_data(i, &data)) continue;
        cJSON *item = cJSON_CreateObject();
        if (!item) break;
        cJSON_AddBoolToObject(item, "online", data.online);
        add_kw(item, "kw", data.telemetry_valid ? data.measured_power_kw : NAN);
        cJSON_AddItemToArray(inverters, item);
    }

    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

/*
 * Sent to every client that is actually a websocket.
 *
 * httpd_get_client_list() returns every open descriptor, including ordinary
 * HTTP keep-alive connections, and writing a websocket frame down one of those
 * corrupts a response somebody is waiting for. httpd_ws_get_fd_info() is what
 * distinguishes them.
 *
 * A send that fails takes the client with it. A browser that closed its tab
 * without a close frame -- a laptop lid, a lost network -- would otherwise hold
 * its socket until the TCP timeout, which is precisely the shortage this
 * exists to remove.
 */
static void broadcast(const char *text)
{
    if (!s_server || !text) return;
    size_t count = LIVE_MAX_CLIENTS;
    int fds[LIVE_MAX_CLIENTS] = {0};
    if (httpd_get_client_list(s_server, &count, fds) != ESP_OK) return;

    httpd_ws_frame_t frame = {0};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = (uint8_t *)text;
    frame.len = strlen(text);

    for (size_t i = 0; i < count; ++i) {
        if (httpd_ws_get_fd_info(s_server, fds[i]) != HTTPD_WS_CLIENT_WEBSOCKET) continue;
        if (httpd_ws_send_frame_async(s_server, fds[i], &frame) != ESP_OK) {
            ESP_LOGW(TAG, "live socket fd %d did not accept a frame; closing it", fds[i]);
            httpd_sess_trigger_close(s_server, fds[i]);
        }
    }
}

static void live_task(void *argument)
{
    (void)argument;
    for (;;) {
        char *text = build_frame();
        if (text) {
            broadcast(text);
            free(text);
        }
        vTaskDelay(pdMS_TO_TICKS(LIVE_PUSH_PERIOD_MS));
    }
}

esp_err_t live_socket_register(httpd_handle_t server)
{
    if (!server) return ESP_ERR_INVALID_ARG;
    s_server = server;

    const httpd_uri_t uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = live_handler,
        .user_ctx = NULL,
        .is_websocket = true,
        /* The control frames are handled by httpd rather than delivered here:
         * a ping that this code had to answer would be a timer to get wrong. */
        .handle_ws_control_frames = false,
    };
    esp_err_t err = httpd_register_uri_handler(server, &uri);
    if (err != ESP_OK) return err;

    if (!s_task &&
        xTaskCreate(live_task, "live_ws", LIVE_TASK_STACK_BYTES, NULL,
                    LIVE_TASK_PRIORITY, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
