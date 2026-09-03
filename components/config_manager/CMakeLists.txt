idf_component_register(
    SRCS "config_manager.c" "config_manager_v6.c" "config_manager_core_guard.c" "meter_scale_migration.c"
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

# The schema-6 initializer is kept byte-for-byte responsible for historical
# schema/NVS handling. A thin public wrapper then performs one explicit,
# persisted migration of the old EM500 scale compatibility fingerprint before
# any meter task can consume configuration. This retires the silent runtime
# scale substitution without changing the effective behavior of an existing
# commissioned controller.
set_property(SOURCE "config_manager_v6.c" APPEND PROPERTY COMPILE_DEFINITIONS
    config_manager_init=config_manager_init_schema6
)
