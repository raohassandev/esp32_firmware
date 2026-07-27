#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t engineering_auth_init(void);
esp_err_t engineering_auth_register(httpd_handle_t server);
bool engineering_auth_is_authorized(httpd_req_t *request);
esp_err_t engineering_auth_require(httpd_req_t *request);
esp_err_t engineering_auth_guarded_handler(httpd_req_t *request);

#ifdef __cplusplus
}
#endif
