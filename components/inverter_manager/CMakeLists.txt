idf_component_register(
    SRCS "inverter_manager.c" "inverter_profiles.c" "inverter_profile_store.c" "inverter_profile_decode.c" "inverter_status.c" "inverter_write_confirmation.c"
    INCLUDE_DIRS "include"
    REQUIRES config_manager modbus_tcp nvs_flash
)
