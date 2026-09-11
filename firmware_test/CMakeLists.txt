cmake_minimum_required(VERSION 3.20.0)

# The VL53L9CX driver is an out-of-tree Zephyr module, kept inside this project
# so the whole thing is self-contained: one directory to build, flash and hand
# to someone else. Added before find_package(Zephyr), which is when modules are
# resolved.
list(APPEND EXTRA_ZEPHYR_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/drivers/vl53l9cx)

# water_sense_board lives in this project, so point Zephyr at it from here
# rather than expecting -DBOARD_ROOT on the command line. That makes the build
# work unchanged from the nRF Connect VS Code extension, where extra CMake
# arguments are an easy thing to set once and then forget you set.
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# Firmware version, stamped from git at every build.
#
# Exists because on 2026-09-11 a bench log could not be matched to a commit, and
# the whole session turned on which build was flashed. A version the board
# prints itself is the only kind that cannot be wrong.
execute_process(
  COMMAND git describe --tags --always --dirty
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE APP_GIT_DESCRIBE
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT APP_GIT_DESCRIBE)
  set(APP_GIT_DESCRIBE "unknown")
endif()
add_compile_definitions(APP_GIT_VERSION="${APP_GIT_DESCRIBE}")
message(STATUS "Firmware version: ${APP_GIT_DESCRIBE}")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(firmware_test)

target_sources(app PRIVATE src/main.c)
