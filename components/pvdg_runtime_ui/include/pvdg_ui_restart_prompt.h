#pragma once
#include <stdbool.h>
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct{void(*request_restart)(void*user);void(*request_refresh)(void*user);void*user;}pvdg_ui_restart_prompt_callbacks_t;typedef struct{bool busy;bool controller_online;const char*message;}pvdg_ui_restart_prompt_state_t;lv_obj_t*pvdg_ui_restart_prompt_create(const pvdg_ui_restart_prompt_callbacks_t*callbacks);void pvdg_ui_restart_prompt_show(const char*change_summary);void pvdg_ui_restart_prompt_hide(void);void pvdg_ui_restart_prompt_apply_state(const pvdg_ui_restart_prompt_state_t*state);
#ifdef __cplusplus
}
#endif
