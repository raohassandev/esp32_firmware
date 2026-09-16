#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "pvdg_ui_inverter_config.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct{bool config_available;bool profiles_available;bool assignments_available;bool write_allowed;bool busy;bool restart_required;const char*message;}pvdg_ui_inverter_setup_state_t;typedef struct{void(*request_config)(void*user);void(*request_profiles)(void*user);void(*request_assignments)(void*user);void(*submit_config)(const pvdg_ui_inverter_list_t*list,void*user);void(*assign_profile)(uint8_t inverter_index,const char*profile_id,void*user);void*user;}pvdg_ui_inverter_setup_callbacks_t;lv_obj_t*pvdg_ui_inverter_setup_create(lv_obj_t*parent,const pvdg_ui_inverter_setup_callbacks_t*callbacks);void pvdg_ui_inverter_setup_set_config(const pvdg_ui_inverter_list_t*list);void pvdg_ui_inverter_setup_set_profiles(const pvdg_ui_inverter_profile_catalog_t*catalog);void pvdg_ui_inverter_setup_set_assignments(const pvdg_ui_inverter_assignment_map_t*assignments);void pvdg_ui_inverter_setup_apply_state(const pvdg_ui_inverter_setup_state_t*state);
#ifdef __cplusplus
}
#endif
