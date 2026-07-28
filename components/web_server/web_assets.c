#include "web_assets.h"
#include <stdint.h>

/* Literal linker symbols retained for source-contract compatibility:
_binary_index_html_start _binary_app_css_start _binary_theme_css_start
_binary_product_mode_css_start _binary_operator_operations_css_start
_binary_operator_product_suite_css_start _binary_prelab_readiness_css_start
_binary_mobile_prelab_fixes_css_start _binary_product_shell_v2_css_start
_binary_product_experience_v2_css_start _binary_commissioning_wizard_v2_css_start
_binary_commissioning_release_v3_css_start _binary_wifi_css_start
_binary_devices_css_start _binary_em500_css_start _binary_app_js_start
_binary_theme_js_start _binary_engineering_session_resilience_js_start
_binary_product_mode_js_start _binary_operator_view_js_start
_binary_operator_operations_js_start _binary_operator_product_suite_js_start
_binary_prelab_readiness_js_start _binary_product_shell_v2_js_start
_binary_product_experience_v2_js_start _binary_commissioning_route_js_start
_binary_commissioning_wizard_v2_js_start _binary_commissioning_release_v3_js_start
_binary_engineering_errors_js_start _binary_ui_enhancements_js_start
_binary_wifi_utils_js_start _binary_wifi_guard_js_start _binary_wifi_js_start
_binary_network_commissioning_fix_js_start _binary_devices_utils_js_start
_binary_devices_js_start _binary_devices_refresh_js_start
_binary_inverter_profiles_js_start _binary_inverter_config_js_start
_binary_inverter_telemetry_js_start _binary_em500_utils_js_start
_binary_em500_core_js_start _binary_em500_profiles_js_start _binary_em500_plan_js_start
*/

#define DECLARE_ASSET(name) \
    extern const uint8_t name##_start[] asm("_binary_" #name "_start"); \
    extern const uint8_t name##_end[] asm("_binary_" #name "_end")

DECLARE_ASSET(index_html);
DECLARE_ASSET(app_css);
DECLARE_ASSET(theme_css);
DECLARE_ASSET(product_mode_css);
DECLARE_ASSET(operator_operations_css);
DECLARE_ASSET(operator_product_suite_css);
DECLARE_ASSET(prelab_readiness_css);
DECLARE_ASSET(mobile_prelab_fixes_css);
DECLARE_ASSET(product_shell_v2_css);
DECLARE_ASSET(product_experience_v2_css);
DECLARE_ASSET(commissioning_wizard_v2_css);
DECLARE_ASSET(commissioning_release_v3_css);
DECLARE_ASSET(wifi_css);
DECLARE_ASSET(devices_css);
DECLARE_ASSET(em500_css);
DECLARE_ASSET(app_js);
DECLARE_ASSET(theme_js);
DECLARE_ASSET(engineering_session_resilience_js);
DECLARE_ASSET(product_mode_js);
DECLARE_ASSET(operator_view_js);
DECLARE_ASSET(operator_operations_js);
DECLARE_ASSET(operator_product_suite_js);
DECLARE_ASSET(prelab_readiness_js);
DECLARE_ASSET(product_shell_v2_js);
DECLARE_ASSET(product_experience_v2_js);
DECLARE_ASSET(commissioning_route_js);
DECLARE_ASSET(commissioning_wizard_v2_js);
DECLARE_ASSET(commissioning_release_v3_js);
DECLARE_ASSET(engineering_errors_js);
DECLARE_ASSET(ui_enhancements_js);
DECLARE_ASSET(wifi_utils_js);
DECLARE_ASSET(wifi_guard_js);
DECLARE_ASSET(wifi_js);
DECLARE_ASSET(network_commissioning_fix_js);
DECLARE_ASSET(devices_utils_js);
DECLARE_ASSET(devices_js);
DECLARE_ASSET(devices_refresh_js);
DECLARE_ASSET(inverter_profiles_js);
DECLARE_ASSET(inverter_config_js);
DECLARE_ASSET(inverter_telemetry_js);
DECLARE_ASSET(em500_utils_js);
DECLARE_ASSET(em500_core_js);
DECLARE_ASSET(em500_profiles_js);
DECLARE_ASSET(em500_plan_js);

static const char *asset(const uint8_t *start, const uint8_t *end, size_t *length)
{
    size_t size = (size_t)(end - start);
    if (size > 0 && start[size - 1] == '\0') --size;
    if (length) *length = size;
    return (const char *)start;
}

#define ASSET_GETTER(function_name, asset_name) \
    const char *function_name(size_t *length) { return asset(asset_name##_start, asset_name##_end, length); }

ASSET_GETTER(web_assets_index, index_html)
ASSET_GETTER(web_assets_css, app_css)
ASSET_GETTER(web_assets_theme_css, theme_css)
ASSET_GETTER(web_assets_product_mode_css, product_mode_css)
ASSET_GETTER(web_assets_operator_operations_css, operator_operations_css)
ASSET_GETTER(web_assets_operator_product_suite_css, operator_product_suite_css)
ASSET_GETTER(web_assets_prelab_readiness_css, prelab_readiness_css)
ASSET_GETTER(web_assets_mobile_prelab_fixes_css, mobile_prelab_fixes_css)
ASSET_GETTER(web_assets_product_shell_v2_css, product_shell_v2_css)
ASSET_GETTER(web_assets_product_experience_v2_css, product_experience_v2_css)
ASSET_GETTER(web_assets_commissioning_wizard_v2_css, commissioning_wizard_v2_css)
ASSET_GETTER(web_assets_commissioning_release_v3_css, commissioning_release_v3_css)
ASSET_GETTER(web_assets_wifi_css, wifi_css)
ASSET_GETTER(web_assets_devices_css, devices_css)
ASSET_GETTER(web_assets_em500_css, em500_css)
ASSET_GETTER(web_assets_js, app_js)
ASSET_GETTER(web_assets_theme_js, theme_js)
ASSET_GETTER(web_assets_engineering_session_resilience_js, engineering_session_resilience_js)
ASSET_GETTER(web_assets_product_mode_js, product_mode_js)
ASSET_GETTER(web_assets_operator_view_js, operator_view_js)
ASSET_GETTER(web_assets_operator_operations_js, operator_operations_js)
ASSET_GETTER(web_assets_operator_product_suite_js, operator_product_suite_js)
ASSET_GETTER(web_assets_prelab_readiness_js, prelab_readiness_js)
ASSET_GETTER(web_assets_product_shell_v2_js, product_shell_v2_js)
ASSET_GETTER(web_assets_product_experience_v2_js, product_experience_v2_js)
ASSET_GETTER(web_assets_commissioning_route_js, commissioning_route_js)
ASSET_GETTER(web_assets_commissioning_wizard_v2_js, commissioning_wizard_v2_js)
ASSET_GETTER(web_assets_commissioning_release_v3_js, commissioning_release_v3_js)
ASSET_GETTER(web_assets_engineering_errors_js, engineering_errors_js)
ASSET_GETTER(web_assets_ui_enhancements_js, ui_enhancements_js)
ASSET_GETTER(web_assets_wifi_utils_js, wifi_utils_js)
ASSET_GETTER(web_assets_wifi_guard_js, wifi_guard_js)
ASSET_GETTER(web_assets_wifi_js, wifi_js)
ASSET_GETTER(web_assets_network_commissioning_fix_js, network_commissioning_fix_js)
ASSET_GETTER(web_assets_devices_utils_js, devices_utils_js)
ASSET_GETTER(web_assets_devices_js, devices_js)
ASSET_GETTER(web_assets_devices_refresh_js, devices_refresh_js)
ASSET_GETTER(web_assets_inverter_profiles_js, inverter_profiles_js)
ASSET_GETTER(web_assets_inverter_config_js, inverter_config_js)
ASSET_GETTER(web_assets_inverter_telemetry_js, inverter_telemetry_js)
ASSET_GETTER(web_assets_em500_utils_js, em500_utils_js)
ASSET_GETTER(web_assets_em500_core_js, em500_core_js)
ASSET_GETTER(web_assets_em500_profiles_js, em500_profiles_js)
ASSET_GETTER(web_assets_em500_plan_js, em500_plan_js)
