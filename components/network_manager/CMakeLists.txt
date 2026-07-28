idf_component_register(
    SRCS "network_manager.c" "network_scan.c" "network_wifi_copy.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_event esp_netif esp_timer config_manager
)

/* Route only network_manager.c string copies through the fixed-width-aware
 * helper. Other component sources retain the normal platform strlcpy. */
set_property(SOURCE "network_manager.c" APPEND PROPERTY
    COMPILE_DEFINITIONS strlcpy=network_manager_wifi_strlcpy)
set_property(SOURCE "network_manager.c" APPEND PROPERTY
    COMPILE_OPTIONS "-include;network_wifi_copy.h")
