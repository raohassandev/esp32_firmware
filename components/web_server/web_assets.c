#include "web_assets.h"
#include <stdint.h>

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t app_css_start[] asm("_binary_app_css_start");
extern const uint8_t app_css_end[] asm("_binary_app_css_end");
extern const uint8_t theme_css_start[] asm("_binary_theme_css_start");
extern const uint8_t theme_css_end[] asm("_binary_theme_css_end");
extern const uint8_t product_mode_css_start[] asm("_binary_product_mode_css_start");
extern const uint8_t product_mode_css_end[] asm("_binary_product_mode_css_end");
extern const uint8_t operator_operations_css_start[] asm("_binary_operator_operations_css_start");
extern const uint8_t operator_operations_css_end[] asm("_binary_operator_operations_css_end");
extern const uint8_t operator_product_suite_css_start[] asm("_binary_operator_product_suite_css_start");
extern const uint8_t operator_product_suite_css_end[] asm("_binary_operator_product_suite_css_end");
extern const uint8_t wifi_css_start[] asm("_binary_wifi_css_start");
extern const uint8_t wifi_css_end[] asm("_binary_wifi_css_end");
extern const uint8_t devices_css_start[] asm("_binary_devices_css_start");
extern const uint8_t devices_css_end[] asm("_binary_devices_css_end");
extern const uint8_t em500_css_start[] asm("_binary_em500_css_start");
extern const uint8_t em500_css_end[] asm("_binary_em500_css_end");
extern const uint8_t app_js_start[] asm("_binary_app_js_start");
extern const uint8_t app_js_end[] asm("_binary_app_js_end");
extern const uint8_t theme_js_start[] asm("_binary_theme_js_start");
extern const uint8_t theme_js_end[] asm("_binary_theme_js_end");
extern const uint8_t product_mode_js_start[] asm("_binary_product_mode_js_start");
extern const uint8_t product_mode_js_end[] asm("_binary_product_mode_js_end");
extern const uint8_t operator_view_js_start[] asm("_binary_operator_view_js_start");
extern const uint8_t operator_view_js_end[] asm("_binary_operator_view_js_end");
extern const uint8_t operator_operations_js_start[] asm("_binary_operator_operations_js_start");
extern const uint8_t operator_operations_js_end[] asm("_binary_operator_operations_js_end");
extern const uint8_t operator_product_suite_js_start[] asm("_binary_operator_product_suite_js_start");
extern const uint8_t operator_product_suite_js_end[] asm("_binary_operator_product_suite_js_end");
extern const uint8_t engineering_errors_js_start[] asm("_binary_engineering_errors_js_start");
extern const uint8_t engineering_errors_js_end[] asm("_binary_engineering_errors_js_end");
extern const uint8_t ui_enhancements_js_start[] asm("_binary_ui_enhancements_js_start");
extern const uint8_t ui_enhancements_js_end[] asm("_binary_ui_enhancements_js_end");
extern const uint8_t wifi_utils_js_start[] asm("_binary_wifi_utils_js_start");
extern const uint8_t wifi_utils_js_end[] asm("_binary_wifi_utils_js_end");
extern const uint8_t wifi_guard_js_start[] asm("_binary_wifi_guard_js_start");
extern const uint8_t wifi_guard_js_end[] asm("_binary_wifi_guard_js_end");
extern const uint8_t wifi_js_start[] asm("_binary_wifi_js_start");
extern const uint8_t wifi_js_end[] asm("_binary_wifi_js_end");
extern const uint8_t devices_utils_js_start[] asm("_binary_devices_utils_js_start");
extern const uint8_t devices_utils_js_end[] asm("_binary_devices_utils_js_end");
extern const uint8_t devices_js_start[] asm("_binary_devices_js_start");
extern const uint8_t devices_js_end[] asm("_binary_devices_js_end");
extern const uint8_t devices_refresh_js_start[] asm("_binary_devices_refresh_js_start");
extern const uint8_t devices_refresh_js_end[] asm("_binary_devices_refresh_js_end");
extern const uint8_t inverter_profiles_js_start[] asm("_binary_inverter_profiles_js_start");
extern const uint8_t inverter_profiles_js_end[] asm("_binary_inverter_profiles_js_end");
extern const uint8_t inverter_config_js_start[] asm("_binary_inverter_config_js_start");
extern const uint8_t inverter_config_js_end[] asm("_binary_inverter_config_js_end");
extern const uint8_t inverter_telemetry_js_start[] asm("_binary_inverter_telemetry_js_start");
extern const uint8_t inverter_telemetry_js_end[] asm("_binary_inverter_telemetry_js_end");
extern const uint8_t em500_utils_js_start[] asm("_binary_em500_utils_js_start");
extern const uint8_t em500_utils_js_end[] asm("_binary_em500_utils_js_end");
extern const uint8_t em500_core_js_start[] asm("_binary_em500_core_js_start");
extern const uint8_t em500_core_js_end[] asm("_binary_em500_core_js_end");
extern const uint8_t em500_profiles_js_start[] asm("_binary_em500_profiles_js_start");
extern const uint8_t em500_profiles_js_end[] asm("_binary_em500_profiles_js_end");
extern const uint8_t em500_plan_js_start[] asm("_binary_em500_plan_js_start");
extern const uint8_t em500_plan_js_end[] asm("_binary_em500_plan_js_end");

static const char *asset(const uint8_t *start, const uint8_t *end, size_t *length)
{
    size_t size = (size_t)(end - start);
    if (size > 0 && start[size - 1] == '\0') --size;
    if (length) *length = size;
    return (const char *)start;
}

const char *web_assets_index(size_t *length) { return asset(index_html_start, index_html_end, length); }
const char *web_assets_css(size_t *length) { return asset(app_css_start, app_css_end, length); }
const char *web_assets_theme_css(size_t *length) { return asset(theme_css_start, theme_css_end, length); }
const char *web_assets_product_mode_css(size_t *length) { return asset(product_mode_css_start, product_mode_css_end, length); }
const char *web_assets_operator_operations_css(size_t *length) { return asset(operator_operations_css_start, operator_operations_css_end, length); }
const char *web_assets_operator_product_suite_css(size_t *length) { return asset(operator_product_suite_css_start, operator_product_suite_css_end, length); }
const char *web_assets_wifi_css(size_t *length) { return asset(wifi_css_start, wifi_css_end, length); }
const char *web_assets_devices_css(size_t *length) { return asset(devices_css_start, devices_css_end, length); }
const char *web_assets_em500_css(size_t *length) { return asset(em500_css_start, em500_css_end, length); }
const char *web_assets_js(size_t *length) { return asset(app_js_start, app_js_end, length); }
const char *web_assets_theme_js(size_t *length) { return asset(theme_js_start, theme_js_end, length); }
const char *web_assets_product_mode_js(size_t *length) { return asset(product_mode_js_start, product_mode_js_end, length); }
const char *web_assets_operator_view_js(size_t *length) { return asset(operator_view_js_start, operator_view_js_end, length); }
const char *web_assets_operator_operations_js(size_t *length) { return asset(operator_operations_js_start, operator_operations_js_end, length); }
const char *web_assets_operator_product_suite_js(size_t *length) { return asset(operator_product_suite_js_start, operator_product_suite_js_end, length); }
const char *web_assets_engineering_errors_js(size_t *length) { return asset(engineering_errors_js_start, engineering_errors_js_end, length); }
const char *web_assets_ui_enhancements_js(size_t *length) { return asset(ui_enhancements_js_start, ui_enhancements_js_end, length); }
const char *web_assets_wifi_utils_js(size_t *length) { return asset(wifi_utils_js_start, wifi_utils_js_end, length); }
const char *web_assets_wifi_guard_js(size_t *length) { return asset(wifi_guard_js_start, wifi_guard_js_end, length); }
const char *web_assets_wifi_js(size_t *length) { return asset(wifi_js_start, wifi_js_end, length); }
const char *web_assets_devices_utils_js(size_t *length) { return asset(devices_utils_js_start, devices_utils_js_end, length); }
const char *web_assets_devices_js(size_t *length) { return asset(devices_js_start, devices_js_end, length); }
const char *web_assets_devices_refresh_js(size_t *length) { return asset(devices_refresh_js_start, devices_refresh_js_end, length); }
const char *web_assets_inverter_profiles_js(size_t *length) { return asset(inverter_profiles_js_start, inverter_profiles_js_end, length); }
const char *web_assets_inverter_config_js(size_t *length) { return asset(inverter_config_js_start, inverter_config_js_end, length); }
const char *web_assets_inverter_telemetry_js(size_t *length) { return asset(inverter_telemetry_js_start, inverter_telemetry_js_end, length); }
const char *web_assets_em500_utils_js(size_t *length) { return asset(em500_utils_js_start, em500_utils_js_end, length); }
const char *web_assets_em500_core_js(size_t *length) { return asset(em500_core_js_start, em500_core_js_end, length); }
const char *web_assets_em500_profiles_js(size_t *length) { return asset(em500_profiles_js_start, em500_profiles_js_end, length); }
const char *web_assets_em500_plan_js(size_t *length) { return asset(em500_plan_js_start, em500_plan_js_end, length); }
