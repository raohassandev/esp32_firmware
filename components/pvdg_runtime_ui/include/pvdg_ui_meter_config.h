#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PVDG_UI_MAX_METERS 4U
#define PVDG_UI_METER_NAME_BYTES 24U
#define PVDG_UI_MODBUS_HOST_BYTES 64U
#define PVDG_UI_METER_GENERATOR_NONE 0xFFU

typedef enum {
    PVDG_UI_METER_ROLE_UNASSIGNED = 0,
    PVDG_UI_METER_ROLE_GRID = 1,
    PVDG_UI_METER_ROLE_GENERATOR = 2,
    PVDG_UI_METER_ROLE_LOAD = 3,
    PVDG_UI_METER_ROLE_PV = 4
} pvdg_ui_meter_role_t;

typedef enum {
    PVDG_UI_MODBUS_UINT16 = 0,
    PVDG_UI_MODBUS_INT16,
    PVDG_UI_MODBUS_UINT32,
    PVDG_UI_MODBUS_INT32,
    PVDG_UI_MODBUS_FLOAT32
} pvdg_ui_meter_data_type_t;

typedef enum {
    PVDG_UI_ORDER_ABCD = 0,
    PVDG_UI_ORDER_CDAB,
    PVDG_UI_ORDER_BADC,
    PVDG_UI_ORDER_DCBA
} pvdg_ui_meter_word_order_t;

typedef struct {
    bool enabled;
    char name[PVDG_UI_METER_NAME_BYTES];
    char host[PVDG_UI_MODBUS_HOST_BYTES];
    uint16_t port;
    uint8_t unit_id;
    uint32_t timeout_ms;
    uint8_t function_code;
    uint16_t active_power_address;
    pvdg_ui_meter_data_type_t data_type;
    pvdg_ui_meter_word_order_t word_order;
    double scale;
    uint32_t poll_ms;
    pvdg_ui_meter_role_t role;
    uint8_t generator_index;
} pvdg_ui_meter_config_t;

typedef struct {
    uint8_t count;
    pvdg_ui_meter_config_t meters[PVDG_UI_MAX_METERS];
} pvdg_ui_meter_list_t;

typedef struct {
    uint8_t grid_count;
    uint8_t generator_count[3];
    bool duplicate_generator;
    bool valid;
} pvdg_ui_meter_role_state_t;

typedef enum {
    PVDG_UI_METER_CONFIG_OK = 0,
    PVDG_UI_METER_CONFIG_INVALID_ARGUMENT,
    PVDG_UI_METER_CONFIG_COUNT_INVALID,
    PVDG_UI_METER_CONFIG_NAME_INVALID,
    PVDG_UI_METER_CONFIG_ENDPOINT_INVALID,
    PVDG_UI_METER_CONFIG_ROLE_INVALID,
    PVDG_UI_METER_CONFIG_REGISTER_INVALID,
    PVDG_UI_METER_CONFIG_SCALE_INVALID,
    PVDG_UI_METER_CONFIG_TIMING_INVALID,
    PVDG_UI_METER_CONFIG_DUPLICATE_ENDPOINT
} pvdg_ui_meter_config_result_t;

void pvdg_ui_meter_defaults(pvdg_ui_meter_config_t *meter, uint8_t index);
pvdg_ui_meter_config_result_t pvdg_ui_meter_list_validate(const pvdg_ui_meter_list_t *list, char *error, size_t error_size);
pvdg_ui_meter_role_state_t pvdg_ui_meter_role_state(const pvdg_ui_meter_list_t *list);

#ifdef __cplusplus
}
#endif
