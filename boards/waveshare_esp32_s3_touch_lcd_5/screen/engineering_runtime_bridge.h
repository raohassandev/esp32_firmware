#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pvdg_ui_engineering.h"

#ifdef __cplusplus
extern "C" {
#endif

void engineering_runtime_bridge_refresh(pvdg_ui_engineering_state_t *state);
void engineering_runtime_bridge_submit_password(const char *password, void *user);
void engineering_runtime_bridge_logout(void *user);
bool engineering_runtime_bridge_is_authorized(void);
bool engineering_runtime_bridge_password_configured(void);

#ifdef __cplusplus
}
#endif
