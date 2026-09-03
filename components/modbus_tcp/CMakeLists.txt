idf_component_register(
    SRCS "modbus_tcp.c" "modbus_connection_policy.c" "modbus_decoder.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer
)
