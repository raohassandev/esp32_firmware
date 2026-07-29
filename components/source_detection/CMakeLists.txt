idf_component_register(
    SRCS
        "source_detection_engine.c"
        "source_detection_config.c"
        "source_detection.c"
    INCLUDE_DIRS "include"
    REQUIRES config_manager meter_manager nvs_flash esp_timer
)
