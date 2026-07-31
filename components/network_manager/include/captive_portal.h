#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Answers every A query arriving on the recovery access point with the
 * controller's own address, so a phone that joins the setup network opens the
 * setup page instead of an access point that appears to do nothing.
 *
 * portal_address_network_order is the AP netif address, in network byte order.
 * The listener binds to THAT address and never to INADDR_ANY: on INADDR_ANY it
 * would also answer DNS for the site LAN and take that network down, and the
 * fault would look like the router rather than like this controller.
 *
 * Starting twice is a no-op. There is no upstream, no cache and no recursion --
 * a device on the recovery AP has no internet through this controller and is not
 * told otherwise.
 */
esp_err_t captive_portal_start(uint32_t portal_address_network_order);

void captive_portal_stop(void);

#ifdef __cplusplus
}
#endif
