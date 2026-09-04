# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

# The VL53L9CX driver is an out-of-tree Zephyr module, and the board lives in
# this repository too. Both are found relative to this file so a build works
# from a clean west workspace with no environment fiddling.
list(APPEND EXTRA_ZEPHYR_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../drivers/vl53l9cx)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(vl53l9cx_bringup)

target_sources(app PRIVATE src/main.c)
