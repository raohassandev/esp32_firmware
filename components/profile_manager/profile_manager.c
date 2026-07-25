#include "profile_manager.h"
#include <string.h>

esp_err_t profile_manager_init(void)
{
    return ESP_OK;
}

esp_err_t profile_manager_validate_point(const register_point_t *point)
{
    if (!point || !point->key[0]) return ESP_ERR_INVALID_ARG;
    if (point->function_code != 3 && point->function_code != 4 &&
        point->function_code != 6 && point->function_code != 16) return ESP_ERR_NOT_SUPPORTED;
    if (point->data_type > MODBUS_DATA_FLOAT32 || point->word_order > MODBUS_ORDER_DCBA) return ESP_ERR_INVALID_ARG;
    if (point->poll_interval_ms && point->poll_interval_ms < 50) return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}
