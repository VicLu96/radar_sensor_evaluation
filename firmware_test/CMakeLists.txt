cmake_minimum_required(VERSION 3.20.0)

# The VL53L9CX driver is an out-of-tree Zephyr module living in this same
# repository. Added before find_package(Zephyr) because that is when modules are
# resolved.
list(APPEND EXTRA_ZEPHYR_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/drivers/vl53l9cx)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(firmware_test)

target_sources(app PRIVATE src/main.c)
