idf_component_register(
    SRCS "network_manager.c" "network_scan.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_event esp_netif config_manager
)
