#pragma once

#include <stddef.h>
#include "esp_err.h"

/* Name-based discovery for a controller whose IP address nobody knows.
 *
 * The unit is moved between sites. Each site's DHCP server hands it a different
 * address, and there is no console on the enclosure, so an address is not
 * something an engineer can be expected to have. mDNS replaces it with a name
 * that does not change: http://<hostname>.local.
 *
 * The responder is bound to the predefined station and soft-AP interfaces, so
 * the same name resolves whether the engineer is on the site network or joined
 * to the controller's own recovery AP. */

/* hostname must be a bare DNS label (no ".local" suffix, no dots): the
 * responder appends the domain itself. */
esp_err_t network_mdns_start(const char *hostname, const char *instance_name);

/* Bare label this unit publishes, or an empty string before the responder has
 * started. Safe to call from any task; the buffer is written under no lock
 * because it is set once during initialization and only read afterwards. */
const char *network_mdns_hostname(void);
