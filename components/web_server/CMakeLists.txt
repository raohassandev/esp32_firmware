configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/index.html" "${CMAKE_CURRENT_BINARY_DIR}/index.html" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/app.css" "${CMAKE_CURRENT_BINARY_DIR}/app.css" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/theme.css" "${CMAKE_CURRENT_BINARY_DIR}/theme.css" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/product-mode.css" "${CMAKE_CURRENT_BINARY_DIR}/product-mode.css" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/wifi.css" "${CMAKE_CURRENT_BINARY_DIR}/wifi.css" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/devices.css" "${CMAKE_CURRENT_BINARY_DIR}/devices.css" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/em500.css" "${CMAKE_CURRENT_BINARY_DIR}/em500.css" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/app.js" "${CMAKE_CURRENT_BINARY_DIR}/app.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/theme.js" "${CMAKE_CURRENT_BINARY_DIR}/theme.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/product-mode.js" "${CMAKE_CURRENT_BINARY_DIR}/product-mode.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/operator-view.js" "${CMAKE_CURRENT_BINARY_DIR}/operator-view.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/engineering-errors.js" "${CMAKE_CURRENT_BINARY_DIR}/engineering-errors.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/ui-enhancements.js" "${CMAKE_CURRENT_BINARY_DIR}/ui-enhancements.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/wifi-utils.js" "${CMAKE_CURRENT_BINARY_DIR}/wifi-utils.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/wifi-guard.js" "${CMAKE_CURRENT_BINARY_DIR}/wifi-guard.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/wifi.js" "${CMAKE_CURRENT_BINARY_DIR}/wifi.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/devices-utils.js" "${CMAKE_CURRENT_BINARY_DIR}/devices-utils.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/devices.js" "${CMAKE_CURRENT_BINARY_DIR}/devices.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/devices-refresh.js" "${CMAKE_CURRENT_BINARY_DIR}/devices-refresh.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/inverter-profiles.js" "${CMAKE_CURRENT_BINARY_DIR}/inverter-profiles.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/inverter-config.js" "${CMAKE_CURRENT_BINARY_DIR}/inverter-config.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/inverter-telemetry.js" "${CMAKE_CURRENT_BINARY_DIR}/inverter-telemetry.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/em500-utils.js" "${CMAKE_CURRENT_BINARY_DIR}/em500-utils.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/em500-core.js" "${CMAKE_CURRENT_BINARY_DIR}/em500-core.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/em500-profiles.js" "${CMAKE_CURRENT_BINARY_DIR}/em500-profiles.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/em500-plan.js" "${CMAKE_CURRENT_BINARY_DIR}/em500-plan.js" COPYONLY)

set(WEB_SERVER_C_SOURCES
    "web_server.c"
    "web_api.c"
    "device_api.c"
    "engineering_auth.c"
    "engineering_guard.c"
    "inverter_profile_api.c"
    "inverter_config_api.c"
    "meter_config_api.c"
    "em500_api.c"
    "em500_history_api.c"
    "em500_settings_api.c"
    "em500_settings_plan_api.c"
    "web_assets.c"
)

idf_component_register(
    SRCS ${WEB_SERVER_C_SOURCES}
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server esp_system esp_timer esp_hw_support cjson nvs_flash mbedtls config_manager network_manager meter_manager modbus_tcp inverter_manager control_engine safety_manager
    EMBED_TXTFILES
        "${CMAKE_CURRENT_BINARY_DIR}/index.html"
        "${CMAKE_CURRENT_BINARY_DIR}/app.css"
        "${CMAKE_CURRENT_BINARY_DIR}/theme.css"
        "${CMAKE_CURRENT_BINARY_DIR}/product-mode.css"
        "${CMAKE_CURRENT_BINARY_DIR}/wifi.css"
        "${CMAKE_CURRENT_BINARY_DIR}/devices.css"
        "${CMAKE_CURRENT_BINARY_DIR}/em500.css"
        "${CMAKE_CURRENT_BINARY_DIR}/app.js"
        "${CMAKE_CURRENT_BINARY_DIR}/theme.js"
        "${CMAKE_CURRENT_BINARY_DIR}/product-mode.js"
        "${CMAKE_CURRENT_BINARY_DIR}/operator-view.js"
        "${CMAKE_CURRENT_BINARY_DIR}/engineering-errors.js"
        "${CMAKE_CURRENT_BINARY_DIR}/ui-enhancements.js"
        "${CMAKE_CURRENT_BINARY_DIR}/wifi-utils.js"
        "${CMAKE_CURRENT_BINARY_DIR}/wifi-guard.js"
        "${CMAKE_CURRENT_BINARY_DIR}/wifi.js"
        "${CMAKE_CURRENT_BINARY_DIR}/devices-utils.js"
        "${CMAKE_CURRENT_BINARY_DIR}/devices.js"
        "${CMAKE_CURRENT_BINARY_DIR}/devices-refresh.js"
        "${CMAKE_CURRENT_BINARY_DIR}/inverter-profiles.js"
        "${CMAKE_CURRENT_BINARY_DIR}/inverter-config.js"
        "${CMAKE_CURRENT_BINARY_DIR}/inverter-telemetry.js"
        "${CMAKE_CURRENT_BINARY_DIR}/em500-utils.js"
        "${CMAKE_CURRENT_BINARY_DIR}/em500-core.js"
        "${CMAKE_CURRENT_BINARY_DIR}/em500-profiles.js"
        "${CMAKE_CURRENT_BINARY_DIR}/em500-plan.js"
)

# Apply the registration gateway only to C translation units. Embedded web
# assets are compiled through generated assembler files and must never receive
# C/FreeRTOS headers through a component-wide forced include.
foreach(source IN LISTS WEB_SERVER_C_SOURCES)
    if(NOT source STREQUAL "engineering_guard.c")
        set_source_files_properties(${source} PROPERTIES COMPILE_OPTIONS "-include;engineering_auth.h")
    endif()
endforeach()
set_source_files_properties("engineering_guard.c" PROPERTIES
    COMPILE_DEFINITIONS ENGINEERING_GUARD_IMPLEMENTATION=1)
