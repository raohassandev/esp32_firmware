#include "network_wifi_copy.h"

#include <string.h>

size_t network_manager_wifi_strlcpy(char *destination,
                                    const char *source,
                                    size_t destination_size)
{
    if (!destination || !source) return 0U;
    const size_t source_length = strlen(source);
    if (destination_size == 0U) return source_length;

    /* These are the exact ESP-IDF fixed-width Wi-Fi byte-array sizes. A source
     * of exactly the same legal length must not lose its final byte merely to
     * create a C-string terminator that the driver structure does not require. */
    if ((destination_size == 32U || destination_size == 64U) &&
        source_length == destination_size) {
        memcpy(destination, source, destination_size);
        return source_length;
    }

    size_t copied = source_length < destination_size - 1U
                        ? source_length
                        : destination_size - 1U;
    memcpy(destination, source, copied);
    destination[copied] = '\0';
    return source_length;
}
