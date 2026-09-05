# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

# SUPERSEDED SCAFFOLDING. This application and firmware/boards/pbl/vl53l9_node
# were written before the real board existed, and both are replaced by
# firmware_test on water_sense_board. Kept only because the staged bring-up
# gates in src/main.c are still the plan for the ToF work; the driver it points
# at now lives inside firmware_test.
list(APPEND EXTRA_ZEPHYR_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../../firmware_test/drivers/vl53l9cx)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(vl53l9cx_bringup)

target_sources(app PRIVATE src/main.c)
