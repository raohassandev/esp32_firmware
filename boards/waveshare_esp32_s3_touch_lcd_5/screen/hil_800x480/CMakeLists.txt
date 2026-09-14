cmake_minimum_required(VERSION 3.16)

# Exact-target hardware bring-up project for Waveshare ESP32-S3-Touch-LCD-5
# (800x480). It consumes the board-local screen component without wiring that
# component into the site-tested root/default firmware project.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/..")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(waveshare_screen_hil_800x480)
