#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "pvdg_ui_model.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { bool authenticated; bool setup_required; bool password_change_recommended; bool locked_out; bool write_contract_verified; uint32_t session_remaining_seconds; uint32_t lockout_remaining_seconds; uint16_t session_timeout_minutes; const char *security_state; const char *message; } pvdg_ui_engineering_state_t;
typedef enum { PVDG_UI_ENGINEERING_SOURCE_SETUP=0,PVDG_UI_ENGINEERING_METER_SETUP,PVDG_UI_ENGINEERING_INVERTER_SETUP,PVDG_UI_ENGINEERING_EXPORT_CONTROL,PVDG_UI_ENGINEERING_SAFETY_LIMITS,PVDG_UI_ENGINEERING_NETWORK,PVDG_UI_ENGINEERING_DIAGNOSTICS,PVDG_UI_ENGINEERING_CONFIG_MANAGEMENT } pvdg_ui_engineering_section_t;
typedef struct { void (*request_session_refresh)(void *user); void (*submit_password)(const char *password,void *user); void (*request_logout)(void *user); void (*change_password)(const char *current_password,const char *new_password,void *user); void (*open_section)(pvdg_ui_engineering_section_t section,void *user); void *user; } pvdg_ui_engineering_callbacks_t;
lv_obj_t *pvdg_ui_engineering_create(lv_obj_t *parent,const pvdg_ui_engineering_callbacks_t *callbacks); void pvdg_ui_engineering_apply(const pvdg_ui_model_t *model,const pvdg_ui_engineering_state_t *state); void pvdg_ui_engineering_set_action_state(bool busy,const char *message);
#ifdef __cplusplus
}
#endif
