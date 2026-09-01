# VL53L9CX — sensor reference

Researched 2026-08-31. Every figure here carries its source. Anything marked
**VERIFY** was not confirmed and must not be designed against until it is.

> **It is not a radar.** The VL53L9CX is a *direct time-of-flight optical* sensor — a
> SPAD array with a 940 nm VCSEL illuminator. This matters beyond pedantry: it fixes
> the failure modes (sunlight, glass, dark/absorbing surfaces, cover-glass crosstalk),
> and it means the relevant literature is depth/dToF occupancy sensing, **not** mmWave
> radar people-counting. The repository name is historical.

## What it is

ST's highest-resolution FlightSense part, announced 2026 — an all-in-one dToF LiDAR
module with the VCSEL, SPAD array and processing in one 12.8 × 6.1 × 4.6 mm package.
Full part number in use: `VL53L9CXV0VE/1`.

| Parameter | Value | Source |
|---|---|---|
| Resolution | up to **54 × 42 = 2268 zones** ("2.3K") | ST product page |
| Field of view | 71° diagonal (54° × 42°), **1° angular resolution** | ST |
| Range | **< 5 cm to 8.8 m** | ST |
| Max frame rate | **100 Hz** | ST |
| Typical system power | **150 mW** | ST |
| Outputs | depth, 2D IR (active), 2D IR (ambient), reflectance, confidence | ST |
| Host interface | **I²C and MIPI I3C** on shared SDA/SCL; I3C to 12.5 MHz after DAA | ST datasheet |
| Package | 12.8 × 6.1 × 4.6 mm | ST |

## The three facts that shape this project

**1. 150 mW is a lot.** For scale, the nRF54L15 in a BLE connection averages
low-single-digit mA. The *sensor* is the power budget, not the MCU — so the whole
firmware design is about keeping it off, not about optimising the radio. Anything that
reduces active sensor time by 2× roughly doubles battery life; shaving MCU cycles does
almost nothing. **Design accordingly.**

**2. Six resolution modes — this is the power lever** *(source-verified 2026-08-31)*.

Confirmed from a hardware-validated driver's register map (see Sources). Each mode is
an on-device binning factor; the square formats transmit a square array with a crop
offset applied on-device.

| Mode | Zones | Binning | Transmitted | Frame bytes | @400 kHz | @1 MHz |
|---|---|---|---|---|---|---|
| **54×42** | 2268 | 2 | 54×42 | **~13.6 KB** | **~370 ms** | ~148 ms |
| **24×20** | 480 | 4 | 24×24 (crop y+2) | ~3.5 KB | ~86 ms | ~35 ms |
| **18×14** | 252 | 6 | 18×14 | ~1.5 KB | ~38 ms | ~15 ms |
| **12×10** | 120 | 8 | 12×10 | ~0.7 KB | ~18 ms | ~7 ms |
| **8×6** | 48 | 12 | 8×8 (crop y+1) | ~0.4 KB | ~10 ms | ~4 ms |
| **4×4** | 16 | 24 | 4×4 | ~96 B | **~2.4 ms** | ~1 ms |

**That is a 142× span in zones and a ~150× span in bus time.** The energy-accuracy
curve the paper needs has six real points across two orders of magnitude — the single
best piece of news for the project.

**Frame format:** three `uint16` per zone — **depth, amplitude, ambient** — so **6 bytes
per zone**, plus a status line. Depth is **15-bit millimetres with a VALID flag in bit
15**; a zero flag means no or bad measurement and the value must not be used.

At full resolution the **bus, not the sensor, caps the frame rate** at roughly 2.7 fps
(400 kHz) — well below the sensor's 100 Hz. Fine for a ~1 Hz counter, but it belongs in
the paper rather than being discovered late. At 4×4 the bus is irrelevant.

**3. The firmware blob is small — and this reverses the earlier conclusion.**

**9,865 bytes**, patch version 0.17, extracted byte-for-byte from ST's
X-CUBE-53L9A1 v1.0.0 (`vl53l9_patch.h`, `g_vl53l9_fw_patch[]`) and redistributed
BSD-3-Clause. Loaded to device RAM after **every power or XSHUT cycle**.

At 400 kHz that is **~250 ms** — *about the cost of a single full-resolution frame
read*, not the ~2 s extrapolated from the VL53L8CX's 84 KB blob.

**This inverts the power strategy in the favourable direction.** A cold start is cheap
enough that fully powering the sensor domain down between readings is likely to beat
holding it in standby at any duty period beyond a fraction of a second — the opposite of
the earlier assumption. See the crossover in
[`../plan/implementation.md`](../plan/implementation.md).

## Failure modes to design and test against

Optical dToF, not radar — so:

- **Ambient sunlight** raises the noise floor and cuts effective range. The ambient-IR
  output exists partly to let you detect this. Test near windows.
- **Cover glass** introduces crosstalk that must be calibrated out. If the enclosure
  has a window, calibration is not optional.
- **Dark clothing and hair** absorb 940 nm strongly — the classic dToF failure for
  people sensing, and the reason reflectance output matters.
- **Glass and mirrors** produce phantom returns.
- **Multipath** in corridors and near walls.

## Resolved 2026-08-31 — was VERIFY, now source-verified

| Was unknown | Answer | Source |
|---|---|---|
| Firmware blob size | **9,865 bytes**, patch v0.17 | Extraction from X-CUBE-53L9A1 v1.0.0 |
| Reduced-resolution modes | **Six**: 54×42, 24×20, 18×14, 12×10, 8×6, 4×4 | Driver register map |
| Per-zone data layout | **6 bytes** — depth, amplitude, ambient as `uint16` | Driver frame parser |
| I²C address | **0x29 (7-bit)** — 0x52 is the 8-bit form. ⚠ Re-opened 2026-09-01: ST's own code passes 0x52 into a 7-bit HAL field. Scan at bring-up | Community driver constant / ST `VL53L9_DEFAULT_ADDRESS` |
| Register index width | **16-bit** | Driver register addresses (e.g. `0xD208`) |

## Resolved 2026-09-01 — from X-CUBE-53L9A1 v1.0.0 itself

| Was unknown | Answer | Source |
|---|---|---|
| Exact frame size, all six modes | 14,842 / 3,844 / 1,738 / 880 / 516 / 204 bytes | `vl53l9.c:65-84` |
| Fixed per-frame overhead | **100-byte status line** on every frame, any mode | `VL53L9_STATUS_SIZE` |
| Does binning preserve the FoV | Yes within the wide family, no across families | `vl53l9_set_binning()` |
| Sensor power modes | Three: `REGULAR`, `LOW`, `ULTRA_LOW` | `vl53l9_power_mode_t` |
| Single-shot capture | Supported — `trigger_frame()` + `poll_frame()` | `vl53l9.h` |
| Two on-device configurations | `CONTEXT_SHORT` / `CONTEXT_LONG`, independent binning and exposure | `vl53l9_set_context()` |
| Per-frame health telemetry | 8 error bits: VHV, SPAD supply, HV boost, PLL lock, ref array, internal FW | `vl53l9_status_t` |
| Factory calibration | **2,332 bytes**, retrievable via `vl53l9_get_calib_data()` | `VL53L9_CALIB_DATA_SIZE` |

## Still open — **VERIFY before designing against**

1. **Standby current, and whether the blob survives standby.** Now the main unknown in
   the power model — though with a 250 ms reload, full power-down is cheap enough that
   standby may simply not be worth using.
2. **Maximum I²C clock the part accepts** (400 kHz vs 1 MHz). Halves or doubles every
   figure in the table above.
3. **Whether the 150 mW figure is at 100 Hz full resolution**, and how it scales with
   binning. The datasheet quotes one number; the paper needs the curve.
4. **Integration time per mode** — the other half of the frame period, alongside bus
   time. At 4×4 the bus is negligible and integration dominates.
5. ~~**Does binning change the field of view or only the sampling within it?**~~
   **Answered 2026-09-01** from `vl53l9_set_binning()` in ST's driver. Field preserved
   within the *wide* family — 54×42, 18×14, 12×10 — which merges zones with no crop.
   The *square* family — 24×20, 8×6, 4×4 — transmits a square array with an on-device
   crop window and does **not** cover the same vertical field. The paper's central
   curve therefore uses binning 2 / 6 / 8 only. See
   [st-package-audit.md](../plan/st-package-audit.md) §4.

## Software

- **X-CUBE-53L9A1** — ST's STM32Cube expansion package for this part. Note this is a
  *different* package from `X-CUBE-TOF1`, which covers the older parts (VL53L5CX,
  VL53L8CX, …) and **does not include the VL53L9CX**.
- **UM3655** — getting started with the STEVAL-VL53L9 board.
- **UM3656** — getting started with X-CUBE-53L9A1.
- **STSW-IMG052** — the GUI for X-NUCLEO-53L9A1; reported to 404 on ST's site and may
  need to be requested from ST directly.
### Community drivers — cloned to `vendor/`, and the best reference we have

**ST's own package cannot be fetched automatically**: X-CUBE-53L9A1 sits behind a
licence acceptance on st.com, which is Victor's click to make, not something to
automate. These two fill the gap in the meantime and are BSD-3-Clause.

- **`VanBruce/vl53l9cx-python`** — pure-Python **I²C** driver, hardware-validated.
  **Our exact transport**, and the source of every figure resolved above: the register
  map, the six binning modes, the frame layout, and the firmware patch itself
  (`vl53l9cx_fw_patch.bin`, 9,865 bytes, extracted from ST's package with documented
  provenance and SHA-256). **Read this before writing the Zephyr port** — it is a
  working implementation of exactly what we are building.
- **`earlynerd/VL53L9-Arduino`** (RP2040/RP2350) — I3C transport via PIO, plus ST driver
  init and frame reads. Different transport, same init ordering.

Both are cloned into `vendor/` (gitignored). ST's official package is still worth
downloading — it is the authority, and the community work is a port of it — but it is no
longer blocking.

## Evaluation boards worth having

- **STEVAL-VL53L9** — for STM32 Nucleo-N657X0 / N6570-DK.
- **X-NUCLEO-53L9A1** — Nucleo expansion board.

Either gives a known-good reference to compare our port against, which is worth a great
deal when a sensor bring-up goes silent.

## Sources

- [ST VL53L9CX product page](https://www.st.com/en/imaging-and-photonics-solutions/vl53l9cx.html)
- [VL53L9CX datasheet](https://www.st.com/resource/en/datasheet/vl53l9cx.pdf)
- [VL53L9CX data brief](https://www.st.com/resource/en/data_brief/vl53l9cx.pdf)
- [ST press release](https://newsroom.st.com/media-center/press-item.html/p4783.html)
- [CNX Software coverage](https://www.cnx-software.com/2026/06/22/st-vl53l9cx-direct-time-of-flight-3d-lidar-supports-5cm-to-9m-range-2-3k-zones-resolution/)
- [UM3655 STEVAL-VL53L9](https://www.st.com/resource/en/user_manual/um3655-getting-started-with-the-stevalvl53l9-board-based-on-the-vl53l9cx-for-stm32-nucleo-n657x0-and-n6570dk-stmicroelectronics.pdf)
- [UM3656 X-CUBE-53L9A1](https://www.st.com/resource/en/user_manual/um3656-getting-started-with-xcube53l9a1-direct-tof-lidar-sensor-software-expansion-for-stm32cube-stmicroelectronics.pdf)
- [VL53L9-Arduino community port](https://github.com/earlynerd/VL53L9-Arduino)
