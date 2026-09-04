# SPDX-License-Identifier: Apache-2.0

# VERIFY: matches the debugger actually used. nrfutil is the current Nordic
# default; swap for nrfjprog if that is what is installed.
board_runner_args(nrfutil "--nrf-family=NRF54L")
board_runner_args(jlink "--device=nRF54L15_M33" "--speed=4000")

include(${ZEPHYR_BASE}/boards/common/nrfutil.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
