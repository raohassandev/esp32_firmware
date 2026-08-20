idf_component_register(
    SRCS
        "main.c"
        "local_backend_provider.c"
    INCLUDE_DIRS "."
    REQUIRES
        app_core
        screen
        esp_http_client
        esp_netif
        esp_lvgl_adapter
        esp_lcd_touch_gt911
        esp_lcd
        esp_timer
        esp_driver_gpio
)
