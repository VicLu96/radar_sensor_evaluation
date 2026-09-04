# SPDX-License-Identifier: Apache-2.0

# nrfjprog does not support the nRF54L family; nrfutil replaces it. J-Link is
# kept because the RTT console this board uses for output needs it anyway.
board_runner_args(jlink "--device=nRF54L15_M33" "--speed=4000")
board_runner_args(nrfutil "--nrf-family=NRF54L")

include(${ZEPHYR_BASE}/boards/common/nrfutil.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
