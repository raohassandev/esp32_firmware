cmake_minimum_required(VERSION 3.16)

# Compile the board-local screen as an isolated component without adding it to
# the production/default firmware project.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/..")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(waveshare_screen_qualification)
