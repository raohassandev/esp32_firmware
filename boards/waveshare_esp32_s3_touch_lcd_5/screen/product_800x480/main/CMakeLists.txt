idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        app_core
        ota_manager
        screen
        esp_system
        esp_timer
)
