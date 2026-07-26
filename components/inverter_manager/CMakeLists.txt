idf_component_register(
    SRCS "inverter_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES config_manager modbus_tcp modbus_decoder network_manager profile_manager
)
