idf_component_register(
    SRCS
        "meter_profiles.c"
    INCLUDE_DIRS "include"
    REQUIRES config_manager modbus_tcp
)
