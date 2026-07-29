cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")

# Report the commit, not `git describe`. Left to itself ESP-IDF picks the nearest
# tag, so the archive/* tags created when retiring old branches made the firmware
# announce itself as "archive/feature-pvdg-batch3-con..." - a released image must
# not be traceable to a tag that has nothing to do with it.
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    OUTPUT_VARIABLE PVDG_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT PVDG_GIT_COMMIT)
    set(PVDG_GIT_COMMIT "unknown")
endif()
set(PROJECT_VER "${PVDG_GIT_COMMIT}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(automatrix_pvdg)
