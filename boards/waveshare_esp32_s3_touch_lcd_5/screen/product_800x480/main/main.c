#include <stdbool.h>
#include <stdint.h>

#include "app_core.h"
#include "esp_err.h"
#include "esp_flash_dispatcher.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "local_backend_provider.h"
#include "local_commissioning_backend.h"
#include "local_source_commissioning_backend.h"
#include "lvgl.h"
#include "screen_api.h"
#include "screen_app.h"
#include "screen_runtime.h"
#include "waveshare_display_port.h"
#include "waveshare_display_profile.h"

#define SCREEN_REFRESH_STACK_BYTES 12288
#define PRODUCT_RGB_BOUNCE_LINES 10U
#define PRODUCT_TEAR_MODE ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE
#define SCREEN_FAST_MS 1000U
#define SCREEN_STATUS_MS 5000U
#define SCREEN_DEVICES_MS 2000U
#define SCREEN_OPERATIONS_MS 5000U
#define FLASH_DISPATCHER_STACK_BYTES 2048U

static const char *TAG = "waveshare_product";
static waveshare_display_port_handles_t s_display;
static const waveshare_display_profile_t *s_profile;
static const esp_lv_adapter_rotation_t s_rotation = ESP_LV_ADAPTER_ROTATE_0;
static screen_commissioning_snapshot_t s_commissioning;
static screen_commissioning_backend_t s_commissioning_backend;
static source_commission_backend_t s_source_commissioning_backend;
static bool s_flash_dispatcher_ready;

static void log_dma_headroom(const char *stage)
{
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;
    ESP_LOGI(TAG, "%s: internal DMA free=%u largest=%u",
             stage,
             (unsigned)heap_caps_get_free_size(caps),
             (unsigned)heap_caps_get_largest_free_block(caps));
}

/* Reserve only the board resources that MUST win the scarce DMA-capable DRAM
 * race before Wi-Fi/httpd/control tasks start. Full LVGL/UI creation is delayed
 * until after the unchanged shared Core has created its safety-critical tasks.
 *
 * Ten bounce lines match the hardware-stable standalone HIL setting and use
 * 32 kB total for the two RGB565 DRAM bounce buffers at 800 px width. Espressif
 * documents larger bounce buffers as more robust against short PSRAM-bandwidth
 * spikes; that is the failure class isolated after the locked Source page still
 * shook at the same cadence with no HMI refresh work running.
 *
 * Use one PSRAM framebuffer for the product HMI so a label repaint modifies the
 * currently scanned frame instead of switching the whole panel to another full
 * framebuffer. Core acquisition cadence, control timing and safety policy are
 * unchanged. sdkconfig reserves explicit internal/DMA headroom before ordinary
 * malloc traffic so the qualified ten-line bounce path can coexist with Wi-Fi.
 * The standalone HIL image remains pinned to its separately-qualified settings. */
static esp_err_t native_screen_reserve(void)
{
    s_profile = waveshare_display_profile(WAVESHARE_DISPLAY_800X480);
    if (!s_profile || s_profile->width != 800U || s_profile->height != 480U) {
        return ESP_ERR_INVALID_STATE;
    }

    const waveshare_display_port_config_t display_config = {
        .profile = s_profile,
        .i2c_bus = NULL,
        .tear_mode = PRODUCT_TEAR_MODE,
        .rotation = s_rotation,
        .enable_touch = true,
        .bounce_buffer_lines = PRODUCT_RGB_BOUNCE_LINES,
        .allow_no_bounce_fallback = true,
    };

    ESP_LOGI(TAG, "Reserving native 800x480 Waveshare LCD/touch DMA before Core");
    ESP_LOGI(TAG, "RGB live-update mode: SINGLE_FRAMEBUFFER with 10-line bounce and active-page rendering");
    log_dma_headroom("Before LCD DMA reservation");
    esp_err_t err = waveshare_display_port_init(&display_config, &s_display);
    if (err != ESP_OK) return err;
    log_dma_headroom("After LCD DMA reservation");
    ESP_LOGI(TAG, "Native screen DMA resources reserved before Core startup");
    return ESP_OK;
}

static esp_err_t init_flash_dispatcher(void)
{
    const esp_flash_dispatcher_config_t config = {
        .task_stack_size = FLASH_DISPATCHER_STACK_BYTES,
        .task_priority = 10,
        .task_core_id = tskNO_AFFINITY,
        .queue_size = 1,
    };
    const esp_err_t err = esp_flash_dispatcher_init(&config);
    if (err == ESP_OK) {
        s_flash_dispatcher_ready = true;
        ESP_LOGI(TAG,
                 "Espressif flash dispatcher ready; PSRAM-stacked HMI persistence is routed through internal RAM");
    } else {
        s_flash_dispatcher_ready = false;
        ESP_LOGE(TAG,
                 "Flash dispatcher init failed: %s; Engineering write backends stay disabled",
                 esp_err_to_name(err));
    }
    return err;
}

/* Everything here can wait until the shared Core has finished allocating its
 * internal task stacks and service objects. The LVGL adapter stack and display
 * draw buffers are explicitly PSRAM-backed. */
static esp_err_t native_screen_activate(void)
{
    if (!s_display.panel || !s_profile) return ESP_ERR_INVALID_STATE;

    esp_lv_adapter_config_t adapter_config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_config.task_stack_size = 12 * 1024;
    adapter_config.stack_in_psram = true;
    esp_err_t err = esp_lv_adapter_init(&adapter_config);
    if (err != ESP_OK) return err;

    esp_lv_adapter_display_config_t lv_display_config =
        ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
            s_display.panel, NULL, s_profile->width, s_profile->height, s_rotation);
    /* Keep adapter and esp_lcd ownership on the same single-buffer mode. */
    lv_display_config.tear_avoid_mode = PRODUCT_TEAR_MODE;
    lv_display_config.profile.use_psram = true;

    lv_display_t *display = esp_lv_adapter_register_display(&lv_display_config);
    if (!display) return ESP_FAIL;

    if (s_display.touch) {
        esp_lv_adapter_touch_config_t touch_config =
            ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(display, s_display.touch);
        if (!esp_lv_adapter_register_touch(&touch_config)) return ESP_FAIL;
    }

    err = esp_lv_adapter_start();
    if (err != ESP_OK) return err;

    err = esp_lv_adapter_lock(-1);
    if (err != ESP_OK) return err;
    lv_obj_t *root = screen_app_create(lv_screen_active());
    if (root) screen_app_show_backend_unavailable();
    esp_lv_adapter_unlock();
    if (!root) return ESP_ERR_NO_MEM;

    err = waveshare_display_port_backlight_on();
    if (err != ESP_OK) return err;

    log_dma_headroom("After LVGL/UI activation");
    ESP_LOGI(TAG, "Native LCD/LVGL/touch ready; awaiting existing Core data");
    return ESP_OK;
}

static void refresh_fast(void)
{
    (void)local_backend_provider_fetch(SCREEN_API_LIVE_PATH);
    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        (void)screen_runtime_refresh_fast();
        esp_lv_adapter_unlock();
    }
}

static void refresh_status(void)
{
    (void)local_backend_provider_fetch(SCREEN_API_STATUS_PATH);
    (void)local_backend_provider_fetch(SCREEN_API_TELEMETRY_PATH);
    const bool commissioning_ok = local_backend_provider_read_commissioning(&s_commissioning);
    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        (void)screen_runtime_refresh_status();
        if (commissioning_ok) screen_app_apply_commissioning(&s_commissioning);
        else screen_app_show_commissioning_unavailable();
        esp_lv_adapter_unlock();
    }
}

static void refresh_devices(void)
{
    (void)local_backend_provider_fetch(SCREEN_API_METERS_PATH);
    (void)local_backend_provider_fetch(SCREEN_API_INVERTERS_PATH);
    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        (void)screen_runtime_refresh_devices();
        esp_lv_adapter_unlock();
    }
}

static void refresh_operations(void)
{
    (void)local_backend_provider_fetch(SCREEN_API_ALARMS_PATH);
    (void)local_backend_provider_fetch(SCREEN_API_EVENTS_PATH);
    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        (void)screen_runtime_refresh_operations();
        esp_lv_adapter_unlock();
    }
}

static screen_page_t active_page(void)
{
    screen_page_t page = SCREEN_PAGE_OVERVIEW;
    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        page = screen_app_get_active_page();
        esp_lv_adapter_unlock();
    }
    return page;
}

/* Refresh only the context the operator can see. Core acquisition continues at
 * its own authoritative cadence; this changes only the HMI projection workload.
 * Page changes are refreshed immediately on the next 1 s tick, while steady
 * pages keep the existing bounded cadences. */
static void refresh_active_context(screen_page_t page, uint32_t elapsed_ms, bool page_changed)
{
    switch (page) {
    case SCREEN_PAGE_OVERVIEW:
        refresh_fast();
        if (page_changed || (elapsed_ms % SCREEN_STATUS_MS) == 0U) refresh_status();
        break;
    case SCREEN_PAGE_GRID:
    case SCREEN_PAGE_SOLAR:
        if (page_changed || (elapsed_ms % SCREEN_DEVICES_MS) == 0U) refresh_devices();
        break;
    case SCREEN_PAGE_ALARMS:
        if (page_changed || (elapsed_ms % SCREEN_OPERATIONS_MS) == 0U) refresh_operations();
        break;
    case SCREEN_PAGE_READINESS:
        if (page_changed || (elapsed_ms % SCREEN_STATUS_MS) == 0U) refresh_status();
        break;
    case SCREEN_PAGE_COMMISSIONING:
        if (page_changed || (elapsed_ms % SCREEN_STATUS_MS) == 0U) refresh_status();
        if (page_changed || (elapsed_ms % SCREEN_DEVICES_MS) == 0U) refresh_devices();
        break;
    case SCREEN_PAGE_SOURCE:
    case SCREEN_PAGE_COUNT:
    default:
        /* Source commissioning owns its own user-driven reads/writes. */
        break;
    }
}

static void screen_refresh_task(void *argument)
{
    (void)argument;
    TickType_t wake = xTaskGetTickCount();
    uint32_t elapsed_ms = 0U;
    screen_page_t previous = SCREEN_PAGE_COUNT;

    for (;;) {
        const screen_page_t page = active_page();
        const bool page_changed = page != previous;
        refresh_active_context(page, elapsed_ms, page_changed);
        previous = page;
        elapsed_ms += SCREEN_FAST_MS;
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(SCREEN_FAST_MS));
    }
}

void app_main(void)
{
    /* Use ESP-IDF's main task instead of allocating a second 16 kB internal
     * bootstrap stack. sdkconfig gives main enough measured headroom, and the
     * task is released automatically when app_main returns. */
    esp_err_t screen_reserve_err = native_screen_reserve();
    if (screen_reserve_err != ESP_OK) {
        ESP_LOGE(TAG, "Native screen DMA reservation failed: %s; Core continues headless",
                 esp_err_to_name(screen_reserve_err));
    }

    log_dma_headroom("Before Product Core init");
    esp_err_t core_err = app_core_init();
    if (core_err != ESP_OK) {
        ESP_LOGE(TAG, "Product Core initialization failed: %s; controller remains fail-safe",
                 esp_err_to_name(core_err));
    } else {
        ESP_LOGI(TAG, "Shared Product Core started");
    }
    log_dma_headroom("After Product Core init");

    /* ESP-IDF documents NVS/flash operations from PSRAM-stacked tasks as unsafe
     * unless they are routed through esp_flash_dispatcher. Initialize the
     * official dispatcher after Core startup has claimed its safety-critical
     * resources and before LVGL's PSRAM-stacked Engineering callbacks can run. */
    if (core_err == ESP_OK) {
        (void)init_flash_dispatcher();
    }

    esp_err_t screen_activate_err = screen_reserve_err;
    if (screen_reserve_err == ESP_OK) {
        screen_activate_err = native_screen_activate();
        if (screen_activate_err != ESP_OK) {
            ESP_LOGE(TAG, "Native screen LVGL/UI activation failed: %s; Core continues headless",
                     esp_err_to_name(screen_activate_err));
        }
    }

    if (screen_activate_err == ESP_OK && core_err == ESP_OK) {
        screen_api_provider_t provider = {0};
        if (local_backend_provider_init(&provider) && screen_runtime_init(&provider)) {
            if (s_flash_dispatcher_ready &&
                local_commissioning_backend_init(&s_commissioning_backend)) {
                if (esp_lv_adapter_lock(-1) == ESP_OK) {
                    screen_app_set_commissioning_backend(&s_commissioning_backend);
                    esp_lv_adapter_unlock();
                }
                ESP_LOGI(TAG, "Local Engineering commissioning backend bound to touchscreen");
            } else if (!s_flash_dispatcher_ready) {
                ESP_LOGE(TAG,
                         "Commissioning writes disabled because flash dispatcher is unavailable");
            } else {
                ESP_LOGE(TAG, "Local commissioning backend initialization failed; Commission page stays locked");
            }

            if (s_flash_dispatcher_ready &&
                local_source_commissioning_backend_init(&s_source_commissioning_backend)) {
                if (esp_lv_adapter_lock(-1) == ESP_OK) {
                    screen_app_set_source_commissioning_backend(&s_source_commissioning_backend);
                    esp_lv_adapter_unlock();
                }
                ESP_LOGI(TAG, "Local source-evidence commissioning backend bound to touchscreen");
            } else if (!s_flash_dispatcher_ready) {
                ESP_LOGE(TAG,
                         "Source commissioning writes disabled because flash dispatcher is unavailable");
            } else {
                ESP_LOGE(TAG, "Source commissioning backend initialization failed; Source page stays locked");
            }

            /* This task performs only in-process Core snapshot projections and
             * LVGL updates. Commissioning flash operations are intercepted by
             * Espressif's dispatcher and executed on its internal-RAM task. */
            BaseType_t created = xTaskCreateWithCaps(
                screen_refresh_task,
                "screen_refresh",
                SCREEN_REFRESH_STACK_BYTES,
                NULL,
                5,
                NULL,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (created == pdPASS) {
                ESP_LOGI(TAG, "Screen refresh task created in PSRAM");
                ESP_LOGI(TAG, "Screen read models bound in-process; commissioning writes require local Engineering unlock");
            } else {
                ESP_LOGE(TAG, "Unable to create PSRAM screen refresh task; UI stays unavailable");
            }
        } else {
            ESP_LOGE(TAG, "Screen backend provider initialization failed; UI stays unavailable");
        }
    }

    ESP_LOGI(TAG, "Main-task headroom %u bytes; free heap %u (minimum %u)",
             (unsigned)uxTaskGetStackHighWaterMark(NULL),
             (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size());
}
