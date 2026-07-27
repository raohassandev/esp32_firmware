idf_component_register(
    SRCS "inverter_manager.c" "inverter_profiles.c"
    INCLUDE_DIRS "include"
    REQUIRES config_manager modbus_tcp
)
