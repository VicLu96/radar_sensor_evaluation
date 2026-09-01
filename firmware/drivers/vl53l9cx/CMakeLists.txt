# SPDX-License-Identifier: Apache-2.0

if(CONFIG_VL53L9CX)

  zephyr_include_directories(include)
  zephyr_library()

  # Our code: the platform binding and the Zephyr driver wrapper.
  zephyr_library_sources(
    vl53l9cx_platform.c
    vl53l9cx.c
  )

  # ST code: used unmodified so their updates drop in cleanly and the
  # boundary between our bugs and theirs stays sharp.
  set(ULD ${CMAKE_CURRENT_SOURCE_DIR}/${CONFIG_VL53L9CX_ULD_PATH})
  if(NOT EXISTS ${ULD})
    message(FATAL_ERROR
      "ST ULD sources not found at ${ULD}.\n"
      "Download X-CUBE-53L9A1 from st.com and place it there, or set "
      "CONFIG_VL53L9CX_ULD_PATH. The package is licensed and is deliberately "
      "not vendored into this repository.")
  endif()

  zephyr_library_include_directories(${ULD})
  # VERIFY the actual filenames against the downloaded package.
  zephyr_library_sources(${ULD}/vl53l9cx_api.c)

endif()
