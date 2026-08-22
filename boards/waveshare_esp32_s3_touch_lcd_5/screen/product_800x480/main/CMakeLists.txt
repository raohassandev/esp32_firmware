idf_component_register(
    SRCS
        "main.c"
        "local_backend_provider.c"
        "local_commissioning_backend.c"
        "local_source_commissioning_backend.c"
        "local_persistence_worker.c"
    INCLUDE_DIRS "."
    REQUIRES
        app_core
        screen
        config_manager
        control_engine
        inverter_manager
        meter_manager
        network_manager
        safety_manager
        source_detection
        solar_grid_config
        web_server
        cjson
        esp_app_format
        esp_lvgl_adapter
        esp_lcd_touch_gt911
        esp_lcd
        esp_timer
        esp_system
        esp_driver_gpio
)

# Commissioning callbacks run on the LVGL adapter task, whose stack is
# intentionally in PSRAM. ESP32-S3 PSRAM is inaccessible while flash cache is
# disabled, so persistent writes are compile-time redirected to the dedicated
# internal-DRAM worker without changing the commissioning source logic itself.
#
# Keep -include and its operand as one SHELL option group. esp_lvgl_adapter also
# supplies a forced include for its alignment header; plain adjacent CMake
# options are de-duplicated and can collapse the two -include switches into one,
# leaving the second header to GCC as an accidental extra source file.
target_compile_options(${COMPONENT_LIB} PRIVATE
    "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/local_persistence_redirect.h"
)
