#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"

/* Read-only snapshots of the same authoritative event/alarm state served by
 * the HTTP operator routes. Callers own the returned cJSON object and must
 * delete it. These builders do not acknowledge, clear, or mutate alarms. */
cJSON *operational_api_build_events_json(void);
cJSON *operational_api_build_alarms_json(void);

/* Mutate exactly one authoritative alarm lifecycle row. Authorization is the
 * caller's responsibility: the HTTP route enforces Engineering session auth,
 * while board-local callers must enforce their own Engineering boundary before
 * invoking this function. Returns false for an unknown/non-alarm code. */
bool operational_api_acknowledge_alarm(uint32_t code,
                                       bool *present,
                                       bool *was_outstanding);

esp_err_t operational_api_register(httpd_handle_t server);
