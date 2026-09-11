# Current State
Last updated: 2026-09-11

## Where the project actually is
**Stage 1 is done. The sensor ranges at full resolution on the custom board.**
54x42, 2268 zones, confirmed on Victor's bench 2026-09-11 — tag
`working-54x42-2026-09-11` (commit d83ad03). Everything before that tag was bring-up.

Next is **stage 2, BLE telemetry**, planned but not started. The plan is
`docs/plan/ble-streaming-and-web-ui.md`, reviewed by three expert agents on
2026-09-11 (`docs/plan/expert-review-2026-09-11.md`); their unactioned concerns are
in the TODO below.

## The hardware, as it is now
| | |
|---|---|
| Sensor | VL53L9CX at I2C 0x29 — optical dToF, **not radar** |
| MCU | ISP2454-LX (nRF54L15), `water_sense_board/nrf54l15/cpuapp`, NCS v3.3.0 |
| **AP_CLK** | **12 MHz, EXTERNAL CRYSTAL** (Victor, 2026-09-11). Not the SoC. The GRTC `clkout-fast` output is deleted in the application overlay |
| I2C | 400 kHz, SCL P1.08 / SDA P1.13, 4.7 kOhm external pull-ups on the host board |
| Other pins | INT P0.01 (active low), XSHUT P1.07, power enable P0.02, DVDD 1.2 V |
| IMU | LSM6DSV..BX at 0x6B — **fitted, and silent since 2026-09-10. Unexplained** |

Board files are Victor's: `firmware_test/boards/ethzurich/**` is hands-off unless he
asks in that message. Everything the driver needs lives in the application overlay
`firmware_test/boards/water_sense_board_nrf54l15_cpuapp.overlay`.

## What the bring-up cost, in one line each
- **The fix was `ext-clock-frequency = <12000000>`.** The board moved to a 12 MHz
  crystal while the devicetree still said 8 MHz. UM3683 2.5.2: that value configures
  *all* internal clocks, so a 1.5x error mistimed the optical pulse and the laser
  driver tripped its interlock (`ERROR_CODE 0x0F00`). Signal went 9 -> 126, ambient
  13 -> 1.
- Before that: `zephyr,concat-buf-size`/`flash-buf-max-size` at 16 bytes silently
  failed every 1026-byte firmware-blob chunk; `MAIN_STACK_SIZE` 1024 overflowed under
  immediate-mode logging; the RTT backend latches `host_present=false` and then drops
  everything in silence.

## Next session — TODO, in order
1. **Capture a full-resolution log and record the numbers.** Nothing from 54x42 is
   written down yet. The one that matters: **per-zone amplitude at binning 2 against
   the 126 measured at 24x20**. Binning 4 -> 2 is a quarter as many SPADs per zone, so
   ~32 is the prediction — this is the expert review's "does full resolution have any
   SNR at all" question, and it decides whether the paper's resolution axis is real.
   Also record valid-zone count, frame time, and whether saturation is still reported.
2. **Sweep exposure at 54x42**, upward from 4 ms, against valid-zone count. Record both.
3. **Record the laser-fault threshold exposure** if the intermittent
   `0x0F00` faults persist — the backoff prints it on every step.
4. **Turn off the two TEMPORARY switches**: `CONFIG_VL53L9CX_EXPOSURE_BACKOFF` and
   `CONFIG_VL53L9CX_HOLD_HFCLK`. Both make energy measurement meaningless.
5. **Start stage 2, BLE.** Custom services per the plan; counts only, never frames.
6. From the expert review, still unactioned:
   - **Strike "150 mW" from CLAUDE.md and the plan.** UM3683 Table 23 says
     **450-800 mW** for the ambient/outdoor profile. The figure the whole energy
     argument rests on is wrong by 3-5x.
   - The **9.6 m hard gate** (UM3683 2.6.1, fixed 64 ns ranging period) bounds corner
     tilt to >=~41 deg and coverage to ~10 m2.
   - Nothing in the counting algorithm splits merged people.
7. **Pull-ups**: 4.7 kOhm is already out of spec at 400 kHz (t_r = 0.8473*R*C vs the
   300 ns limit). 1 kOhm recommended. Victor's board change, and a precondition for
   Fast-mode Plus — which would halve the 334 ms full-resolution read.

## Open questions for Victor
- Ceiling height and room size of the test site. Sets the FoV footprint, and with the
  9.6 m gate it sets the mount angle.
- Can the board measure **sensor and MCU rails separately**? The paper claims a
  per-component breakdown; the Power Profiler measures one rail at a time.
- Paper deadline or venue? It changes the sequencing.
- The IMU at 0x6B: is it worth debugging, or is it out of scope now the bus is proven
  by the ToF sensor itself?

## Watch
The VL53L9CX shipped mid-2026. **The characterisation gap this paper occupies is open
because the part is new, and it will not stay open.**

## Hard rules that keep getting tested
- Every figure carries its **source and date**. A number without one does not go in.
- **`VERIFY` means not confirmed.** Never design against one silently.
- **Counts leave the device, frames never do.** The privacy claim is architectural.
- `DECISIONS.md` is **append-only**. This file is **rewritten** each session.
