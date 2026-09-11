# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

# water_sense_board lives in firmware_test/, the same as for the real
# application, so this builds from the nRF Connect VS Code extension with no
# extra CMake arguments.
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../firmware_test)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ble_beacon)

target_sources(app PRIVATE src/main.c)
