#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t system_resource_api_register(httpd_handle_t server);

/*
 * THE CONTROLLER'S OWN HEALTH, IN THE FORM AN OPERATOR CAN USE.
 *
 * The full resource report is engineering detail -- chip revision, partition
 * table, fragmentation ratios, heap block sizes -- and it is gated accordingly.
 * But three facts out of it are exactly what the person who owns the plant needs
 * and reveal nothing: how long it has been running, whether the last restart was
 * a crash, and one word for whether its memory is healthy.
 *
 * That was the whole point of asking for these on screen: a small factory cannot
 * read a heap fragmentation ratio, and does not need to. It needs to know the
 * controller has been up for eleven days and did not fall over in the night.
 *
 * Shared rather than recomputed, so the operator's word and the engineer's
 * report can never disagree about the same controller.
 */
typedef struct {
    uint64_t uptime_ms;
    /* "healthy", "review" or "critical". */
    const char *state;
    /* True when the last restart was a panic, a watchdog or a brownout rather
     * than a power cycle or a commanded reboot. This is the one that matters:
     * a controller that restarted by itself did so for a reason. */
    bool last_reboot_unexpected;
} system_resource_health_t;

system_resource_health_t system_resource_health(void);
