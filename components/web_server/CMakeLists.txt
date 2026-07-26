configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/index.html" "${CMAKE_CURRENT_BINARY_DIR}/index.html" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/app.css" "${CMAKE_CURRENT_BINARY_DIR}/app.css" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/wifi.css" "${CMAKE_CURRENT_BINARY_DIR}/wifi.css" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/devices.css" "${CMAKE_CURRENT_BINARY_DIR}/devices.css" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/inverter-telemetry.css" "${CMAKE_CURRENT_BINARY_DIR}/inverter-telemetry.css" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/app.js" "${CMAKE_CURRENT_BINARY_DIR}/app.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/wifi-utils.js" "${CMAKE_CURRENT_BINARY_DIR}/wifi-utils.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/wifi-guard.js" "${CMAKE_CURRENT_BINARY_DIR}/wifi-guard.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/wifi.js" "${CMAKE_CURRENT_BINARY_DIR}/wifi.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/devices-utils.js" "${CMAKE_CURRENT_BINARY_DIR}/devices-utils.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/devices.js" "${CMAKE_CURRENT_BINARY_DIR}/devices.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/inverter-telemetry-utils.js" "${CMAKE_CURRENT_BINARY_DIR}/inverter-telemetry-utils.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/inverter-telemetry.js" "${CMAKE_CURRENT_BINARY_DIR}/inverter-telemetry.js" COPYONLY)

idf_component_register(
    SRCS "web_server.c" "web_api.c" "device_api.c" "telemetry_profile_api.c" "web_assets.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server esp_system esp_timer cjson config_manager network_manager meter_manager inverter_manager profile_manager control_engine safety_manager
    EMBED_TXTFILES
        "${CMAKE_CURRENT_BINARY_DIR}/index.html"
        "${CMAKE_CURRENT_BINARY_DIR}/app.css"
        "${CMAKE_CURRENT_BINARY_DIR}/wifi.css"
        "${CMAKE_CURRENT_BINARY_DIR}/devices.css"
        "${CMAKE_CURRENT_BINARY_DIR}/inverter-telemetry.css"
        "${CMAKE_CURRENT_BINARY_DIR}/app.js"
        "${CMAKE_CURRENT_BINARY_DIR}/wifi-utils.js"
        "${CMAKE_CURRENT_BINARY_DIR}/wifi-guard.js"
        "${CMAKE_CURRENT_BINARY_DIR}/wifi.js"
        "${CMAKE_CURRENT_BINARY_DIR}/devices-utils.js"
        "${CMAKE_CURRENT_BINARY_DIR}/devices.js"
        "${CMAKE_CURRENT_BINARY_DIR}/inverter-telemetry-utils.js"
        "${CMAKE_CURRENT_BINARY_DIR}/inverter-telemetry.js"
)
