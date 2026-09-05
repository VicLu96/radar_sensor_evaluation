# ST reference platform port — provenance

**Reference only. Nothing here is built.** This is ST's own implementation of the
platform layer, for an STM32H5 over the I3C peripheral. It is here because our
`vl53l9cx_platform.c` is the Zephyr equivalent of it, and on a board with no known-good
reference, having ST's version to diff against is worth the 56 KB.

| | |
|---|---|
| Source | X-CUBE-53L9A1 / `STM32CubeExpansion_53L9A1_V1.0.0` |
| Path in package | `Utilities/vl53l9-common/` |
| Copied | 2026-09-01 |
| License | **BSD-3-Clause** per the package SBOM (`Package_license.md`, "Utilities - vl53l9-common") |
| Copyright | © 2026 STMicroelectronics |

## What to read, and why

- **`vl53l9/vl53l9_platform.c`** — the thirteen platform functions. Note that despite
  the `HAL_I3C_*` calls, every transfer uses the peripheral's **legacy I²C
  private-transfer** mode, so the wire protocol is ordinary I²C and maps onto Zephyr's
  `i2c_transfer_dt()`. Read this before writing ours.
- **`vl53l9_interface.h`** — `vl53l9_device_t` is ST's statement of what a board must
  supply: bus, address, VDDA, VDDIO, external clock, XSHUT, interrupt. The devicetree
  binding mirrors it.
- **`vl53l9/vl53l9_device.c`** — how ST populates that struct for one sensor.
- **`vl53l9/vl53l9_utils.c`** — frame unpacking helpers, useful for stage 2.

Web-page assets (`_htmresc/`) and `Release_Notes.html` from the original directory are
not copied.
