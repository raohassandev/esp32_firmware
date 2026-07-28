#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t em500_cache_api_register(httpd_handle_t server);
