#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

/** Register the read-only EM500/DMG610-compatible snapshot endpoint. */
esp_err_t em500_api_register(httpd_handle_t server);
