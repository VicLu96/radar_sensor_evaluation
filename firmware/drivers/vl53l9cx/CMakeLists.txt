# SPDX-License-Identifier: Apache-2.0

if(CONFIG_VL53L9CX)

  zephyr_include_directories(include)
  zephyr_library()

  # Our code: the platform binding and the Zephyr driver wrapper.
  zephyr_library_sources(
    vl53l9cx_platform.c
    vl53l9cx.c
  )

  # ST's driver, byte-identical to X-CUBE-53L9A1 v1.0.0 and BSD-3-Clause.
  # Vendored here (see st/PROVENANCE.md) so a clone builds without anyone
  # re-downloading a 39 MB package from st.com. Used unmodified so their
  # updates drop in cleanly and the boundary between our bugs and theirs
  # stays sharp.
  zephyr_library_include_directories(st)
  zephyr_library_sources(st/vl53l9.c)

  # ST's driver is third-party code held byte-identical; do not let our
  # warning settings force edits to it.
  set_source_files_properties(st/vl53l9.c
    PROPERTIES COMPILE_FLAGS "-Wno-unused-parameter")

endif()
