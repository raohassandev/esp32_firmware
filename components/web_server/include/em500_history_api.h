#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

/** Register the read-only maximum/minimum/average/demand endpoint. */
esp_err_t em500_history_api_register(httpd_handle_t server);
