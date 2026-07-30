idf_component_register(
    SRCS "control_engine.c" "source_mode.c" "power_control_policy.c" "grid_control_gate.c"
         "generator_fleet_limit.c"
    INCLUDE_DIRS "include"
    REQUIRES commissioning_gate config_manager solar_grid_config meter_manager inverter_manager safety_manager source_detection esp_timer
)
