idf_component_register(
    SRCS
        "meter_profiles.c"
        "em500_block.c"
    INCLUDE_DIRS "include"
    REQUIRES config_manager modbus_tcp
)
