#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Per-unit identity derived from the factory Wi-Fi MAC.
 *
 * Two controllers are routinely commissioned onto one network, so every name
 * this unit publishes - its DHCP/mDNS hostname and its recovery access point -
 * carries a suffix taken from the last three bytes of the station MAC. The MAC
 * is unique per device and is already broadcast in every frame, so the suffix
 * discloses nothing that a passive listener does not already have.
 *
 * The recovery passphrase is the opposite: it is a real credential, so it is
 * deliberately NOT generated here. config_manager owns it and draws it from the
 * hardware random number generator; nothing in this component may be used to
 * derive a secret, because everything here is computable from a MAC address
 * that is broadcast in every frame. */

/* Six uppercase hex characters, e.g. "A1B2C3". */
#define DEVICE_IDENTITY_SUFFIX_LENGTH 6U
#define DEVICE_IDENTITY_SUFFIX_SIZE (DEVICE_IDENTITY_SUFFIX_LENGTH + 1U)

/* "automatrix-a1b2c3" plus terminator, with room for a longer base label. */
#define DEVICE_IDENTITY_HOSTNAME_SIZE 32U

/* Shortest passphrase WPA2-PSK accepts. Anything below this would force the
 * access point open, so it is treated as "no passphrase at all". */
#define DEVICE_IDENTITY_MIN_PASSPHRASE_LENGTH 8U

/* Writes the uppercase MAC suffix. Requires DEVICE_IDENTITY_SUFFIX_SIZE bytes. */
esp_err_t device_identity_suffix(char *out, size_t size);

/* Writes the lowercase DNS label this unit answers to, e.g.
 * "automatrix-a1b2c3". Only letters, digits and hyphens are produced, so the
 * result is a legal DNS label and a legal DHCP host name - unlike a free-text
 * device name, which may contain spaces. */
esp_err_t device_identity_hostname(char *out, size_t size);

#ifdef __cplusplus
}
#endif
