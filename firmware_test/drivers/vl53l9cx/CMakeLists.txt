# SPDX-License-Identifier: Apache-2.0

if(CONFIG_VL53L9CX)

  zephyr_include_directories(include)

  # NAMED, not zephyr_library().
  #
  # zephyr_library() derives the library name from this directory's path
  # relative to the build, and this module lives outside the application tree —
  # so the name comes out as
  #   ..__..__..__Users__luder__Documents__Claude_File_System__...__vl53l9cx
  # and CMake uses it verbatim as an object-file directory. That is 113
  # characters of directory name, and on Windows it pushed the dependency-file
  # path to 262 characters against a 260-character limit: over by two.
  #
  # The failure does not mention path length. It is
  #   "fatal error: opening dependency file ...: No such file or directory"
  # which reads as a missing file or a broken toolchain.
  #
  # Naming the library explicitly costs nothing and takes the same path to 161.
  zephyr_library_named(vl53l9cx)

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
  zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/st)
  zephyr_library_sources(${CMAKE_CURRENT_SOURCE_DIR}/st/vl53l9.c)

  # ST's driver is third-party code held byte-identical; do not let our
  # warning settings force edits to it.
  set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/st/vl53l9.c
    PROPERTIES COMPILE_FLAGS "-Wno-unused-parameter")

endif()
