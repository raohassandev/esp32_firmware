idf_component_register(
    SRCS "meter_manager.c" "meter_poll_schedule.c"
    INCLUDE_DIRS "include"
    REQUIRES config_manager meter_profiles modbus_tcp esp_timer network_manager
    PRIV_REQUIRES lwip
)
