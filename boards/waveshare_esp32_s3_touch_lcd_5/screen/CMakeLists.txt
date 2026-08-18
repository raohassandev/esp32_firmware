# Waveshare local-screen component definition.
#
# This directory is intentionally NOT added to root EXTRA_COMPONENT_DIRS yet.
# The current site-tested firmware build therefore remains unchanged until the
# Waveshare display/LVGL dependency set and exact board target are qualified.
# When the board build gate is opened, this component can be added explicitly.

idf_component_register(
    SRCS
        "api/screen_api.c"
        "components/screen_widgets.c"
        "drivers/waveshare_display_profile.c"
        "pages/overview_screen.c"
        "pages/grid_screen.c"
        "pages/solar_screen.c"
        "pages/alarms_screen.c"
        "pages/readiness_screen.c"
        "screen_app.c"
        "screen_runtime.c"
    INCLUDE_DIRS
        "."
        "api"
        "components"
        "drivers"
        "pages"
    REQUIRES
        cjson
        lvgl
)
