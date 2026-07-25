configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/index.html" "${CMAKE_CURRENT_BINARY_DIR}/index.html" COPYONLY)

idf_component_register(
    SRCS "web_server.c" "web_api.c" "web_assets.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server cjson config_manager network_manager meter_manager inverter_manager control_engine safety_manager
    EMBED_TXTFILES "${CMAKE_CURRENT_BINARY_DIR}/index.html"
)
