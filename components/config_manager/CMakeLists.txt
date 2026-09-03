idf_component_register(
    SRCS "config_manager.c" "config_manager_v6.c" "config_manager_core_guard.c" "config_manager_json_guard.c" "config_manager_stack_safe.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash cjson modbus_tcp
)

# Keep the long-lived core implementation intact while schema-6 wraps its
# persistence/API entry points. This gives us an explicit migration boundary
# for the repurposed endpoint alignment byte without rewriting commissioned
# state. Destructive erase is intercepted, and legacy-migration heap failure is
# prevented from falling through to defaults + NVS replacement.
set_property(SOURCE "config_manager.c" APPEND PROPERTY COMPILE_DEFINITIONS
    config_manager_init=config_manager_init_core
    config_manager_save=config_manager_save_core
    config_manager_import_json=config_manager_import_json_core
    config_manager_export_json=config_manager_export_json_core
    nvs_flash_erase=config_manager_forbidden_nvs_erase
    malloc=config_manager_core_malloc
    nvs_set_blob=config_manager_guarded_nvs_set_blob
)

# Keep the original schema-6 implementation linked as a compatibility reference,
# but move its three full-app-config stack entry points behind non-public names.
# config_manager_stack_safe.c owns the public init/import/export symbols and keeps
# every ~2.5 kB app_config_t snapshot off the main/httpd task stacks.
set_property(SOURCE "config_manager_v6.c" APPEND PROPERTY COMPILE_DEFINITIONS
    config_manager_init=config_manager_init_v6_stack_legacy
    config_manager_import_json=config_manager_import_json_v6_stack_legacy
    config_manager_export_json=config_manager_export_json_v6_stack_legacy
    cJSON_Parse=config_manager_guarded_cjson_parse
)
set_property(SOURCE "config_manager_v6.c" APPEND PROPERTY
    COMPILE_OPTIONS "-include;config_manager_json_guard.h")
