idf_component_register(
    SRCS "config_manager.c" "config_manager_v6.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash cjson modbus_tcp
)

# Keep the long-lived core implementation intact while schema-6 wraps its
# persistence/API entry points.  This gives us an explicit migration boundary
# for the repurposed endpoint alignment byte without rewriting commissioned
# state.  The NVS erase symbol is also intercepted so a recoverable NVS error
# can fail closed instead of erasing commissioned configuration.
set_property(SOURCE "config_manager.c" APPEND PROPERTY COMPILE_DEFINITIONS
    config_manager_init=config_manager_init_core
    config_manager_save=config_manager_save_core
    config_manager_import_json=config_manager_import_json_core
    config_manager_export_json=config_manager_export_json_core
    nvs_flash_erase=config_manager_forbidden_nvs_erase
)
