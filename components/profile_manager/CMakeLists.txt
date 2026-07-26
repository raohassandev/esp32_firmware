idf_component_register(
    SRCS "profile_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES modbus_tcp nvs_flash
)
