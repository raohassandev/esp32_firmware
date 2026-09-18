#include "engineering_sections.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config_manager.h"
#include "engineering_config_bridge.h"
#include "engineering_runtime_bridge.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pvdg_ui_diagnostics.h"
#include "pvdg_ui_export_control.h"
#include "pvdg_ui_inverter_setup.h"
#include "pvdg_ui_meter_setup.h"
#include "pvdg_ui_source_setup.h"
#include "pvdg_ui_theme.h"
#include "screen_app.h"
#include "sdkconfig.h"
#include "solar_grid_config.h"

static lv_obj_t *s_overlay;
static lv_obj_t *s_content;
static lv_obj_t *s_title;
static lv_obj_t *s_pages[PVDG_UI_ENGINEERING_SECTION_COUNT];
static lv_obj_t *s_safety_text;
static lv_obj_t *s_config_text;

static const char *const s_names[PVDG_UI_ENGINEERING_SECTION_COUNT] = {
    "Source Setup", "Meter Setup", "Inverter Setup", "Export Control",
    "Safety Limits", "Network", "Diagnostics", "Configuration",
};

static lv_obj_t *make_button(lv_obj_t *parent, const char *text,
                             lv_event_cb_t callback, void *user)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_height(button, PVDG_UI_TOUCH_MIN);
    lv_obj_t *label = pvdg_ui_make_muted(button, text);
    lv_obj_set_style_text_color(label, lv_color_hex(PVDG_UI_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user);
    return button;
}

void engineering_sections_hide(void)
{
    if (s_overlay) lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void back_event(lv_event_t *event)
{
    (void)event;
    engineering_sections_hide();
}

static void safety_refresh(void)
{
    if (!s_safety_text) return;
    solar_grid_config_t config = {0};
    if (solar_grid_config_get_snapshot(&config) != ESP_OK) {
        lv_label_set_text(s_safety_text, "Safety/source configuration unavailable.");
        return;
    }
    char text[640];
    snprintf(text, sizeof(text),
             "Policy: %s\nExport limit: %.1f kW\nMinimum import: %.1f kW\n"
             "Generator 1: min %.1f%% · reverse margin %.1f kW\n"
             "Generator 2: min %.1f%% · reverse margin %.1f kW\n"
             "Generator 3: min %.1f%% · reverse margin %.1f kW\n\n"
             "Safety values are never inferred. Edit source evidence/timing in Source Setup and export policy in Export Control. Every save forces automatic control disabled and requires restart.",
             solar_grid_policy_name(config.policy),
             (double)config.export_limit_kw,
             (double)config.minimum_import_kw,
             (double)config.generators[0].minimum_loading_percent,
             (double)config.generators[0].reverse_power_margin_kw,
             (double)config.generators[1].minimum_loading_percent,
             (double)config.generators[1].reverse_power_margin_kw,
             (double)config.generators[2].minimum_loading_percent,
             (double)config.generators[2].reverse_power_margin_kw);
    lv_label_set_text(s_safety_text, text);
}

static void config_refresh(void)
{
    if (!s_config_text) return;
    app_config_t config = {0};
    if (config_manager_get_snapshot(&config) != ESP_OK) {
        lv_label_set_text(s_config_text, "Controller configuration unavailable.");
        return;
    }
    char text[420];
    snprintf(text, sizeof(text),
             "Device: %s\nMeters: %u\nInverters: %u\nAutomatic control: %s\n"
             "Primary Wi-Fi: %s\nFallback Wi-Fi: %s\n\n"
             "Configuration writes are authenticated. Transport/source changes force automatic control disabled before persistence.",
             config.device_name,
             (unsigned)config.meter_count,
             (unsigned)config.inverter_count,
             config.control.enabled ? "Enabled" : "Disabled",
             config.wifi.primary.ssid,
             config.wifi.fallback.enabled ? config.wifi.fallback.ssid : "Disabled");
    lv_label_set_text(s_config_text, text);
}

static const char *reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON: return "Power-on";
    case ESP_RST_SW: return "Software";
    case ESP_RST_PANIC: return "Panic";
    case ESP_RST_INT_WDT: return "Interrupt watchdog";
    case ESP_RST_TASK_WDT: return "Task watchdog";
    case ESP_RST_BROWNOUT: return "Brownout";
    default: return "Other";
    }
}

static void diagnostics_refresh(void)
{
    esp_chip_info_t chip = {0};
    esp_chip_info(&chip);
    uint32_t flash_size = 0U;
    const bool flash_ok = esp_flash_get_size(NULL, &flash_size) == ESP_OK;
    const uint32_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t min_internal = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    const uint32_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const uint32_t psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    const double fragmentation = free_internal > 0U
        ? 1.0 - ((double)largest_internal / (double)free_internal) : 0.0;

    pvdg_ui_system_resources_t resources = {
        .available = true,
        .target = "ESP32-S3",
        .chip_revision = chip.revision,
        .cpu_cores = chip.cores,
        .cpu_frequency_available = true,
        .cpu_frequency_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .uptime_ms = (uint64_t)esp_timer_get_time() / 1000ULL,
        .task_count = uxTaskGetNumberOfTasks(),
        .reset_reason_name = reset_reason_name(esp_reset_reason()),
        .total_internal_heap_bytes = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        .free_heap_bytes = esp_get_free_heap_size(),
        .minimum_free_heap_bytes = esp_get_minimum_free_heap_size(),
        .free_internal_heap_bytes = free_internal,
        .minimum_internal_heap_bytes = min_internal,
        .largest_internal_block_bytes = largest_internal,
        .internal_fragmentation_ratio = fragmentation,
        .psram_available = psram_total > 0U,
        .psram_total_bytes = psram_total,
        .psram_free_bytes = psram_free,
        .psram_largest_block_bytes = psram_largest,
        .flash_size_available = flash_ok,
        .flash_size_bytes = flash_size,
        .temperature_available = false,
        .temperature_c = 0.0,
        .temperature_note = "Not sampled by runtime UI",
        .resource_state = free_internal < 32768U ? PVDG_UI_RESOURCE_CRITICAL :
                          free_internal < 65536U ? PVDG_UI_RESOURCE_REVIEW :
                                                  PVDG_UI_RESOURCE_HEALTHY,
    };
    pvdg_ui_diagnostics_apply(&resources);
}

static void open_section_event(lv_event_t *event)
{
    const intptr_t section = (intptr_t)lv_event_get_user_data(event);
    if (section >= 0 && section < PVDG_UI_ENGINEERING_SECTION_COUNT) {
        engineering_sections_open((pvdg_ui_engineering_section_t)section);
    }
}

static void restart_task(void *argument)
{
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
}

static void restart_event(lv_event_t *event)
{
    (void)event;
    if (!engineering_runtime_bridge_is_authorized()) {
        if (s_config_text) {
            lv_label_set_text(s_config_text,
                              "Engineering authentication is required before restart.");
        }
        return;
    }
    if (s_config_text) lv_label_set_text(s_config_text, "Restarting controller...");
    (void)xTaskCreate(restart_task, "ui_restart", 2048, NULL, 4, NULL);
}

static lv_obj_t *make_info_page(lv_obj_t *parent, const char *title, lv_obj_t **text_out)
{
    lv_obj_t *root = lv_obj_create(parent);
    pvdg_ui_style_root(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_layout(root, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root, PVDG_UI_GAP_MD, LV_PART_MAIN);
    pvdg_ui_make_title(root, title);
    lv_obj_t *card = pvdg_ui_make_card(root);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_flex_grow(card, 1);
    lv_obj_t *text = pvdg_ui_make_muted(card, "--");
    lv_obj_set_width(text, LV_PCT(100));
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    if (text_out) *text_out = text;
    return root;
}

static void create_overlay(void)
{
    if (s_overlay) return;
    pvdg_ui_theme_init();
    s_overlay = lv_obj_create(lv_layer_top());
    pvdg_ui_style_root(s_overlay);
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_layout(s_overlay, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *top = pvdg_ui_make_card(s_overlay);
    lv_obj_set_width(top, LV_PCT(100));
    lv_obj_set_height(top, 44);
    lv_obj_remove_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(top, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(top, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_column(top, 8, LV_PART_MAIN);
    lv_obj_t *back = make_button(top, "Back", back_event, NULL);
    lv_obj_set_width(back, 90);
    s_title = pvdg_ui_make_title(top, "Engineering");

    s_content = lv_obj_create(s_overlay);
    pvdg_ui_style_root(s_content);
    lv_obj_set_width(s_content, LV_PCT(100));
    lv_obj_set_flex_grow(s_content, 1);
    lv_obj_remove_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);

    const pvdg_ui_source_setup_callbacks_t source_callbacks = {
        .request_config = engineering_config_source_request,
        .submit_config = engineering_config_source_submit,
        .user = NULL,
    };
    const pvdg_ui_meter_setup_callbacks_t meter_callbacks = {
        .request_list = engineering_config_meter_request,
        .submit_list = engineering_config_meter_submit,
        .user = NULL,
    };
    const pvdg_ui_inverter_setup_callbacks_t inverter_callbacks = {
        .request_config = engineering_config_inverter_request,
        .request_profiles = engineering_config_inverter_profiles_request,
        .request_assignments = engineering_config_inverter_assignments_request,
        .submit_config = engineering_config_inverter_submit,
        .assign_profile = engineering_config_inverter_assign_profile,
        .user = NULL,
    };
    const pvdg_ui_export_control_callbacks_t export_callbacks = {
        .request_config = engineering_config_export_request,
        .submit_config = engineering_config_export_submit,
        .user = NULL,
    };

    s_pages[PVDG_UI_ENGINEERING_SOURCE_SETUP] =
        pvdg_ui_source_setup_create(s_content, &source_callbacks);
    s_pages[PVDG_UI_ENGINEERING_METER_SETUP] =
        pvdg_ui_meter_setup_create(s_content, &meter_callbacks);
    s_pages[PVDG_UI_ENGINEERING_INVERTER_SETUP] =
        pvdg_ui_inverter_setup_create(s_content, &inverter_callbacks);
    s_pages[PVDG_UI_ENGINEERING_EXPORT_CONTROL] =
        pvdg_ui_export_control_create(s_content, &export_callbacks);

    s_pages[PVDG_UI_ENGINEERING_SAFETY_LIMITS] =
        make_info_page(s_content, "Safety Limits", &s_safety_text);
    lv_obj_t *safety_actions = lv_obj_create(s_pages[PVDG_UI_ENGINEERING_SAFETY_LIMITS]);
    pvdg_ui_style_root(safety_actions);
    lv_obj_set_width(safety_actions, LV_PCT(100));
    lv_obj_set_height(safety_actions, 46);
    lv_obj_set_layout(safety_actions, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(safety_actions, LV_FLEX_FLOW_ROW);
    make_button(safety_actions, "Open Source Setup", open_section_event,
                (void *)(intptr_t)PVDG_UI_ENGINEERING_SOURCE_SETUP);
    make_button(safety_actions, "Open Export Control", open_section_event,
                (void *)(intptr_t)PVDG_UI_ENGINEERING_EXPORT_CONTROL);

    /* Network uses the main Wi-Fi page, so this slot intentionally has no child. */
    s_pages[PVDG_UI_ENGINEERING_NETWORK] = NULL;

    s_pages[PVDG_UI_ENGINEERING_DIAGNOSTICS] = pvdg_ui_diagnostics_create(s_content);

    s_pages[PVDG_UI_ENGINEERING_CONFIGURATION] =
        make_info_page(s_content, "Configuration", &s_config_text);
    lv_obj_t *config_actions = lv_obj_create(s_pages[PVDG_UI_ENGINEERING_CONFIGURATION]);
    pvdg_ui_style_root(config_actions);
    lv_obj_set_width(config_actions, LV_PCT(100));
    lv_obj_set_height(config_actions, 46);
    lv_obj_set_layout(config_actions, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(config_actions, LV_FLEX_FLOW_ROW);
    make_button(config_actions, "Refresh", open_section_event,
                (void *)(intptr_t)PVDG_UI_ENGINEERING_CONFIGURATION);
    make_button(config_actions, "Restart Controller", restart_event, NULL);

    for (int i = 0; i < PVDG_UI_ENGINEERING_SECTION_COUNT; ++i) {
        if (s_pages[i]) lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}

void engineering_sections_open(pvdg_ui_engineering_section_t section)
{
    if (section < 0 || section >= PVDG_UI_ENGINEERING_SECTION_COUNT) return;
    if (section == PVDG_UI_ENGINEERING_NETWORK) {
        engineering_sections_hide();
        screen_app_show_page(SCREEN_PAGE_WIFI);
        return;
    }

    create_overlay();
    for (int i = 0; i < PVDG_UI_ENGINEERING_SECTION_COUNT; ++i) {
        if (!s_pages[i]) continue;
        if (i == (int)section) lv_obj_remove_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    pvdg_ui_label_set_if_changed(s_title, s_names[section]);

    switch (section) {
    case PVDG_UI_ENGINEERING_SOURCE_SETUP:
        engineering_config_source_request(NULL);
        break;
    case PVDG_UI_ENGINEERING_METER_SETUP:
        engineering_config_meter_request(NULL);
        break;
    case PVDG_UI_ENGINEERING_INVERTER_SETUP:
        engineering_config_inverter_request(NULL);
        engineering_config_inverter_profiles_request(NULL);
        engineering_config_inverter_assignments_request(NULL);
        break;
    case PVDG_UI_ENGINEERING_EXPORT_CONTROL:
        engineering_config_export_request(NULL);
        break;
    case PVDG_UI_ENGINEERING_SAFETY_LIMITS:
        safety_refresh();
        break;
    case PVDG_UI_ENGINEERING_DIAGNOSTICS:
        diagnostics_refresh();
        break;
    case PVDG_UI_ENGINEERING_CONFIGURATION:
        config_refresh();
        break;
    case PVDG_UI_ENGINEERING_NETWORK:
    case PVDG_UI_ENGINEERING_SECTION_COUNT:
    default:
        break;
    }
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_overlay);
}
