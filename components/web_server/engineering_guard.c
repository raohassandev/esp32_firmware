#include "engineering_auth.h"

esp_err_t engineering_auth_guarded_handler(httpd_req_t *request)
{
    if (!engineering_auth_is_authorized(request)) {
        return engineering_auth_require(request);
    }
    httpd_uri_func handler = (httpd_uri_func)request->user_ctx;
    if (!handler) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Engineering handler is unavailable");
    }
    return handler(request);
}
