# ---------------------------------------------------------------------------
# Web assets and the CSS cascade
#
# index.html links exactly one stylesheet, /app.css, and one script, /app.js.
# Every other file below is bundled at request time: `css_handler` in
# web_server.c concatenates the embedded CSS blobs and serves them as /app.css.
#
# IMPORTANT: the order of the lists in THIS file does NOT determine the
# cascade. Only the `assets[]` array in `css_handler` (web_server.c) does.
# The two orders are deliberately different - do not "align" them, and do not
# assume that moving a line here changes what the browser sees.
#
# Cascade order as served (later layers win at equal specificity):
#
#   1  app.css                     Base design system: :root tokens (dark is the
#                                  default theme), reset, shell, panels, buttons,
#                                  forms. Also holds the compatibility aliases
#                                  (--panel-bg, --surface, --border, --input-bg,
#                                  --good/--warning/--bad ...) that make legacy
#                                  `var(--x, <literal>)` call sites theme-aware.
#   2  wifi.css                    \
#   3  devices.css                  } Per-feature component styles. These sit
#   4  em500.css                   /  BEFORE theme.css on purpose.
#   5  theme.css                   Light-theme token overrides
#                                  (html[data-theme="light"]) plus desktop
#                                  density and the shared responsive
#                                  breakpoints. MUST stay after the feature
#                                  sheets above so light theme can correct them;
#                                  tests/ui_runtime_consistency_source_contract.py
#                                  asserts this ordering in web_server.c.
#   6  product-mode.css            Operator vs engineering mode gating.
#   7  operator-operations.css     Alarms, events, history/trend widgets.
#   8  operator-product-suite.css  Operator product surfaces (--product-touch).
#   9  prelab-readiness.css        Pre-Lab Readiness page (minified, one line).
#  10  mobile-prelab-fixes.css     Touch/mobile hardening, incl. the S6 44x44
#                                  `pointer: coarse` hit-target block.
#  11  product-shell-v2.css        Shell V2 chrome (topbar, popovers, nav).
#  12  product-experience-v2.css   Experience V2 page composition.
#  13  commissioning-wizard-v2.css Commissioning wizard.
#  14  commissioning-release-v3.css Commissioning release flow (minified, one
#                                  line). Last layer, so it wins everything.
#
# Where to put a new rule:
#   - Restyling an existing component        -> edit that component's sheet.
#   - Colour of any kind                     -> use a token from app.css/theme.css.
#     NEVER hardcode a hex background next to a themed foreground: that is the
#     S5 defect class (docs/UI_VISUAL_AUDIT_2026-07-29.md) and it produced
#     1.10:1 and 1.17:1 contrast in the field. Every colour must resolve
#     correctly in BOTH themes.
#   - Undefined token names                  -> add the token, don't add a
#     `var(--x, #fff)` literal fallback; that fallback fires in both themes and
#     is what made the readiness page white-on-white (S5a).
#   - Layout/responsive tweak                -> theme.css or mobile-prelab-fixes.css.
#
# Do NOT create product-experience-v3.css. Adding a new sheet also requires
# edits to web_assets.c, include/web_assets.h and web_server.c, and every extra
# layer makes the !important arms race worse. Fix the rule where it lives.
# ---------------------------------------------------------------------------
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/index.html" "${CMAKE_CURRENT_BINARY_DIR}/index.html" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/app.css" "${CMAKE_CURRENT_BINARY_DIR}/app.css" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/theme.css" "${CMAKE_CURRENT_BINARY_DIR}/theme.css" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/product-mode.css" "${CMAKE_CURRENT_BINARY_DIR}/product-mode.css" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/operator-operations.css" "${CMAKE_CURRENT_BINARY_DIR}/operator-operations.css" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/operator-product-suite.css" "${CMAKE_CURRENT_BINARY_DIR}/operator-product-suite.css" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/prelab-readiness.css" "${CMAKE_CURRENT_BINARY_DIR}/prelab-readiness.css" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/mobile-prelab-fixes.css" "${CMAKE_CURRENT_BINARY_DIR}/mobile-prelab-fixes.css" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/product-shell-v2.css" "${CMAKE_CURRENT_BINARY_DIR}/product-shell-v2.css" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/product-experience-v2.css" "${CMAKE_CURRENT_BINARY_DIR}/product-experience-v2.css" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/commissioning-wizard-v2.css" "${CMAKE_CURRENT_BINARY_DIR}/commissioning-wizard-v2.css" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/commissioning-release-v3.css" "${CMAKE_CURRENT_BINARY_DIR}/commissioning-release-v3.css" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/wifi.css" "${CMAKE_CURRENT_BINARY_DIR}/wifi.css" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/devices.css" "${CMAKE_CURRENT_BINARY_DIR}/devices.css" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/em500.css" "${CMAKE_CURRENT_BINARY_DIR}/em500.css" COPYONLY)
# The one time-series chart component. Its sheet is concatenated last (see
# css_handler in web_server.c) so it can retire the superseded sparkline
# geometry without editing the sheets that declared it.
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/pvdg-chart.css" "${CMAKE_CURRENT_BINARY_DIR}/pvdg-chart.css" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/app.js" "${CMAKE_CURRENT_BINARY_DIR}/app.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/theme.js" "${CMAKE_CURRENT_BINARY_DIR}/theme.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/product-mode.js" "${CMAKE_CURRENT_BINARY_DIR}/product-mode.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/operator-view.js" "${CMAKE_CURRENT_BINARY_DIR}/operator-view.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/operator-network.js" "${CMAKE_CURRENT_BINARY_DIR}/operator-network.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/operator-proof.js" "${CMAKE_CURRENT_BINARY_DIR}/operator-proof.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/icons.js" "${CMAKE_CURRENT_BINARY_DIR}/icons.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/cards.css" "${CMAKE_CURRENT_BINARY_DIR}/cards.css" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/energy-flow.css" "${CMAKE_CURRENT_BINARY_DIR}/energy-flow.css" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/operator-operations.js" "${CMAKE_CURRENT_BINARY_DIR}/operator-operations.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/operator-product-suite.js" "${CMAKE_CURRENT_BINARY_DIR}/operator-product-suite.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/prelab-readiness.js" "${CMAKE_CURRENT_BINARY_DIR}/prelab-readiness.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/product-shell-v2.js" "${CMAKE_CURRENT_BINARY_DIR}/product-shell-v2.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/product-experience-v2.js" "${CMAKE_CURRENT_BINARY_DIR}/product-experience-v2.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/commissioning-wizard-v2.js" "${CMAKE_CURRENT_BINARY_DIR}/commissioning-wizard-v2.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/commissioning-release-v3.js" "${CMAKE_CURRENT_BINARY_DIR}/commissioning-release-v3.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/engineering-errors.js" "${CMAKE_CURRENT_BINARY_DIR}/engineering-errors.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/ui-enhancements.js" "${CMAKE_CURRENT_BINARY_DIR}/ui-enhancements.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/wifi-utils.js" "${CMAKE_CURRENT_BINARY_DIR}/wifi-utils.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/wifi-guard.js" "${CMAKE_CURRENT_BINARY_DIR}/wifi-guard.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/wifi.js" "${CMAKE_CURRENT_BINARY_DIR}/wifi.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/network-commissioning-fix.js" "${CMAKE_CURRENT_BINARY_DIR}/network-commissioning-fix.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/devices-utils.js" "${CMAKE_CURRENT_BINARY_DIR}/devices-utils.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/pvdg-chart.js" "${CMAKE_CURRENT_BINARY_DIR}/pvdg-chart.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/devices.js" "${CMAKE_CURRENT_BINARY_DIR}/devices.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/devices-refresh.js" "${CMAKE_CURRENT_BINARY_DIR}/devices-refresh.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/inverter-profiles.js" "${CMAKE_CURRENT_BINARY_DIR}/inverter-profiles.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/inverter-config.js" "${CMAKE_CURRENT_BINARY_DIR}/inverter-config.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/inverter-telemetry.js" "${CMAKE_CURRENT_BINARY_DIR}/inverter-telemetry.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/em500-utils.js" "${CMAKE_CURRENT_BINARY_DIR}/em500-utils.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/em500-core.js" "${CMAKE_CURRENT_BINARY_DIR}/em500-core.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/em500-quality.js" "${CMAKE_CURRENT_BINARY_DIR}/em500-quality.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/em500-profiles.js" "${CMAKE_CURRENT_BINARY_DIR}/em500-profiles.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/em500-plan.js" "${CMAKE_CURRENT_BINARY_DIR}/em500-plan.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/source-detection.js" "${CMAKE_CURRENT_BINARY_DIR}/source-detection.js" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/solar-grid.js" "${CMAKE_CURRENT_BINARY_DIR}/solar-grid.js" COPYONLY)

set(WEB_SERVER_C_SOURCES
    "web_server.c"
    "web_api.c"
    "device_api.c"
    "engineering_auth.c"
    "engineering_guard.c"
    "http_json.c"
    "operational_api.c"
    "alarm_journal.c"
    "alarm_metrics.c"
    "alarm_suppression.c"
    "inverter_profile_api.c"
    "inverter_config_api.c"
    "meter_config_api.c"
    "source_detection_api.c"
    "em500_cache.c"
    "em500_cache_adapter.c"
    "em500_cache_api.c"
    "meter_read_jobs.c"
    "em500_api.c"
    "em500_history_api.c"
    "em500_settings_api.c"
    "em500_settings_plan_api.c"
    "solar_grid_api.c"
    "solar_grid_status_api.c"
    # write_provenance.c is deliberately free of cJSON and ESP-IDF so the fleet
    # roll-up rule is executed by tests/write_provenance_test.c on the host
    # rather than asserted about. write_provenance_api.c is the cJSON side.
    "write_provenance.c"
    "write_provenance_api.c"
    "commissioning_gate_api.c"
    "system_resource_api.c"
    "audit_log_core.c"
    "audit_log.c"
    "audit_log_api.c"
    "web_assets.c"
)

# Sources that persist configuration. Each is compiled so that its calls to the
# persistence entry points are routed through the audit wrappers, which record
# the write and then call the real function. See the header comment in
# include/audit_log.h for why the audit point is here and not in each handler.
set(AUDIT_PERSISTENCE_SOURCES
    "web_api.c"
    "inverter_config_api.c"
    "meter_config_api.c"
    "solar_grid_api.c"
)

idf_component_register(
    SRCS ${WEB_SERVER_C_SOURCES}
    INCLUDE_DIRS "include"
    REQUIRES commissioning_gate meter_profiles esp_http_server esp_system esp_timer esp_hw_support esp_app_format esp_partition app_update spi_flash spiffs cjson nvs_flash mbedtls config_manager solar_grid_config source_detection network_manager meter_manager modbus_tcp inverter_manager control_engine safety_manager
    EMBED_TXTFILES
        "${CMAKE_CURRENT_BINARY_DIR}/index.html"
        "${CMAKE_CURRENT_BINARY_DIR}/app.css"
        "${CMAKE_CURRENT_BINARY_DIR}/theme.css"
        "${CMAKE_CURRENT_BINARY_DIR}/product-mode.css"
        "${CMAKE_CURRENT_BINARY_DIR}/operator-operations.css"
        "${CMAKE_CURRENT_BINARY_DIR}/operator-product-suite.css"
        "${CMAKE_CURRENT_BINARY_DIR}/prelab-readiness.css"
        "${CMAKE_CURRENT_BINARY_DIR}/mobile-prelab-fixes.css"
        "${CMAKE_CURRENT_BINARY_DIR}/product-shell-v2.css"
        "${CMAKE_CURRENT_BINARY_DIR}/product-experience-v2.css"
        "${CMAKE_CURRENT_BINARY_DIR}/commissioning-wizard-v2.css"
        "${CMAKE_CURRENT_BINARY_DIR}/commissioning-release-v3.css"
        "${CMAKE_CURRENT_BINARY_DIR}/wifi.css"
        "${CMAKE_CURRENT_BINARY_DIR}/devices.css"
        "${CMAKE_CURRENT_BINARY_DIR}/em500.css"
        "${CMAKE_CURRENT_BINARY_DIR}/pvdg-chart.css"
        "${CMAKE_CURRENT_BINARY_DIR}/app.js"
        "${CMAKE_CURRENT_BINARY_DIR}/theme.js"
        "${CMAKE_CURRENT_BINARY_DIR}/product-mode.js"
        "${CMAKE_CURRENT_BINARY_DIR}/operator-view.js"
        "${CMAKE_CURRENT_BINARY_DIR}/operator-network.js"
        "${CMAKE_CURRENT_BINARY_DIR}/operator-proof.js"
        "${CMAKE_CURRENT_BINARY_DIR}/icons.js"
        "${CMAKE_CURRENT_BINARY_DIR}/cards.css"
        "${CMAKE_CURRENT_BINARY_DIR}/energy-flow.css"
        "${CMAKE_CURRENT_BINARY_DIR}/operator-operations.js"
        "${CMAKE_CURRENT_BINARY_DIR}/operator-product-suite.js"
        "${CMAKE_CURRENT_BINARY_DIR}/prelab-readiness.js"
        "${CMAKE_CURRENT_BINARY_DIR}/product-shell-v2.js"
        "${CMAKE_CURRENT_BINARY_DIR}/product-experience-v2.js"
        "${CMAKE_CURRENT_BINARY_DIR}/commissioning-wizard-v2.js"
        "${CMAKE_CURRENT_BINARY_DIR}/commissioning-release-v3.js"
        "${CMAKE_CURRENT_BINARY_DIR}/engineering-errors.js"
        "${CMAKE_CURRENT_BINARY_DIR}/ui-enhancements.js"
        "${CMAKE_CURRENT_BINARY_DIR}/wifi-utils.js"
        "${CMAKE_CURRENT_BINARY_DIR}/wifi-guard.js"
        "${CMAKE_CURRENT_BINARY_DIR}/wifi.js"
        "${CMAKE_CURRENT_BINARY_DIR}/network-commissioning-fix.js"
        "${CMAKE_CURRENT_BINARY_DIR}/devices-utils.js"
        "${CMAKE_CURRENT_BINARY_DIR}/pvdg-chart.js"
        "${CMAKE_CURRENT_BINARY_DIR}/devices.js"
        "${CMAKE_CURRENT_BINARY_DIR}/devices-refresh.js"
        "${CMAKE_CURRENT_BINARY_DIR}/inverter-profiles.js"
        "${CMAKE_CURRENT_BINARY_DIR}/inverter-config.js"
        "${CMAKE_CURRENT_BINARY_DIR}/inverter-telemetry.js"
        "${CMAKE_CURRENT_BINARY_DIR}/em500-utils.js"
        "${CMAKE_CURRENT_BINARY_DIR}/em500-core.js"
        "${CMAKE_CURRENT_BINARY_DIR}/em500-quality.js"
        "${CMAKE_CURRENT_BINARY_DIR}/em500-profiles.js"
        "${CMAKE_CURRENT_BINARY_DIR}/em500-plan.js"
        "${CMAKE_CURRENT_BINARY_DIR}/source-detection.js"
        "${CMAKE_CURRENT_BINARY_DIR}/solar-grid.js"
)

foreach(source IN LISTS WEB_SERVER_C_SOURCES)
    if(NOT source STREQUAL "engineering_guard.c" AND NOT source STREQUAL "operational_api.c")
        set_source_files_properties(${source} PROPERTIES COMPILE_OPTIONS "-include;engineering_auth.h")
    endif()
endforeach()
set_source_files_properties("engineering_guard.c" "operational_api.c" PROPERTIES
    COMPILE_DEFINITIONS ENGINEERING_GUARD_IMPLEMENTATION=1)
# The audit wrappers must be visible before any header is parsed, because the
# substitution below also rewrites the declarations in config_manager.h and
# solar_grid_config.h. The wrappers are declared with identical signatures, so
# the rewritten declarations agree with them.
#
# audit_log*.c are excluded: they implement the wrappers and must reach the real
# persistence functions.
foreach(source IN LISTS AUDIT_PERSISTENCE_SOURCES)
    set_property(SOURCE ${source} APPEND PROPERTY COMPILE_OPTIONS "-include;audit_log.h")
    set_property(SOURCE ${source} APPEND PROPERTY COMPILE_DEFINITIONS
        config_manager_save=audit_config_manager_save
        solar_grid_config_save=audit_solar_grid_config_save)
endforeach()

set_property(SOURCE "em500_api.c" APPEND PROPERTY
    COMPILE_DEFINITIONS meter_manager_read_registers=em500_cache_read_pdu_registers)
set_property(SOURCE "em500_history_api.c" "em500_settings_api.c" "em500_settings_plan_api.c"
    APPEND PROPERTY COMPILE_DEFINITIONS meter_manager_read_registers=meter_read_jobs_cached_read)
