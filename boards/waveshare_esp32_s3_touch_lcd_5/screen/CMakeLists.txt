# Waveshare ESP32-S3-Touch-LCD-5 physical display component.
#
# Reconciled onto current firmware dev from the qualified board reference.
# This component intentionally contains hardware/display code only. Legacy
# screen pages, commissioning backends and product logic are not compiled here.

idf_component_register(
    SRCS
        "drivers/waveshare_display_profile.c"
        "drivers/waveshare_display_port.c"
    INCLUDE_DIRS
        "drivers"
    REQUIRES
        lvgl
        esp_lvgl_adapter
        esp_lcd_touch_gt911
        esp_lcd
        esp_driver_i2c
        esp_driver_gpio
)
