#pragma once

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

cJSON *config_manager_guarded_cjson_parse(const char *text);

#ifdef __cplusplus
}
#endif
