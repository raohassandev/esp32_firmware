idf_component_register(
    SRCS "app_core.c"
    INCLUDE_DIRS "include"
    REQUIRES config_manager network_manager profile_manager meter_manager inverter_manager safety_manager control_engine web_server
)
