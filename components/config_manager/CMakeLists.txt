idf_component_register(
    SRCS "config_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash cjson modbus_tcp esp_hw_support device_identity
)
