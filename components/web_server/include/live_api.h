#pragma once
#include "esp_err.h"
#include "esp_http_server.h"

/* Registers /api/live: the small, fast payload the operator screens read at
 * 500 ms, while the larger endpoints slow to 10-15 s. See live_api.c for the
 * measurements that set both numbers. */
esp_err_t live_api_register(httpd_handle_t server);
