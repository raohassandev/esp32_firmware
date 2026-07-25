idf_component_register(
    SRCS "meter_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES config_manager modbus_tcp esp_timer network_manager
)
