# ST VL53L9 driver — provenance

**Do not edit these files.** They are ST's, byte-identical to the release, and the value
of that is that ST updates drop in and the line between "our bug" and "their bug" stays
sharp. All adaptation happens in `../vl53l9cx_platform.c` and `../vl53l9cx.c`.

| | |
|---|---|
| Source | X-CUBE-53L9A1 / `STM32CubeExpansion_53L9A1_V1.0.0` |
| Path in package | `Drivers/BSP/Components/vl53l9/` |
| Copied | 2026-09-01 |
| Driver version | `VL53L9_CORE` 1.0.0 (`vl53l9.h`) |
| Firmware patch | v0.17, 9,865 bytes (`vl53l9_patch.h`) |
| License | **BSD-3-Clause** per the package SBOM (`Package_license.md`, "BSP Components") |
| Copyright | © 2026 STMicroelectronics, retained in every file header |

The full package is kept locally at `vendor/x-cube-53l9a1/` and is gitignored — it is
39 MB, most of it STM32H5 HAL and CMSIS that this project never builds, plus
SLA-licensed middleware and demo projects that are deliberately not redistributed.

To update: drop in the new release's `Drivers/BSP/Components/vl53l9/`, re-check
`vl53l9_platform.h` for signature changes, re-check the patch version against whatever
the driver enforces, and record the change in `DECISIONS.md` with the new package
version.

See `docs/plan/st-package-audit.md` for what this API provides and what it changed.
