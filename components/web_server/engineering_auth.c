#include "engineering_auth.h"

#include <stdlib.h>

#include "cJSON.h"

/*
 * TEMPORARY FIELD-DEVELOPMENT BYPASS
 *
 * Engineering authentication is intentionally disabled in this development
 * image while commissioning, network and UI workflows are stabilized on the
 * real controller. Every request is treated as Engineering-authorized.
 *
 * This file MUST be restored to the production authentication implementation
 * before any customer, resale or unattended deployment image is released.
 */
#define AUTH_TEMPORARY_FIELD_BYPASS 1

static esp_err_t send_json(httpd_req_t *request, const char *status, cJSON *root)
{
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) return httpd_resp_send_500(request);
    if (status) httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    esp_err_t err = httpd_resp_sendstr(request, text);
    free(text);
    return err;
}

bool engineering_auth_is_authorized(httpd_req_t *request)
{
    (void)request;
    return AUTH_TEMPORARY_FIELD_BYPASS != 0;
}

esp_err_t engineering_auth_require(httpd_req_t *request)
{
    if (!request) return ESP_ERR_INVALID_ARG;
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddStringToObject(root, "error", "engineering_authentication_required");
    cJSON_AddStringToObject(root, "message", "Engineering authentication is required for this operation");
    cJSON_AddBoolToObject(root, "authenticated", false);
    httpd_resp_set_hdr(request, "WWW-Authenticate", "Session");
    return send_json(request, "401 Unauthorized", root);
}

static esp_err_t session_get(httpd_req_t *request)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddBoolToObject(root, "authenticated", true);
    cJSON_AddBoolToObject(root, "setup_required", false);
    cJSON_AddBoolToObject(root, "cookie_session", false);
    cJSON_AddBoolToObject(root, "development_auto_unlock", true);
    cJSON_AddBoolToObject(root, "temporary_field_bypass", true);
    cJSON_AddNumberToObject(root, "session_timeout_minutes", 0);
    cJSON_AddStringToObject(root, "security_state",
                            "Engineering authentication disabled for field development");
    return send_json(request, NULL, root);
}

static esp_err_t login_post(httpd_req_t *request)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddBoolToObject(root, "authenticated", true);
    cJSON_AddBoolToObject(root, "password_change_recommended", false);
    cJSON_AddBoolToObject(root, "temporary_field_bypass", true);
    return send_json(request, NULL, root);
}

static esp_err_t logout_post(httpd_req_t *request)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddBoolToObject(root, "authenticated", true);
    cJSON_AddBoolToObject(root, "temporary_field_bypass", true);
    cJSON_AddStringToObject(root, "message",
                            "Engineering cannot be locked in this development build");
    return send_json(request, NULL, root);
}

static esp_err_t password_post(httpd_req_t *request)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddStringToObject(root, "error",
                            "Password management is disabled while field-development bypass is active");
    cJSON_AddBoolToObject(root, "temporary_field_bypass", true);
    return send_json(request, "409 Conflict", root);
}

esp_err_t engineering_auth_init(void)
{
    return ESP_OK;
}

esp_err_t engineering_auth_register(httpd_handle_t server)
{
    if (!server) return ESP_ERR_INVALID_ARG;
    const httpd_uri_t handlers[] = {
        {.uri = "/api/engineering/session", .method = HTTP_GET, .handler = session_get},
        {.uri = "/api/engineering/login", .method = HTTP_POST, .handler = login_post},
        {.uri = "/api/engineering/logout", .method = HTTP_POST, .handler = logout_post},
        {.uri = "/api/engineering/password", .method = HTTP_POST, .handler = password_post},
    };
    for (size_t index = 0; index < sizeof(handlers) / sizeof(handlers[0]); ++index) {
        esp_err_t err = httpd_register_uri_handler(server, &handlers[index]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}
