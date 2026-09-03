idf_component_register(
    SRCS "inverter_manager.c" "inverter_write_authority_guard.c" "inverter_profiles.c" "inverter_profile_write_gate.c" "inverter_profile_store.c" "inverter_profile_decode.c" "inverter_status.c" "inverter_command_policy.c"
    INCLUDE_DIRS "include"
    REQUIRES config_manager modbus_tcp nvs_flash
)

# Keep fail-safe zero available even when status evidence is absent, but route
# every positive public fleet command through the cached operational-status
# authority guard. The core implementation remains unchanged and cannot be
# called through the public symbol from outside this component source.
set_property(SOURCE "inverter_manager.c" APPEND PROPERTY
    COMPILE_DEFINITIONS inverter_manager_set_total_power_kw=inverter_manager_set_total_power_kw_core)

# The base catalogue predicate owns the existing command/readback/approval
# checks. Route it through a second evidence-completeness gate so a future
# production approval cannot become commandable without identity, active-power
# telemetry and a fully configured read-only operational-status mapping.
set_property(SOURCE "inverter_profiles.c" APPEND PROPERTY
    COMPILE_DEFINITIONS inverter_profile_allows_write=inverter_profile_allows_write_core)
