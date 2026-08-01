#include "web_assets.h"
#include <stdint.h>

/* Literal linker symbols retained for source-contract compatibility:
_binary_app_bundle_js_start _binary_app_bundle_js_gz_start _binary_app_bundle_css_start _binary_app_bundle_css_gz_start _binary_index_html_start _binary_app_css_start _binary_theme_css_start
_binary_product_mode_css_start _binary_operator_operations_css_start
_binary_operator_product_suite_css_start _binary_prelab_readiness_css_start
_binary_mobile_prelab_fixes_css_start _binary_product_shell_v2_css_start
_binary_product_experience_v2_css_start _binary_commissioning_wizard_v2_css_start
_binary_commissioning_release_v3_css_start _binary_wifi_css_start
_binary_devices_css_start _binary_em500_css_start _binary_app_js_start
_binary_theme_js_start
_binary_product_mode_js_start _binary_operator_view_js_start _binary_operator_network_js_start _binary_operator_proof_js_start _binary_cards_css_start _binary_energy_flow_css_start _binary_icons_js_start _binary_meter_detail_js_start _binary_inverter_detail_js_start _binary_alarm_journal_js_start _binary_alarm_journal_css_start _binary_meter_detail_css_start
_binary_operator_operations_js_start _binary_operator_product_suite_js_start
_binary_prelab_readiness_js_start _binary_product_shell_v2_js_start
_binary_product_experience_v2_js_start
_binary_commissioning_wizard_v2_js_start _binary_commissioning_release_v3_js_start
_binary_engineering_errors_js_start _binary_ui_enhancements_js_start
_binary_wifi_utils_js_start _binary_wifi_guard_js_start _binary_wifi_js_start
_binary_network_commissioning_fix_js_start _binary_devices_utils_js_start
_binary_pvdg_chart_css_start _binary_pvdg_chart_js_start
_binary_devices_js_start _binary_devices_refresh_js_start
_binary_inverter_profiles_js_start _binary_inverter_config_js_start
_binary_inverter_telemetry_js_start _binary_em500_utils_js_start
_binary_em500_core_js_start _binary_em500_quality_js_start
_binary_em500_profiles_js_start _binary_em500_plan_js_start
_binary_source_detection_js_start _binary_solar_grid_js_start
*/

#define DECLARE_ASSET(name) \
    extern const uint8_t name##_start[] asm("_binary_" #name "_start"); \
    extern const uint8_t name##_end[] asm("_binary_" #name "_end")

/* The two bundles the browser actually receives, each in both forms. The
 * per-module assets below are still embedded and still served individually in
 * the preview and by the source contracts; what changed is that /app.js and
 * /app.css now come from ONE pre-built blob instead of being concatenated from
 * thirty-seven at request time. */
DECLARE_ASSET(app_bundle_js);
DECLARE_ASSET(app_bundle_js_gz);
DECLARE_ASSET(app_bundle_css);
DECLARE_ASSET(app_bundle_css_gz);

DECLARE_ASSET(index_html);

static const char *asset(const uint8_t *start, const uint8_t *end, size_t *length)
{
    size_t size = (size_t)(end - start);
    if (size > 0 && start[size - 1] == '\0') --size;
    if (length) *length = size;
    return (const char *)start;
}

#define ASSET_GETTER(function_name, asset_name) \
    const char *function_name(size_t *length) { return asset(asset_name##_start, asset_name##_end, length); }

ASSET_GETTER(web_assets_bundle_js, app_bundle_js)
ASSET_GETTER(web_assets_bundle_js_gz, app_bundle_js_gz)
ASSET_GETTER(web_assets_bundle_css, app_bundle_css)
ASSET_GETTER(web_assets_bundle_css_gz, app_bundle_css_gz)

ASSET_GETTER(web_assets_index, index_html)
