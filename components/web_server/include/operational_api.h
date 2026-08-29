#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

typedef struct cJSON cJSON;

/* Authoritative operational payload builders shared by the HTTP API and
 * the same-MCU native HMI. Caller owns the returned cJSON object and must
 * release it with cJSON_Delete(). NULL means the snapshot could not be
 * constructed. Keeping the builder here prevents the LCD from re-deriving
 * alarm lifecycle, suppression, causality, priority or event wording. */
cJSON *operational_api_build_events_json(void);
cJSON *operational_api_build_alarms_json(void);

esp_err_t operational_api_register(httpd_handle_t server);
