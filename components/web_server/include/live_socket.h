#pragma once
#include "esp_err.h"
#include "esp_http_server.h"

/* Registers /ws and starts the task that pushes the live frame once a second.
 * See live_socket.c for why this exists: one browser tab holds ten of the
 * controller's ten client sockets when it polls, and reducing the number of
 * polled endpoints does not change that. */
esp_err_t live_socket_register(httpd_handle_t server);
