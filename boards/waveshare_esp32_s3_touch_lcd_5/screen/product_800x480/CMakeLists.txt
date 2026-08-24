cmake_minimum_required(VERSION 3.16)

# Dedicated Waveshare product build. The site-tested root/default project stays
# unchanged; this project explicitly composes the shared Product Core with the
# board-local screen component.
get_filename_component(PVDG_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(EXTRA_COMPONENT_DIRS
    "${PVDG_ROOT}/components"
    "${CMAKE_CURRENT_LIST_DIR}/..")

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${PVDG_ROOT}"
    OUTPUT_VARIABLE PVDG_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT PVDG_GIT_COMMIT)
    set(PVDG_GIT_COMMIT "unknown")
endif()
set(PROJECT_VER "${PVDG_GIT_COMMIT}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(automatrix_pvdg_waveshare_800x480)
