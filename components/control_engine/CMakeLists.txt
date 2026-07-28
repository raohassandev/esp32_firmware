idf_component_register(
    SRCS "control_engine.c" "source_mode.c"
    INCLUDE_DIRS "include"
    REQUIRES config_manager meter_manager inverter_manager safety_manager esp_timer
)
