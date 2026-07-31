idf_component_register(
    SRCS "captive_portal.c" "network_manager.c" "network_scan.c" "network_wifi_copy.c" "network_mdns.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif esp_timer config_manager device_identity
    PRIV_REQUIRES espressif__mdns
)

# Route only network_manager.c string copies through the fixed-width-aware
# helper. Other component sources retain the normal platform strlcpy.
set_property(SOURCE "network_manager.c" APPEND PROPERTY
    COMPILE_DEFINITIONS strlcpy=network_manager_wifi_strlcpy)
set_property(SOURCE "network_manager.c" APPEND PROPERTY
    COMPILE_OPTIONS "-include;network_wifi_copy.h")
