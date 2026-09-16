#pragma once
#include <stdbool.h>
#include "lvgl.h"
#include "pvdg_ui_meter_config.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct{bool config_available;bool write_allowed;bool busy;bool restart_required;const char*message;}pvdg_ui_meter_setup_state_t;typedef struct{void(*request_list)(void*user);void(*submit_list)(const pvdg_ui_meter_list_t*list,void*user);void*user;}pvdg_ui_meter_setup_callbacks_t;lv_obj_t*pvdg_ui_meter_setup_create(lv_obj_t*parent,const pvdg_ui_meter_setup_callbacks_t*callbacks);void pvdg_ui_meter_setup_set_list(const pvdg_ui_meter_list_t*list);void pvdg_ui_meter_setup_apply_state(const pvdg_ui_meter_setup_state_t*state);
#ifdef __cplusplus
}
#endif
