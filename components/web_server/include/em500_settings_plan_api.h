#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

/** Register the validation-only CT/PT/wiring/tariff change-plan endpoint. */
esp_err_t em500_settings_plan_api_register(httpd_handle_t server);
