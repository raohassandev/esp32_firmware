#pragma once
#include <stdbool.h>
#include "esp_err.h"

esp_err_t network_manager_init(void);
bool network_manager_is_connected(void);
const char *network_manager_get_ip(void);
