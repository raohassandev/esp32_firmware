# Waveshare local-screen component definition.
#
# This directory is intentionally NOT added to root EXTRA_COMPONENT_DIRS yet.
# The current site-tested firmware build therefore remains unchanged until the
# Waveshare display/LVGL dependency set and exact board target are qualified.
# When the board build gate is opened, this component can be added explicitly.

idf_component_register(
    SRCS
        "api/screen_api.c"
        "pages/overview_screen.c"
    INCLUDE_DIRS
        "api"
        "pages"
    REQUIRES
        cjson
        lvgl
)
