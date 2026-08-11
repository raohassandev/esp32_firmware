# ---------------------------------------------------------------------------
# Web assets
#
# index.html links exactly one stylesheet, /app.css, and one script, /app.js.
# Both are built and gzipped by tools/build_bundle.py further down this file,
# from the lists in web/app.css.order and web/app.js.order.
#
# THOSE TWO FILES ARE THE ONLY PLACE THE ORDER LIVES. It used to live here as
# well, in a deliberately different order from the assets[] arrays in
# web_server.c, with a third copy in a JSON file for the preview -- and a long
# comment in this position explaining that moving a line here changed nothing.
# Three lists meant three chances to disagree, and a disagreement is invisible
# until a page silently stops working.
#
# The cascade rules that comment described have not changed and still matter;
# they are recorded where the order now is, in web/app.css.order:
#
#   - Per-feature sheets come BEFORE theme.css, so the light-theme token
#     overrides can correct them. tests/ui_runtime_consistency_source_contract.py
#     asserts that, now against the order file.
#   - cards.css is the shared layer the per-module panels are being converted to,
#     so nothing it replaces may follow it.
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
#
# Adding a module is now ONE edit: append it to the right .order file. It used to
# take four -- this file, web_assets.c, include/web_assets.h and web_server.c --
# and forgetting any one of them produced a page that loaded and did nothing.
# ---------------------------------------------------------------------------
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/index.html" "${CMAKE_CURRENT_BINARY_DIR}/index.html" COPYONLY)
# The one time-series chart component. Its sheet is concatenated last (see
# css_handler in web_server.c) so it can retire the superseded sparkline
# geometry without editing the sheets that declared it.

set(WEB_SERVER_C_SOURCES
    "web_server.c"
    "web_api.c"
    "device_api.c"
    "live_api.c"
    "meter_json.c"
    "inverter_json.c"
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


# ---------------------------------------------------------------------------
# The bundles the browser actually receives, built and COMPRESSED here.
#
# The controller served 873 KB of JavaScript uncompressed over Wi-Fi from an
# ESP32 -- about 27 seconds from the board before the interface appeared, which
# is long enough that people conclude it has crashed and reload, making it worse.
# The same bytes gzipped are a quarter of that.
#
# Compressed at BUILD time, not on the device: an ESP32 compressing 873 KB on
# every page load would spend CPU the control loop needs, on work whose answer
# cannot change between builds.
#
# Order comes from web/app.js.order and web/app.css.order, which are the source
# of truth. It used to live in three places -- the assets[] arrays in
# web_server.c, this file in a different order, and a JSON copy for the preview
# -- and three lists mean three chances to disagree, with the disagreement
# invisible until a page silently stops working.
#
# Configure-time rather than a build rule, because EMBED_FILES needs the outputs
# to exist when idf_component_register runs. The order files and every web
# source are registered below as configure dependencies, so editing any of them
# regenerates the bundle.
set(WEB_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../web")

# Skipped during ESP-IDF's early-expansion pass, which re-reads every component
# file only to collect requirements. Most build properties -- the Python
# interpreter among them -- are not set there, so running the generator would
# invoke an empty command and fail the build for a reason unrelated to the code.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    file(GLOB WEB_BUNDLE_INPUTS
        "${WEB_SOURCE_DIR}/*.js" "${WEB_SOURCE_DIR}/*.css" "${WEB_SOURCE_DIR}/*.order")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${WEB_BUNDLE_INPUTS})
    # Editing any web module or either order file re-runs the generator above.

    # The interpreter the build itself uses, not whatever "python" happens to
    # resolve to on the developer's PATH -- which on Windows is frequently
    # nothing at all, and the resulting failure names the bundle rather than the
    # missing interpreter.
    idf_build_get_property(bundle_python PYTHON)
    if(NOT bundle_python)
        set(bundle_python "python")
    endif()

    execute_process(
        COMMAND "${bundle_python}"
                "${CMAKE_CURRENT_LIST_DIR}/../../tools/build_bundle.py"
                "${CMAKE_CURRENT_BINARY_DIR}"
        RESULT_VARIABLE WEB_BUNDLE_RESULT
        OUTPUT_VARIABLE WEB_BUNDLE_OUTPUT
        ERROR_VARIABLE WEB_BUNDLE_ERROR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE)
    if(NOT WEB_BUNDLE_RESULT EQUAL 0)
        # Fatal. A build that shrugs this off links a firmware whose interface is
        # an empty page, and nothing else in the build would report it.
        message(FATAL_ERROR
            "web bundle build failed (${WEB_BUNDLE_RESULT}): "
            "${WEB_BUNDLE_OUTPUT} ${WEB_BUNDLE_ERROR}")
    endif()
    message(STATUS "web bundle: ${WEB_BUNDLE_OUTPUT}")
endif()

idf_component_register(
    SRCS ${WEB_SERVER_C_SOURCES}
    INCLUDE_DIRS "include"
    REQUIRES commissioning_gate meter_profiles esp_http_server esp_system esp_timer esp_hw_support esp_app_format esp_partition app_update spi_flash spiffs cjson nvs_flash mbedtls config_manager solar_grid_config source_detection network_manager meter_manager modbus_tcp inverter_manager control_engine safety_manager
    EMBED_FILES
        # The two bundles, each in both forms. Both are kept because every
        # browser sends Accept-Encoding: gzip but a commissioning script written
        # with plain curl does not, and handing that script gzip bytes it never
        # asked for produces a file of binary garbage during a site visit.
        "${CMAKE_CURRENT_BINARY_DIR}/app_bundle.js"
        "${CMAKE_CURRENT_BINARY_DIR}/app_bundle.js.gz"
        "${CMAKE_CURRENT_BINARY_DIR}/app_bundle.css"
        "${CMAKE_CURRENT_BINARY_DIR}/app_bundle.css.gz"
    EMBED_TXTFILES
        "${CMAKE_CURRENT_BINARY_DIR}/index.html"
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
