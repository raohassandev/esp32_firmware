#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

/** Register the read-only EM500/DMG610-compatible M01-M18 setup endpoint. */
esp_err_t em500_settings_api_register(httpd_handle_t server);
