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

**2. I²C makes the frame the bottleneck** *(Victor's decision, 2026-08-31)*.
2268 zones × ~4 bytes (distance + status, minimum useful) ≈ **9 KB per frame**, and
considerably more if IR or reflectance planes are pulled.

| I²C clock | Effective throughput | Time for one 9 KB frame |
|---|---|---|
| 400 kHz | ~40 KB/s | **~225 ms** |
| 1 MHz | ~100 KB/s | **~90 ms** |

So the interface, not the sensor, caps you at roughly **4–10 fps** at full resolution.
That is fine — the application wants ~1 Hz — but it must be stated in the paper rather
than discovered late. It also means **the bus is a significant share of the energy per
frame**, which makes reduced-resolution modes doubly valuable.

**3. The firmware blob is a cold-start tax.** These parts load a firmware image over
the bus at every power-on. The comparable VL53L8CX blob is **~84 KB**; the VL53L9CX's
is **VERIFY** but is very unlikely to be smaller given 35× the zones.

At 400 kHz, 84 KB is **~2.1 s of bus activity before the first measurement** — and
proportionally more if the blob is larger. **This inverts the naive power strategy:**
fully power-cycling the sensor between readings would cost more energy in re-boot than
it saves in idle. The design must use a standby state that *retains* the firmware, and
the reboot cost must be measured early because it sets the minimum useful duty period.

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

## Open questions — **VERIFY before designing against**

1. **Firmware blob size** and load time over I²C. Sets the cold-start energy and the
   minimum sensible duty cycle. Highest-priority unknown.
2. **Reduced-resolution modes.** The VL53L5CX offers 4×4 and 8×8; the VL53L9CX almost
   certainly offers sub-modes of 54×42, but the exact grid options and their frame
   rates are unconfirmed. **This is the main power lever** — the energy-accuracy curve
   in the paper depends on it.
3. **Standby / low-power state current**, and whether firmware is retained across it.
4. **Maximum I²C clock the part accepts** (400 kHz vs 1 MHz) — doubles or halves the
   per-frame bus cost.
5. **Per-zone data layout and actual bytes per frame** for each output combination.
6. Whether the 150 mW figure is at 100 Hz full resolution, and how it scales down.

## Software

- **X-CUBE-53L9A1** — ST's STM32Cube expansion package for this part. Note this is a
  *different* package from `X-CUBE-TOF1`, which covers the older parts (VL53L5CX,
  VL53L8CX, …) and **does not include the VL53L9CX**.
- **UM3655** — getting started with the STEVAL-VL53L9 board.
- **UM3656** — getting started with X-CUBE-53L9A1.
- **STSW-IMG052** — the GUI for X-NUCLEO-53L9A1; reported to 404 on ST's site and may
  need to be requested from ST directly.
- **Community port:** `github.com/earlynerd/VL53L9-Arduino` (RP2040/RP2350) reports
  working I3C transport, ST driver init, firmware patch upload and raw frame reads.
  Useful as a reference for the init sequence even though our transport is I²C.

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
