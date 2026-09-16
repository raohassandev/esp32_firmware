#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "pvdg_ui_model.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Deterministic V1 core snapshot codec for host fixtures/replay. Extended inverter/energy tables are not silently reconstructed. */
size_t pvdg_ui_snapshot_export_core(char *buffer,size_t buffer_size,const pvdg_ui_model_t *model);
bool pvdg_ui_snapshot_import_core(const char *text,pvdg_ui_model_t *model);
#ifdef __cplusplus
}
#endif
