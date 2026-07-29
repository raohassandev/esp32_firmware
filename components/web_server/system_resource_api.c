#include "system_resource_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audit_log.h"
#include "cJSON.h"
#include "config_manager.h"
#include "config_types.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

/* The product this firmware IS. It is a compile-time property of the image, not
 * a per-unit attribute, so reporting it invents nothing.
 *
 * There is deliberately no serial number and no hardware revision anywhere in
 * this file. Neither is programmed into this controller, and a service page
 * that displays a plausible-looking serial number is worse than one that
 * displays none: a field engineer will quote it into a warranty claim or an RMA
 * and nobody downstream will be able to tell it was manufactured by the UI. The
 * MAC address is the identity this unit genuinely has, and it is what operators
 * already quote. */
#define PVDG_PRODUCT_MODEL "Automatrix PV-DG Controller"

/* Single source of truth for the health thresholds: the same constants decide
 * resource_state and are published, so the service page can never disagree with
 * the controller about what "review" or "critical" means. */
#define RESOURCE_FREE_INTERNAL_WARNING_BYTES 65536
#define RESOURCE_FREE_INTERNAL_CRITICAL_BYTES 32768
#define RESOURCE_LARGEST_BLOCK_WARNING_BYTES 32768
#define RESOURCE_LARGEST_BLOCK_CRITICAL_BYTES 16384
#define RESOURCE_FRAGMENTATION_WARNING_RATIO 0.70
#define RESOURCE_FRAGMENTATION_CRITICAL_RATIO 0.85

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

/* The reboot reasons a service engineer must not scroll past. A controller that
 * came back from a panic, a watchdog or a brownout did not simply restart: it
 * failed, and the site probably saw PV drop to zero when it did. */
static bool reset_reason_unexpected(esp_reset_reason_t reason)
{
    return reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
           reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT ||
           reason == ESP_RST_BROWNOUT || reason == ESP_RST_PWR_GLITCH ||
           reason == ESP_RST_CPU_LOCKUP;
}

/* Only enumerators that have existed across the ESP-IDF 5.x/6.x range are named
 * explicitly; anything else falls back to the build's own target string rather
 * than to a guess. A wrong chip name on a service page misroutes a repair. */
static const char *chip_model_name(esp_chip_model_t model)
{
    switch (model) {
        case CHIP_ESP32: return "ESP32";
        case CHIP_ESP32S2: return "ESP32-S2";
        case CHIP_ESP32S3: return "ESP32-S3";
        case CHIP_ESP32C3: return "ESP32-C3";
        default: return CONFIG_IDF_TARGET;
    }
}

static void format_mac(const uint8_t mac[6], char out[18])
{
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* Adds a MAC as the field identity of the unit. Reported as unavailable rather
 * than blank if the read fails, so an empty field is never mistaken for a
 * device that has no address. */
static void add_mac(cJSON *parent, const char *name, esp_mac_type_t type)
{
    uint8_t mac[6] = {0};
    char text[18];
    if (esp_read_mac(mac, type) == ESP_OK) {
        format_mac(mac, text);
        cJSON_AddStringToObject(parent, name, text);
    } else {
        cJSON_AddNullToObject(parent, name);
    }
}

static const char *partition_type_name(esp_partition_type_t type)
{
    switch (type) {
        case ESP_PARTITION_TYPE_APP: return "app";
        case ESP_PARTITION_TYPE_DATA: return "data";
        default: return "other";
    }
}

/* GET /api/system/identity
 *
 * A sibling endpoint rather than more fields on /api/system/resources, for two
 * reasons that matter in the field:
 *
 *  - Identity is static for the life of a boot. Health is not. The service page
 *    polls resources continuously; making every poll also walk the partition
 *    table and read the configuration snapshot would burn CPU and heap on a
 *    controller whose health is the thing being measured.
 *  - Identity is what an engineer copies into a report or a support ticket. A
 *    single stable document that can be captured once, without a moving heap
 *    figure in the middle of it, is what that job actually needs.
 *
 * The two overlap only where overlap is load-bearing: firmware version and MAC
 * also appear in the health payload, so a screenshot of a struggling controller
 * can still be attributed to a unit.
 */
static esp_err_t identity_get(httpd_req_t *request)
{
    esp_chip_info_t chip = {0};
    esp_chip_info(&chip);
    const esp_app_desc_t *app = esp_app_get_description();
    const esp_reset_reason_t reset_reason = esp_reset_reason();

    uint32_t flash_size = 0;
    const bool flash_size_available = esp_flash_get_size(NULL, &flash_size) == ESP_OK;

    /* app_config_t is kilobytes; never place it on an HTTP handler stack. */
    app_config_t *config = malloc(sizeof(*config));
    const bool config_available = config != NULL && config_manager_get_snapshot(config) == ESP_OK;

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(config);
        return httpd_resp_send_500(request);
    }

    cJSON_AddStringToObject(root, "product_model", PVDG_PRODUCT_MODEL);
    cJSON_AddStringToObject(root, "project_name", app ? app->project_name : "unknown");

    /* Operator-assigned label. Named as such so it is never mistaken for a
     * factory identifier. */
    if (config_available) cJSON_AddStringToObject(root, "device_name", config->device_name);
    else cJSON_AddNullToObject(root, "device_name");

    /* This controller has no factory-programmed serial number and no readable
     * hardware revision. Both are reported as unavailable, with the reason, so
     * that nobody quotes an invented one. */
    cJSON_AddBoolToObject(root, "serial_number_available", false);
    cJSON_AddNullToObject(root, "serial_number");
    cJSON_AddBoolToObject(root, "hardware_revision_available", false);
    cJSON_AddNullToObject(root, "hardware_revision");
    cJSON_AddStringToObject(root, "identity_note",
                            "No serial number or hardware revision is programmed into this "
                            "controller, so none is reported. Identify the unit by its MAC "
                            "address, which is unique and readable from the hardware.");

    cJSON *firmware = cJSON_AddObjectToObject(root, "firmware");
    if (firmware) {
        /* PROJECT_VER, set from the git short commit hash by the top-level
         * CMakeLists so an image is traceable to a commit rather than to the
         * nearest unrelated tag. */
        cJSON_AddStringToObject(firmware, "version", app ? app->version : "unknown");
        cJSON_AddStringToObject(firmware, "version_source", "git_short_commit");
        cJSON_AddStringToObject(firmware, "build_date", app ? app->date : "unknown");
        cJSON_AddStringToObject(firmware, "build_time", app ? app->time : "unknown");
        cJSON_AddStringToObject(firmware, "idf_version", app ? app->idf_ver : IDF_VER);
        cJSON_AddStringToObject(firmware, "idf_version_compiled", IDF_VER);
        cJSON_AddNumberToObject(firmware, "secure_version", app ? app->secure_version : 0);
    }

    cJSON *hardware = cJSON_AddObjectToObject(root, "hardware");
    if (hardware) {
        cJSON_AddStringToObject(hardware, "target", CONFIG_IDF_TARGET);
        cJSON_AddStringToObject(hardware, "chip_model_name", chip_model_name(chip.model));
        cJSON_AddNumberToObject(hardware, "chip_model", chip.model);
        /* ESP-IDF encodes the silicon revision as major * 100 + minor. */
        cJSON_AddNumberToObject(hardware, "chip_revision", chip.revision);
        cJSON_AddNumberToObject(hardware, "chip_revision_major", chip.revision / 100);
        cJSON_AddNumberToObject(hardware, "chip_revision_minor", chip.revision % 100);
        cJSON_AddNumberToObject(hardware, "cpu_cores", chip.cores);
        add_mac(hardware, "mac_address", ESP_MAC_WIFI_STA);
        add_mac(hardware, "mac_address_softap", ESP_MAC_WIFI_SOFTAP);
        cJSON_AddStringToObject(hardware, "mac_address_note",
                                "Station MAC. This is the identity to quote for this unit.");
        cJSON_AddBoolToObject(hardware, "flash_size_available", flash_size_available);
        if (flash_size_available) {
            cJSON_AddNumberToObject(hardware, "flash_size_bytes", (double)flash_size);
        } else {
            cJSON_AddNullToObject(hardware, "flash_size_bytes");
        }
    }

    cJSON *configuration = cJSON_AddObjectToObject(root, "configuration");
    if (configuration) {
        cJSON_AddNumberToObject(configuration, "schema_version_supported", APP_CONFIG_VERSION);
        if (config_available) {
            cJSON_AddNumberToObject(configuration, "schema_version_stored", config->version);
            cJSON_AddBoolToObject(configuration, "schema_current",
                                  config->version == APP_CONFIG_VERSION);
        } else {
            cJSON_AddNullToObject(configuration, "schema_version_stored");
            cJSON_AddNullToObject(configuration, "schema_current");
        }
        cJSON_AddBoolToObject(configuration, "snapshot_available", config_available);
    }

    if (config) {
        memset(config, 0, sizeof(*config));  /* the snapshot holds Wi-Fi PSKs */
        free(config);
    }

    cJSON *runtime = cJSON_AddObjectToObject(root, "runtime");
    if (runtime) {
        cJSON_AddNumberToObject(runtime, "uptime_ms",
                                (double)((uint64_t)esp_timer_get_time() / 1000ULL));
        cJSON_AddNumberToObject(runtime, "reset_reason", reset_reason);
        cJSON_AddStringToObject(runtime, "reset_reason_name", reset_reason_name(reset_reason));
        cJSON_AddBoolToObject(runtime, "last_reboot_unexpected", reset_reason_unexpected(reset_reason));
        cJSON_AddBoolToObject(runtime, "wall_clock_available", false);
        cJSON_AddStringToObject(runtime, "time_note",
                                "The controller has no real-time clock. Uptime is the only time "
                                "reference it can offer; it cannot date the last reboot.");
    }

    /* Partition layout: which slot is running, and how much room the image has.
     * This is the first thing to check when an OTA appears not to have taken. */
    cJSON *partitions = cJSON_AddObjectToObject(root, "partitions");
    if (partitions) {
        const esp_partition_t *running = esp_ota_get_running_partition();
        cJSON_AddStringToObject(partitions, "running_label", running ? running->label : "unknown");
        if (running) cJSON_AddNumberToObject(partitions, "running_size_bytes", (double)running->size);
        else cJSON_AddNullToObject(partitions, "running_size_bytes");

        uint32_t total = 0;
        uint32_t app_slots = 0;
        cJSON *items = cJSON_AddArrayToObject(partitions, "table");
        esp_partition_iterator_t iterator =
            esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
        while (iterator != NULL) {
            const esp_partition_t *entry = esp_partition_get(iterator);
            if (entry) {
                total++;
                if (entry->type == ESP_PARTITION_TYPE_APP) app_slots++;
                if (items) {
                    cJSON *item = cJSON_CreateObject();
                    if (item) {
                        cJSON_AddStringToObject(item, "label", entry->label);
                        cJSON_AddStringToObject(item, "type", partition_type_name(entry->type));
                        cJSON_AddNumberToObject(item, "subtype", entry->subtype);
                        cJSON_AddNumberToObject(item, "address", (double)entry->address);
                        cJSON_AddNumberToObject(item, "size_bytes", (double)entry->size);
                        cJSON_AddBoolToObject(item, "encrypted", entry->encrypted);
                        cJSON_AddBoolToObject(item, "running",
                                              running != NULL && entry->address == running->address);
                        cJSON_AddItemToArray(items, item);
                    }
                }
            }
            iterator = esp_partition_next(iterator);
        }
        /* esp_partition_next() releases the iterator when it returns NULL, and
         * releasing NULL is a no-op. The call is kept so the iterator is freed
         * under either reading of that contract rather than leaked on a
         * long-lived controller that serves this page repeatedly. */
        esp_partition_iterator_release(iterator);
        cJSON_AddNumberToObject(partitions, "count", total);
        cJSON_AddNumberToObject(partitions, "app_slot_count", app_slots);
    }

    cJSON_AddStringToObject(root, "health_endpoint", "/api/system/resources");
    cJSON_AddStringToObject(root, "audit_log_endpoint", "/api/system/audit-log");

    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) return httpd_resp_send_500(request);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    const esp_err_t err = httpd_resp_sendstr(request, text);
    free(text);
    return err;
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

    /* A health screenshot that cannot be attributed to a unit is evidence of
     * nothing, so the two identifying fields are repeated here. Everything else
     * about the unit lives on /api/system/identity. */
    const esp_app_desc_t *app = esp_app_get_description();
    cJSON_AddStringToObject(root, "firmware_version", app ? app->version : "unknown");
    add_mac(root, "mac_address", ESP_MAC_WIFI_STA);
    cJSON_AddStringToObject(root, "identity_endpoint", "/api/system/identity");

    cJSON_AddStringToObject(root, "target", CONFIG_IDF_TARGET);
    cJSON_AddStringToObject(root, "chip_model_name", chip_model_name(chip.model));
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

    const bool reset_attention = reset_reason_unexpected(reset_reason);
    cJSON_AddBoolToObject(root, "last_reboot_unexpected", reset_attention);
    const bool heap_warning = free_internal < (size_t)RESOURCE_FREE_INTERNAL_WARNING_BYTES ||
                              largest_internal < (size_t)RESOURCE_LARGEST_BLOCK_WARNING_BYTES ||
                              internal_fragmentation > RESOURCE_FRAGMENTATION_WARNING_RATIO;
    const bool heap_critical = free_internal < (size_t)RESOURCE_FREE_INTERNAL_CRITICAL_BYTES ||
                               largest_internal < (size_t)RESOURCE_LARGEST_BLOCK_CRITICAL_BYTES ||
                               internal_fragmentation > RESOURCE_FRAGMENTATION_CRITICAL_RATIO;
    cJSON_AddStringToObject(root, "resource_state",
                            heap_critical ? "critical" :
                            (heap_warning || reset_attention) ? "review" : "healthy");

    cJSON *thresholds = cJSON_AddObjectToObject(root, "thresholds");
    cJSON_AddNumberToObject(thresholds, "free_internal_warning_bytes",
                            RESOURCE_FREE_INTERNAL_WARNING_BYTES);
    cJSON_AddNumberToObject(thresholds, "free_internal_critical_bytes",
                            RESOURCE_FREE_INTERNAL_CRITICAL_BYTES);
    cJSON_AddNumberToObject(thresholds, "largest_block_warning_bytes",
                            RESOURCE_LARGEST_BLOCK_WARNING_BYTES);
    cJSON_AddNumberToObject(thresholds, "largest_block_critical_bytes",
                            RESOURCE_LARGEST_BLOCK_CRITICAL_BYTES);
    cJSON_AddNumberToObject(thresholds, "fragmentation_warning_ratio",
                            RESOURCE_FRAGMENTATION_WARNING_RATIO);
    cJSON_AddNumberToObject(thresholds, "fragmentation_critical_ratio",
                            RESOURCE_FRAGMENTATION_CRITICAL_RATIO);

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
    const httpd_uri_t handlers[] = {
        {.uri = "/api/system/resources", .method = HTTP_GET, .handler = resources_get},
        {.uri = "/api/system/identity", .method = HTTP_GET, .handler = identity_get},
    };
    for (size_t index = 0; index < sizeof(handlers) / sizeof(handlers[0]); ++index) {
        const esp_err_t err = httpd_register_uri_handler(server, &handlers[index]);
        if (err != ESP_OK) return err;
    }
    /* The audit log is an /api/system endpoint and is registered alongside its
     * siblings. web_server.c is owned by other work in flight this iteration,
     * so the registration is chained here rather than added to the server's own
     * registration list; it belongs there once that file is free. */
    return audit_log_api_register(server);
}
