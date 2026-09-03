idf_component_register(
    SRCS "modbus_tcp.c" "modbus_endpoint_guard.c" "modbus_connection_policy.c" "modbus_decoder.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer
)

# The legacy transport still contains a hostname/getaddrinfo compatibility path,
# but synchronous DNS is not covered by the transaction deadline. Route every
# public connection initialization through modbus_endpoint_guard.c, which admits
# literal IPv4 endpoints only; the core transport can therefore never enter the
# blocking DNS path from a valid initialized connection.
set_property(SOURCE "modbus_tcp.c" APPEND PROPERTY
    COMPILE_DEFINITIONS modbus_tcp_connection_init=modbus_tcp_connection_init_core)
