#include "network_mdns.h"

#include <string.h>

#include "esp_log.h"
#include "mdns.h"

static const char *TAG = "wifi_mdns";

/* Written once during initialization, read-only thereafter. */
static char s_hostname[64];

esp_err_t network_mdns_start(const char *hostname, const char *instance_name)
{
    if (!hostname || !hostname[0] || strchr(hostname, '.')) return ESP_ERR_INVALID_ARG;
    if (strlen(hostname) >= sizeof(s_hostname)) return ESP_ERR_INVALID_SIZE;

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Responder init failed: %s; the unit is reachable by address only",
                 esp_err_to_name(err));
        return err;
    }

    err = mdns_hostname_set(hostname);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Host name could not be published: %s", esp_err_to_name(err));
        mdns_free();
        return err;
    }

    if (instance_name && instance_name[0]) {
        esp_err_t instance_err = mdns_instance_name_set(instance_name);
        if (instance_err != ESP_OK) {
            /* Cosmetic only - the instance name is what a browser shows in a
             * service list. Losing it does not affect resolution. */
            ESP_LOGW(TAG, "Service instance name could not be set: %s",
                     esp_err_to_name(instance_err));
        }
    }

    /* The _http._tcp record is what makes the controller show up in a network
     * scan rather than only answering a name the engineer already knows. */
    mdns_txt_item_t txt[] = {
        {"product", "automatrix-pvdg"},
        {"path", "/"},
    };
    err = mdns_service_add(NULL, "_http", "_tcp", 80, txt, sizeof(txt) / sizeof(txt[0]));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP service record could not be published: %s; "
                      "the host name still resolves", esp_err_to_name(err));
    }

    strlcpy(s_hostname, hostname, sizeof(s_hostname));
    ESP_LOGI(TAG, "Discoverable as %s.local on the station and the recovery AP", s_hostname);
    return ESP_OK;
}

const char *network_mdns_hostname(void)
{
    return s_hostname;
}
