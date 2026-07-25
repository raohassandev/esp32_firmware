idf_component_register(
    SRCS "safety_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES config_manager meter_manager
)
