#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "auth_hmac_compat.h"
#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef esp_err_t (*engineering_httpd_uri_func_t)(httpd_req_t *request);

typedef enum {
    ENGINEERING_LOCAL_AUTH_OK = 0,
    ENGINEERING_LOCAL_AUTH_DENIED,
    ENGINEERING_LOCAL_AUTH_LOCKED,
} engineering_local_auth_result_t;

esp_err_t engineering_auth_init(void);
esp_err_t engineering_auth_register(httpd_handle_t server);
bool engineering_auth_is_authorized(httpd_req_t *request);
esp_err_t engineering_auth_require(httpd_req_t *request);
esp_err_t engineering_auth_guarded_handler(httpd_req_t *request);
esp_err_t engineering_register_uri_handler(httpd_handle_t server,
                                           const httpd_uri_t *uri_handler);

/* Verify the same Engineering credential used by the protected web workspace
 * without creating or consuming an HTTP cookie session. This is only for a
 * physically local UI such as the integrated commissioning HMI. The verifier
 * shares the web login's password/setup-code authority, failed-attempt counter,
 * and lockout state; it never creates a second PIN or stores the supplied
 * credential. */
engineering_local_auth_result_t engineering_auth_verify_local_credential(
    const char *credential,
    uint32_t *retry_after_ms,
    bool *setup_required);

#ifdef __cplusplus
}
#endif

/* engineering_guard.c historically used an ESP-IDF-internal callback alias.
 * Keep a local, explicitly defined alias so the gateway remains portable across
 * ESP-IDF releases without depending on private typedef names. */
typedef engineering_httpd_uri_func_t httpd_uri_func;

/* All normal web-server translation units route URI registration through the
 * authorization gateway. The gateway implementation itself defines
 * ENGINEERING_GUARD_IMPLEMENTATION so it can call ESP-IDF's real function. */
#ifndef ENGINEERING_GUARD_IMPLEMENTATION
#define httpd_register_uri_handler engineering_register_uri_handler
#endif
