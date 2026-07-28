#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ESP-IDF station/AP SSID and password arrays are fixed-width byte fields.
 * A legal 32-byte SSID or 64-byte PSK occupies the complete destination and is
 * not NUL terminated. All shorter/general string destinations retain normal
 * strlcpy behavior. */
size_t network_manager_wifi_strlcpy(char *destination,
                                    const char *source,
                                    size_t destination_size);

#ifdef __cplusplus
}
#endif
