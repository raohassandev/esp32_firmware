#include "system_resource_api.h"

#include <stdio.h>
#include <stdlib.h>

#include "cJSON.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static esp_err_t resources_get(httpd_req_t *request)
{
    esp_chip_info_t chip = {0};
    esp_chip_info(&chip);

    const size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t free_total = esp_get_free_heap_size();
    const size_t minimum_free = esp_get_minimum_free_heap_size();
    const uint64_t uptime_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);

    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);

    cJSON_AddNumberToObject(root, "uptime_ms", (double)uptime_ms);
    cJSON_AddNumberToObject(root, "cpu_cores", chip.cores);
#ifdef CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ
    cJSON_AddNumberToObject(root, "cpu_frequency_mhz", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
#else
    cJSON_AddNullToObject(root, "cpu_frequency_mhz");
#endif
    cJSON_AddNumberToObject(root, "task_count", uxTaskGetNumberOfTasks());
    cJSON_AddNumberToObject(root, "free_heap_bytes", (double)free_total);
    cJSON_AddNumberToObject(root, "minimum_free_heap_bytes", (double)minimum_free);
    cJSON_AddNumberToObject(root, "free_internal_heap_bytes", (double)free_internal);
    cJSON_AddNumberToObject(root, "largest_internal_block_bytes", (double)largest_internal);
    cJSON_AddNumberToObject(root, "flash_size_bytes", (double)chip.features);

    /* Temperature telemetry remains explicitly unavailable until the ESP-IDF
     * temperature-sensor driver is initialized by the board-support layer.
     * Do not fabricate a value or confuse Wi-Fi calibration temperature with
     * a qualified enclosure/processor measurement. */
    cJSON_AddBoolToObject(root, "temperature_available", false);
    cJSON_AddNullToObject(root, "temperature_c");
    cJSON_AddStringToObject(root, "temperature_note",
                            "Internal temperature sensor is not initialized in this build");

    const bool heap_warning = free_total < 65536U || largest_internal < 32768U;
    const bool heap_critical = free_total < 32768U || largest_internal < 16384U;
    cJSON_AddStringToObject(root, "resource_state",
                            heap_critical ? "critical" : heap_warning ? "review" : "healthy");

    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) return httpd_resp_send_500(request);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    esp_err_t err = httpd_resp_sendstr(request, text);
    free(text);
    return err;
}

esp_err_t system_resource_api_register(httpd_handle_t server)
{
    const httpd_uri_t handler = {
        .uri = "/api/system/resources",
        .method = HTTP_GET,
        .handler = resources_get,
    };
    return httpd_register_uri_handler(server, &handler);
}
