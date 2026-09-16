#pragma once

#include <stdint.h>

#include "pvdg_ui_export_control.h"
#include "pvdg_ui_inverter_setup.h"
#include "pvdg_ui_meter_setup.h"
#include "pvdg_ui_source_setup.h"

#ifdef __cplusplus
extern "C" {
#endif

void engineering_config_source_request(void *user);
void engineering_config_source_submit(const pvdg_ui_source_config_t *config, void *user);
void engineering_config_meter_request(void *user);
void engineering_config_meter_submit(const pvdg_ui_meter_list_t *list, void *user);
void engineering_config_inverter_request(void *user);
void engineering_config_inverter_profiles_request(void *user);
void engineering_config_inverter_assignments_request(void *user);
void engineering_config_inverter_submit(const pvdg_ui_inverter_list_t *list, void *user);
void engineering_config_inverter_assign_profile(uint8_t inverter_index,
                                                const char *profile_id,
                                                void *user);
void engineering_config_export_request(void *user);
void engineering_config_export_submit(const pvdg_ui_source_config_t *config, void *user);

#ifdef __cplusplus
}
#endif
