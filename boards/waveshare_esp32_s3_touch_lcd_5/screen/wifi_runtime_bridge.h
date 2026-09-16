#pragma once

#include <stdbool.h>

#include "pvdg_ui_model.h"
#include "pvdg_ui_wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

void wifi_runtime_bridge_refresh(pvdg_ui_model_t *model,
                                 pvdg_ui_wifi_scan_t *scan);
void wifi_runtime_bridge_request_scan(void *user);
void wifi_runtime_bridge_request_reconnect(void *user);
bool wifi_runtime_bridge_load_config(pvdg_ui_wifi_config_t *config);
void wifi_runtime_bridge_submit_config(const pvdg_ui_wifi_config_t *config,
                                       bool primary_changed,
                                       void *user);

#ifdef __cplusplus
}
#endif
