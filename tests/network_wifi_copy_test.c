#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "network_wifi_copy.h"

int main(void)
{
    const char ssid32[] = "12345678901234567890123456789012";
    const char psk64[] =
        "1234567890123456789012345678901234567890123456789012345678901234";
    char ssid_field[32];
    char psk_field[64];
    memset(ssid_field, 0xA5, sizeof(ssid_field));
    memset(psk_field, 0xA5, sizeof(psk_field));

    assert(strlen(ssid32) == sizeof(ssid_field));
    assert(strlen(psk64) == sizeof(psk_field));
    assert(network_manager_wifi_strlcpy(ssid_field, ssid32,
                                        sizeof(ssid_field)) == sizeof(ssid_field));
    assert(network_manager_wifi_strlcpy(psk_field, psk64,
                                        sizeof(psk_field)) == sizeof(psk_field));
    assert(memcmp(ssid_field, ssid32, sizeof(ssid_field)) == 0);
    assert(memcmp(psk_field, psk64, sizeof(psk_field)) == 0);

    char ordinary[8];
    assert(network_manager_wifi_strlcpy(ordinary, "network", sizeof(ordinary)) == 7U);
    assert(strcmp(ordinary, "network") == 0);
    assert(network_manager_wifi_strlcpy(ordinary, "too-long-name", sizeof(ordinary)) == 13U);
    assert(strcmp(ordinary, "too-lon") == 0);

    puts("maximum-length Wi-Fi field copy tests passed");
    return 0;
}
