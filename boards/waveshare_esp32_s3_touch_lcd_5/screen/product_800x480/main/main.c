#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

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
#define PRODUCT_RGB_BOUNCE_LINES 6U
#define PRODUCT_RGB_PCLK_HZ 12000000U
#define PRODUCT_TEAR_MODE ESP_LV_ADAPTER_TEAR_AVOID_MODE_DOUBLE_DIRECT
#define SCREEN_LVGL_LOCK_MS 2000U
#define SCREEN_FAST_MS 1000U
#define SCREEN_STATUS_MS 5000U
#define SCREEN_DEVICES_MS 2000U
#define SCREEN_OPERATIONS_MS 5000U
#define SCREEN_RESOURCE_LOG_MS 60000U
#define FLASH_DISPATCHER_STACK_BYTES 2048U

static const char *TAG = "waveshare_product";
static waveshare_display_port_handles_t s_display;
static waveshare_display_profile_t s_product_profile;
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

/* Periodic hardware-evidence line for the stabilization/soak lane. This is
 * deliberately low cadence (once per minute) so gathering proof does not become
 * the workload being measured. The values let a physical run show whether the
 * second RGB framebuffer or sustained UI activity is collapsing PSRAM, scarce
 * internal DMA memory, total heap, or the PSRAM-backed refresh-task stack. */
static void log_runtime_headroom(const char *stage)
{
    const uint32_t psram_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    const uint32_t dma_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;
    ESP_LOGI(TAG,
             "%s: heap free=%u min=%u | PSRAM free=%u largest=%u | DMA free=%u largest=%u | screen stack hwm=%u",
             stage,
             (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size(),
             (unsigned)heap_caps_get_free_size(psram_caps),
             (unsigned)heap_caps_get_largest_free_block(psram_caps),
             (unsigned)heap_caps_get_free_size(dma_caps),
             (unsigned)heap_caps_get_largest_free_block(dma_caps),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

/* Reserve only the board resources that MUST win the scarce DMA-capable DRAM
 * race before Wi-Fi/httpd/control tasks start. Full LVGL/UI creation is delayed
 * until after the unchanged shared Core has created its safety-critical tasks.
 *
 * The failed ten-line product candidate consumed 32 kB total for the two
 * RGB565 DRAM bounce buffers at 800 px width. This candidate uses six lines
 * (19.2 kB total), releasing 12.8 kB of scarce internal/DMA RAM. ESP-IDF's RGB
 * guidance recommends PSRAM XIP and a 64-byte D-cache line for bounce-buffer
 * mode; sdkconfig locks both while keeping the smaller cache sizes that return
 * SRAM to heap. The driver also restores the pinned vendor transfer alignment.
 *
 * The product HMI uses DOUBLE_DIRECT and two PSRAM framebuffers. To offset the
 * smaller bounce pool under simultaneous Wi-Fi/Core traffic, only the product
 * profile lowers PCLK from the vendor/HIL 16 MHz baseline to 12 MHz. Timings,
 * GPIO mapping and the separately-qualified standalone HIL profile remain
 * unchanged. This is an industrial HMI, so the reduced refresh rate is an
 * acceptable trade for deterministic RGB refill bandwidth and memory margin.
 * Core acquisition cadence, control timing and safety policy are unchanged. */
static esp_err_t native_screen_reserve(void)
{
    const waveshare_display_profile_t *vendor_profile =
        waveshare_display_profile(WAVESHARE_DISPLAY_800X480);
    if (!vendor_profile || vendor_profile->width != 800U || vendor_profile->height != 480U) {
        return ESP_ERR_INVALID_STATE;
    }

    s_product_profile = *vendor_profile;
    s_product_profile.pixel_clock_hz = PRODUCT_RGB_PCLK_HZ;
    s_profile = &s_product_profile;

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
    ESP_LOGI(TAG,
             "RGB live-update mode: DOUBLE_DIRECT anti-tear, 2 PSRAM framebuffers, 6-line bounce, pclk=%u Hz",
             (unsigned)s_profile->pixel_clock_hz);
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
 * draw buffers are explicitly PSRAM-backed. External adapter locks are bounded:
 * a stalled LVGL worker must become an observable error, not hold app_main or
 * the refresh task forever. */
static esp_err_t native_screen_activate(void)
{
    if (!s_display.panel || !s_profile) return ESP_ERR_INVALID_STATE;

    ESP_LOGI(TAG, "LVGL activation stage 1/6: adapter init");
    esp_lv_adapter_config_t adapter_config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_config.task_stack_size = 12 * 1024;
    adapter_config.stack_in_psram = true;
    esp_err_t err = esp_lv_adapter_init(&adapter_config);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "LVGL activation stage 2/6: display registration");
    esp_lv_adapter_display_config_t lv_display_config =
        ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
            s_display.panel, NULL, s_profile->width, s_profile->height, s_rotation);
    lv_display_config.tear_avoid_mode = PRODUCT_TEAR_MODE;
    lv_display_config.profile.use_psram = true;

    lv_display_t *display = esp_lv_adapter_register_display(&lv_display_config);
    if (!display) return ESP_FAIL;

    ESP_LOGI(TAG, "LVGL activation stage 3/6: touch registration");
    if (s_display.touch) {
        esp_lv_adapter_touch_config_t touch_config =
            ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(display, s_display.touch);
        if (!esp_lv_adapter_register_touch(&touch_config)) return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LVGL activation stage 4/6: adapter task start");
    err = esp_lv_adapter_start();
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "LVGL activation stage 5/6: create visible Overview (lock timeout=%u ms)",
             (unsigned)SCREEN_LVGL_LOCK_MS);
    err = esp_lv_adapter_lock(SCREEN_LVGL_LOCK_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LVGL activation lock timed out/failed: %s", esp_err_to_name(err));
        return err;
    }
    lv_obj_t *root = screen_app_create(lv_screen_active());
    if (root) screen_app_show_backend_unavailable();
    esp_lv_adapter_unlock();
    if (!root) return ESP_ERR_NO_MEM;

    ESP_LOGI(TAG, "LVGL activation stage 6/6: backlight on");
    err = waveshare_display_port_backlight_on();
    if (err != ESP_OK) return err;

    log_dma_headroom("After LVGL/UI activation");
    ESP_LOGI(TAG, "Native LCD/LVGL/touch ready; awaiting existing Core data");
    return ESP_OK;
}

static void refresh_fast(void)
{
    (void)local_backend_provider_fetch(SCREEN_API_LIVE_PATH);
    if (esp_lv_adapter_lock(SCREEN_LVGL_LOCK_MS) == ESP_OK) {
        (void)screen_runtime_refresh_fast();
        esp_lv_adapter_unlock();
    }
}

static void refresh_status(void)
{
    (void)local_backend_provider_fetch(SCREEN_API_STATUS_PATH);
    (void)local_backend_provider_fetch(SCREEN_API_TELEMETRY_PATH);
    const bool commissioning_ok = local_backend_provider_read_commissioning(&s_commissioning);
    if (esp_lv_adapter_lock(SCREEN_LVGL_LOCK_MS) == ESP_OK) {
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
    if (esp_lv_adapter_lock(SCREEN_LVGL_LOCK_MS) == ESP_OK) {
        (void)screen_runtime_refresh_devices();
        esp_lv_adapter_unlock();
    }
}

static void refresh_operations(void)
{
    (void)local_backend_provider_fetch(SCREEN_API_ALARMS_PATH);
    (void)local_backend_provider_fetch(SCREEN_API_EVENTS_PATH);
    if (esp_lv_adapter_lock(SCREEN_LVGL_LOCK_MS) == ESP_OK) {
        (void)screen_runtime_refresh_operations();
        esp_lv_adapter_unlock();
    }
}

static screen_page_t active_page(void)
{
    screen_page_t page = SCREEN_PAGE_OVERVIEW;
    if (esp_lv_adapter_lock(SCREEN_LVGL_LOCK_MS) == ESP_OK) {
        page = screen_app_get_active_page();
        esp_lv_adapter_unlock();
    }
    return page;
}

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
        break;
    }
}

/* Per-task stack high-water marks. The remaining internal DMA shortfall is
 * dominated by FreeRTOS stacks, and right-sizing them has to be driven by
 * measured peak usage rather than by the configured numbers. Reported once per
 * soak interval alongside the memory line. */
static void log_task_stacks(const char *stage)
{
    const UBaseType_t count = uxTaskGetNumberOfTasks();
    TaskStatus_t *tasks = calloc(count, sizeof(TaskStatus_t));
    if (!tasks) return;
    const UBaseType_t got = uxTaskGetSystemState(tasks, count, NULL);
    for (UBaseType_t i = 0; i < got; ++i) {
        ESP_LOGI(TAG, "%s stack: %-16s hwm=%u prio=%u",
                 stage,
                 tasks[i].pcTaskName,
                 (unsigned)tasks[i].usStackHighWaterMark,
                 (unsigned)tasks[i].uxCurrentPriority);
    }
    free(tasks);
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
        if ((elapsed_ms % SCREEN_RESOURCE_LOG_MS) == 0U) {
            log_runtime_headroom("Screen soak");
            log_task_stacks("Screen soak");
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(SCREEN_FAST_MS));
    }
}

void app_main(void)
{
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
                if (esp_lv_adapter_lock(SCREEN_LVGL_LOCK_MS) == ESP_OK) {
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
                if (esp_lv_adapter_lock(SCREEN_LVGL_LOCK_MS) == ESP_OK) {
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
