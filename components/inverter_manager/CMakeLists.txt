idf_component_register(
    SRCS "inverter_manager.c" "inverter_profiles.c" "inverter_profile_store.c"
    INCLUDE_DIRS "include"
    REQUIRES config_manager modbus_tcp nvs_flash
)
