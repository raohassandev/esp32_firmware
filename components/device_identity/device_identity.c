#include "device_identity.h"

#include <stdio.h>
#include <string.h>

#include "esp_mac.h"

esp_err_t device_identity_suffix(char *out, size_t size)
{
    if (!out || size < DEVICE_IDENTITY_SUFFIX_SIZE) return ESP_ERR_INVALID_ARG;

    uint8_t mac[6] = {0};
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) return err;

    snprintf(out, size, "%02X%02X%02X", mac[3], mac[4], mac[5]);
    return ESP_OK;
}

esp_err_t device_identity_hostname(char *out, size_t size)
{
    if (!out || size < DEVICE_IDENTITY_HOSTNAME_SIZE) return ESP_ERR_INVALID_ARG;

    char suffix[DEVICE_IDENTITY_SUFFIX_SIZE] = {0};
    esp_err_t err = device_identity_suffix(suffix, sizeof(suffix));
    if (err != ESP_OK) return err;

    /* Lowercase: DNS comparison is case insensitive, but some resolvers and
     * printed documentation are not, so one spelling is published. */
    for (size_t n = 0; suffix[n]; ++n) {
        if (suffix[n] >= 'A' && suffix[n] <= 'Z') suffix[n] = (char)(suffix[n] - 'A' + 'a');
    }

    snprintf(out, size, "automatrix-%s", suffix);
    return ESP_OK;
}
