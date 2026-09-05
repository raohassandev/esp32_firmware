#pragma once

#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"

/* Read-only snapshots of the same authoritative event/alarm state served by
 * the HTTP operator routes. Callers own the returned cJSON object and must
 * delete it. These builders do not acknowledge, clear, or mutate alarms. */
cJSON *operational_api_build_events_json(void);
cJSON *operational_api_build_alarms_json(void);

esp_err_t operational_api_register(httpd_handle_t server);
