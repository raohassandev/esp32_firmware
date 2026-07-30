/* GET /api/system/audit-log
 *
 * Reading the audit trail requires an authenticated engineering session. The
 * component-wide authorization gateway already wraps every non-public URI, and
 * the handler checks again explicitly. That duplication is deliberate: the
 * gateway is a build-level indirection, and an audit trail that silently became
 * world-readable because a registration path changed would be worse than no
 * audit trail, because it would be trusted.
 *
 * A denied read is itself audited.
 */

#include "audit_log.h"

#include <stdlib.h>

#include "cJSON.h"
#include "engineering_auth.h"
#include "esp_timer.h"

/* Bounded so the JSON document and the copied entries stay within what an
 * ESP32 HTTP handler can hold. The response states when it is truncated. */
#define AUDIT_EXPORT_MAX_ENTRIES 64u

static esp_err_t audit_log_get(httpd_req_t *request)
{
    if (!engineering_auth_is_authorized(request)) {
        audit_log_record(AUDIT_CATEGORY_AUTHENTICATION, AUDIT_ACTION_AUDIT_LOG_READ,
                         AUDIT_OUTCOME_DENIED);
        return engineering_auth_require(request);
    }

    audit_entry_t *entries = calloc(AUDIT_EXPORT_MAX_ENTRIES, sizeof(*entries));
    if (!entries) return httpd_resp_send_500(request);

    uint32_t overwritten = 0;
    uint32_t last_sequence = 0;
    /* The snapshot is taken first and JSON is built afterwards: cJSON must
     * never be called while the audit spinlock is held. */
    const uint16_t count = audit_log_snapshot(entries, (uint16_t)AUDIT_EXPORT_MAX_ENTRIES,
                                              &overwritten, &last_sequence);

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(entries);
        return httpd_resp_send_500(request);
    }

    /* Everything a reader needs in order not to over-trust this payload. */
    cJSON_AddStringToObject(root, "time_base", "uptime_relative");
    cJSON_AddStringToObject(root, "time_note",
                            "The controller has no real-time clock and no time source. Every "
                            "timestamp is milliseconds since the last boot, not a date. Correlate "
                            "with an external record using controller uptime.");
    cJSON_AddNumberToObject(root, "controller_uptime_ms",
                            (double)((uint64_t)esp_timer_get_time() / 1000ULL));
    cJSON_AddStringToObject(root, "storage", "ram_only");
    cJSON_AddBoolToObject(root, "persisted_across_reboot", false);
    cJSON_AddStringToObject(root, "storage_note",
                            "The audit trail is held in RAM and is lost on reboot or power loss. "
                            "It is not written to flash. Export it before restarting a controller "
                            "you are investigating.");
    cJSON_AddStringToObject(root, "actor_model", "engineering_session");
    cJSON_AddBoolToObject(root, "actor_identified", false);
    cJSON_AddStringToObject(root, "actor_note",
                            "This controller has no per-operator accounts. An entry records that "
                            "an authenticated engineering session performed the action. It cannot "
                            "and does not name a person.");
    cJSON_AddStringToObject(root, "credential_note",
                            "Authentication entries never contain a password, any part of one, "
                            "its length, or a session token. An audit entry has no text field.");

    cJSON_AddNumberToObject(root, "entry_count", count);
    cJSON_AddNumberToObject(root, "capacity", AUDIT_LOG_CAPACITY);
    cJSON_AddNumberToObject(root, "total_recorded", last_sequence);
    cJSON_AddNumberToObject(root, "dropped_oldest", overwritten);
    cJSON_AddBoolToObject(root, "truncated", overwritten > 0u);
    cJSON_AddNumberToObject(root, "export_limit", AUDIT_EXPORT_MAX_ENTRIES);

    cJSON *categories = cJSON_AddArrayToObject(root, "categories");
    if (categories) {
        for (uint8_t index = 0; index < (uint8_t)AUDIT_CATEGORY_COUNT; ++index) {
            cJSON_AddItemToArray(categories,
                                 cJSON_CreateString(audit_log_core_category_name(index)));
        }
    }

    cJSON *items = cJSON_AddArrayToObject(root, "entries");
    if (!items) {
        cJSON_Delete(root);
        free(entries);
        return httpd_resp_send_500(request);
    }
    for (uint16_t index = 0; index < count; ++index) {
        cJSON *item = cJSON_CreateObject();
        if (!item) break;
        cJSON_AddNumberToObject(item, "sequence", entries[index].sequence);
        cJSON_AddNumberToObject(item, "uptime_ms", (double)entries[index].uptime_ms);
        cJSON_AddStringToObject(item, "category",
                                audit_log_core_category_name(entries[index].category));
        cJSON_AddStringToObject(item, "action",
                                audit_log_core_action_name(entries[index].action));
        cJSON_AddStringToObject(item, "outcome",
                                audit_log_core_outcome_name(entries[index].outcome));
        cJSON_AddStringToObject(item, "actor", "authenticated_engineering_session");
        if (entries[index].has_value) cJSON_AddNumberToObject(item, "value", entries[index].value);
        else cJSON_AddNullToObject(item, "value");
        cJSON_AddItemToArray(items, item);
    }
    free(entries);

    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) return httpd_resp_send_500(request);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    const esp_err_t err = httpd_resp_sendstr(request, text);
    free(text);
    return err;
}

esp_err_t audit_log_api_register(httpd_handle_t server)
{
    const httpd_uri_t handler = {
        .uri = "/api/system/audit-log",
        .method = HTTP_GET,
        .handler = audit_log_get,
    };
    return httpd_register_uri_handler(server, &handler);
}
