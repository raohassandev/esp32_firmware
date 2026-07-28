#include "system_resource_api.h"

#include <stdio.h>
#include <stdlib.h>

#include "cJSON.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_POWERON: return "power_on";
        case ESP_RST_EXT: return "external";
        case ESP_RST_SW: return "software";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: return "interrupt_watchdog";
        case ESP_RST_TASK_WDT: return "task_watchdog";
        case ESP_RST_WDT: return "watchdog";
        case ESP_RST_DEEPSLEEP: return "deep_sleep";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_SDIO: return "sdio";
        case ESP_RST_USB: return "usb";
        case ESP_RST_JTAG: return "jtag";
        case ESP_RST_EFUSE: return "efuse";
        case ESP_RST_PWR_GLITCH: return "power_glitch";
        case ESP_RST_CPU_LOCKUP: return "cpu_lockup";
        case ESP_RST_UNKNOWN:
        default: return "unknown";
    }
}

static esp_err_t resources_get(httpd_req_t *request)
{
    esp_chip_info_t chip = {0};
    esp_chip_info(&chip);

    const size_t total_internal = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t minimum_internal = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t free_total = esp_get_free_heap_size();
    const size_t minimum_free = esp_get_minimum_free_heap_size();
    const uint64_t uptime_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);

    uint32_t flash_size = 0;
    const bool flash_size_available = esp_flash_get_size(NULL, &flash_size) == ESP_OK;

    /* Heap capabilities are the portable ESP-IDF source of truth for external
     * RAM. They avoid linking board-specific PSRAM helper symbols while still
     * reporting actual allocator-visible capacity and margin. */
    const size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const size_t psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const bool psram_available = psram_total > 0U;

    const double internal_fragmentation = free_internal > 0U
                                               ? 1.0 - ((double)largest_internal / (double)free_internal)
                                               : 1.0;
    const esp_reset_reason_t reset_reason = esp_reset_reason();

    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);

    cJSON_AddStringToObject(root, "target", CONFIG_IDF_TARGET);
    cJSON_AddNumberToObject(root, "chip_model", chip.model);
    cJSON_AddNumberToObject(root, "chip_revision", chip.revision);
    cJSON_AddNumberToObject(root, "cpu_cores", chip.cores);
#ifdef CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ
    cJSON_AddNumberToObject(root, "cpu_frequency_mhz", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
#else
    cJSON_AddNullToObject(root, "cpu_frequency_mhz");
#endif
    cJSON_AddNumberToObject(root, "uptime_ms", (double)uptime_ms);
    cJSON_AddNumberToObject(root, "task_count", uxTaskGetNumberOfTasks());
    cJSON_AddNumberToObject(root, "reset_reason", reset_reason);
    cJSON_AddStringToObject(root, "reset_reason_name", reset_reason_name(reset_reason));

    cJSON_AddNumberToObject(root, "total_internal_heap_bytes", (double)total_internal);
    cJSON_AddNumberToObject(root, "free_heap_bytes", (double)free_total);
    cJSON_AddNumberToObject(root, "minimum_free_heap_bytes", (double)minimum_free);
    cJSON_AddNumberToObject(root, "free_internal_heap_bytes", (double)free_internal);
    cJSON_AddNumberToObject(root, "minimum_internal_heap_bytes", (double)minimum_internal);
    cJSON_AddNumberToObject(root, "largest_internal_block_bytes", (double)largest_internal);
    cJSON_AddNumberToObject(root, "internal_fragmentation_ratio", internal_fragmentation);

    cJSON_AddBoolToObject(root, "psram_available", psram_available);
    cJSON_AddNumberToObject(root, "psram_total_bytes", (double)psram_total);
    cJSON_AddNumberToObject(root, "psram_free_bytes", (double)psram_free);
    cJSON_AddNumberToObject(root, "psram_largest_block_bytes", (double)psram_largest);

    cJSON_AddBoolToObject(root, "flash_size_available", flash_size_available);
    if (flash_size_available) cJSON_AddNumberToObject(root, "flash_size_bytes", (double)flash_size);
    else cJSON_AddNullToObject(root, "flash_size_bytes");

    /* Temperature remains explicitly unavailable until the ESP-IDF temperature
     * sensor driver is initialized by the board-support layer. This avoids
     * presenting an unqualified internal reading as enclosure temperature. */
    cJSON_AddBoolToObject(root, "temperature_available", false);
    cJSON_AddNullToObject(root, "temperature_c");
    cJSON_AddStringToObject(root, "temperature_note",
                            "Internal temperature sensor is not initialized in this build");

    const bool reset_attention = reset_reason == ESP_RST_PANIC ||
                                 reset_reason == ESP_RST_INT_WDT ||
                                 reset_reason == ESP_RST_TASK_WDT ||
                                 reset_reason == ESP_RST_WDT ||
                                 reset_reason == ESP_RST_BROWNOUT ||
                                 reset_reason == ESP_RST_PWR_GLITCH ||
                                 reset_reason == ESP_RST_CPU_LOCKUP;
    const bool heap_warning = free_internal < 65536U || largest_internal < 32768U ||
                              internal_fragmentation > 0.70;
    const bool heap_critical = free_internal < 32768U || largest_internal < 16384U ||
                               internal_fragmentation > 0.85;
    cJSON_AddStringToObject(root, "resource_state",
                            heap_critical ? "critical" :
                            (heap_warning || reset_attention) ? "review" : "healthy");

    cJSON *thresholds = cJSON_AddObjectToObject(root, "thresholds");
    cJSON_AddNumberToObject(thresholds, "free_internal_warning_bytes", 65536);
    cJSON_AddNumberToObject(thresholds, "free_internal_critical_bytes", 32768);
    cJSON_AddNumberToObject(thresholds, "largest_block_warning_bytes", 32768);
    cJSON_AddNumberToObject(thresholds, "largest_block_critical_bytes", 16384);

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
