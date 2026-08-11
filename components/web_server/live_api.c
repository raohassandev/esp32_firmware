/* live_api.c — the small, fast endpoint the operator screens read.
 *
 * WHY IT EXISTS. The plant overview took its headline figures from /api/status:
 * 1.7 kB, polled every two seconds. The numbers a person watches -- grid power,
 * solar, the commanded percentage -- were therefore up to two seconds behind a
 * controller that reads its meter every 300 ms and decides every second.
 *
 * MEASURED, before choosing anything. The board answers a small request in 84 ms
 * (median of twenty; 59 ms best, 202 ms worst), and the meter reading it holds
 * refreshes every 300 to 600 ms.
 *
 * So: this carries only what changes fast, and the interface asks for it every
 * 500 ms. That is FEWER bytes than before -- about 300 against 1.7 kB, so
 * roughly 600 B/s against 850 -- and the figure on screen is four times fresher.
 * The larger endpoints slow down to 10-15 s, since register maps, error counters
 * and per-phase measurements do not change at the rate anybody watches them.
 *
 * WHY NOT FASTER THAN 500 ms, which is a real limit and not caution:
 *
 *   grid power   500 ms   the meter itself produces a new value every 300-600 ms
 *   solar        1 s      the inverter is polled once a second
 *   requested    1 s      the control loop decides once a second
 *
 * Polling at 300 ms would re-send the same numbers half the time, and with a
 * worst-case 202 ms response it would start overlapping its own requests. There
 * is no information below 500 ms to go and get.
 *
 * WHAT IS DELIBERATELY HERE. Not just the kW figures, but the three short facts
 * that explain them: the control mode, why it is inhibited, and whether the
 * commanded percentage is actually being sent. Those live on /api/status, which
 * is about to be polled at 10 s -- and a reason that lags its number by ten
 * seconds is worse than no reason, because for those ten seconds it explains the
 * wrong figure.
 *
 * NOT engineering-gated. Every value here is already on the public status
 * endpoint, and this is the plant overview's data: gating it would leave the
 * operator screens unable to draw themselves.
 */
#include "live_api.h"

#include <math.h>
#include <stdlib.h>

#include "cJSON.h"
#include "control_engine.h"
#include "esp_log.h"
#include "inverter_manager.h"
#include "meter_manager.h"
#include "source_detection.h"

/* Same shape as every other JSON responder here: no-store, nosniff, and the
 * tree freed on both paths. */
static esp_err_t send_json(httpd_req_t *request, cJSON *root)
{
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) return httpd_resp_send_500(request);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    esp_err_t err = httpd_resp_sendstr(request, text);
    free(text);
    return err;
}

/* Rounded to 10 W, in DOUBLE.
 *
 * A float printed at full precision spends eighteen characters on a figure this
 * product shows to one decimal. Rounded in SINGLE precision it comes back as
 * 310.079986572266 anyway, because cJSON prints the double -- the same trap the
 * history series fell into. */
static void add_kw(cJSON *object, const char *name, float value)
{
    if (isfinite(value)) {
        cJSON_AddNumberToObject(object, name, round((double)value * 100.0) / 100.0);
    } else {
        cJSON_AddNullToObject(object, name);
    }
}

static esp_err_t live_get(httpd_req_t *request)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);

    control_status_t control = {0};
    control_engine_get_status(&control);

    add_kw(root, "grid_kw", control.grid_power_kw);
    /* REQUESTED is what the controller worked out, APPLIED is what it is
     * driving. They differ by permission alone and are never merged. */
    add_kw(root, "requested_pv_kw", control.requested_pv_kw);
    add_kw(root, "applied_pv_kw", control.applied_pv_kw);
    cJSON_AddBoolToObject(root, "control_enabled", control.enabled);
    cJSON_AddStringToObject(root, "mode_label",
                            !control.enabled      ? "Monitoring only"
                            : control.command_authority ? "Commanding"
                                                        : "Inhibited");
    cJSON_AddStringToObject(root, "inhibit_reason", control.inhibit_reason);

    source_detection_status_t source = {0};
    if (source_detection_get_status(&source) == ESP_OK) {
        cJSON_AddStringToObject(root, "source", source_detection_state_name(source.state));
    }

    /* Solar as the fleet measures it, and the capacity the controller may
     * actually move -- the second is what decides whether starting automatic
     * control would change anything, and it is the figure the overview reads to
     * say so. */
    float solar_kw = 0.0f;
    bool solar_known = false;
    const uint8_t inverters = inverter_manager_get_count();
    for (uint8_t i = 0; i < inverters; ++i) {
        inverter_data_t data = {0};
        if (!inverter_manager_get_data(i, &data)) continue;
        if (!data.telemetry_valid || data.telemetry_stale) continue;
        if (!isfinite(data.measured_power_kw)) continue;
        solar_kw += data.measured_power_kw;
        solar_known = true;
    }
    /* NaN rather than zero when nothing is measuring: a plant with no reachable
     * inverter has an UNKNOWN output, and 0.0 kW is a measurement nobody took. */
    add_kw(root, "solar_kw", solar_known ? solar_kw : NAN);

    /*
     * PER MACHINE AS WELL AS THE TOTAL.
     *
     * The fleet total alone left the Solar inverters table -- one row per
     * machine, with its own NOW column -- reading from a ten-second poll, so a
     * command that visibly moved the plant took ten to fifteen seconds to appear
     * against the inverter it moved. The owner watched production jump and the
     * screen sit still.
     *
     * Index and kW only. Everything else on that row -- state, rating, last
     * update, the register detail -- changes at commissioning speed and stays on
     * the slow endpoint.
     */
    cJSON *list = cJSON_AddArrayToObject(root, "inverters");
    for (uint8_t i = 0; list && i < inverters; ++i) {
        inverter_data_t data = {0};
        if (!inverter_manager_get_data(i, &data)) continue;
        cJSON *item = cJSON_CreateObject();
        if (!item) break;
        cJSON_AddNumberToObject(item, "index", i);
        cJSON_AddBoolToObject(item, "online", data.online);
        add_kw(item, "kw", (data.telemetry_valid && !data.telemetry_stale)
                               ? data.measured_power_kw : NAN);
        cJSON_AddItemToArray(list, item);
    }
    add_kw(root, "commandable_kw", inverter_manager_get_total_rated_kw());

    /* The fleet's commanded percentage and whether it is going out, so the card
     * that shows it in green or amber does not need a second request to find
     * out which. */
    cJSON *fleet = cJSON_AddObjectToObject(root, "command");
    if (fleet) {
        inverter_command_preview_t preview = {0};
        bool have = false, in_force = inverters > 0U;
        float percent = 0.0f;
        const char *blocked = NULL;
        for (uint8_t i = 0; i < inverters; ++i) {
            if (!inverter_manager_preview_command(i, control.requested_pv_kw, &preview)) continue;
            if (!preview.available) continue;
            if (!have || preview.percent > percent) percent = preview.percent;
            have = true;
            if (!preview.would_write) {
                in_force = false;
                if (!blocked) blocked = preview.blocked_by;
            }
        }
        if (have) {
            cJSON_AddNumberToObject(fleet, "percent", round((double)percent));
            cJSON_AddBoolToObject(fleet, "in_force", in_force);
            if (blocked) cJSON_AddStringToObject(fleet, "blocked_by", blocked);
        }
    }

    meter_data_t meter = {0};
    if (meter_manager_get_data(0, &meter)) {
        cJSON_AddBoolToObject(root, "meter_online", meter.online);
    }

    return send_json(request, root);
}

esp_err_t live_api_register(httpd_handle_t server)
{
    if (!server) return ESP_ERR_INVALID_ARG;
    const httpd_uri_t uri = {
        .uri = "/api/live", .method = HTTP_GET, .handler = live_get, .user_ctx = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
