# Plan: LSM6DSV..BX IMU over I²C

Written 2026-09-04. **Plan only — no code written yet, at Victor's instruction.**

Goal: read accelerometer X/Y/Z and print them over the RTT log, to prove the IMU and the
I²C bus work.

---

## What the SDK actually has, and the bad news

I checked NCS v3.3.0 rather than assuming. Three findings, in increasing order of how
much they matter.

**1. There is no Zephyr driver for this part.** The in-tree ST drivers are `lsm6ds0`,
`lsm6dsl`, `lsm6dso`, `lsm6dso16is`, `lsm6dsv16x`, and a family driver `lsm6dsvxxx`
covering `lsm6dsv320x`, `lsm6dsv80x` and `ism6hg256x`. No `..BX` variant among them.

**2. The closest driver would reject the part.** `lsm6dsv16x` checks WHO_AM_I, and the
IDs differ:

| | WHO_AM_I |
|---|---|
| `LSM6DSV16X_ID` | **0x70** |
| `LSM6DSV16BX_ID` | **0x71** |

**3. And loosening that check would not be enough** — this is the finding that settles
the approach. The register maps are close but not identical:

| Register | 16X | 16BX |
|---|---|---|
| `WHO_AM_I` | 0x0F | 0x0F |
| `CTRL1` | 0x10 | 0x10 |
| **`OUTX_L_A`** | **0x28** | **0x2C** |

The accelerometer output block moved. A driver told to ignore the ID would read four
bytes off and return plausible-looking nonsense — the worst failure mode available, and
exactly the class of bug this project has already been bitten by twice.

**What does exist:** ST's own HAL for the part, in-tree at
`modules/hal/st/sensor/stmemsc/lsm6dsv16bx_STdC/`. It has no `USE_STDC_LSM6DSV16BX`
Kconfig symbol, so it is present but not wired into the Zephyr build. That is a small
gap to bridge, not a missing dependency.

## First, a part-number question

The stmemsc HAL ships `lsm6dsv16b_STdC` and `lsm6dsv16bx_STdC`. It does **not** ship a
`lsm6dsv15bx`. Before anything is written: **what is the actual marking on the package,
and what does the schematic BOM say?** `LSM6DSV16BXTR` and `LSM6DSV15BXTR` differ by one
character and the plan branches on it — if it is genuinely a 15BX, ST's HAL for it is not
in this SDK either and it has to come from st.com, exactly as X-CUBE-53L9A1 did.

Everything below assumes the register map matches the 16BX. The very first log line will
confirm or destroy that assumption, which is why stage A reads WHO_AM_I before anything
else.

---

## Stage A — prove it is alive, with no driver at all

**This is what to do first, and it is about sixty lines in the existing test app.**

Everything it needs is now known. The part marking is the one open question, and stage A
does not need it — stage A is what *answers* it.

No Zephyr sensor driver, no Kconfig, no out-of-tree module. Talk to the device directly
with `i2c_write_read_dt()` on the existing bus:

1. **Read WHO_AM_I (0x0F)** at 7-bit address **0x6B**.
   - `0x71` → it is a 16BX and the register map below is right.
   - `0x70` → it is a plain 16X/DSV, and the in-tree `lsm6dsv16x` driver works as-is.
     Stage B collapses to "enable the existing driver", which would be a good outcome.
   - anything else → stop and identify the part before writing another line.
2. **Write `CTRL1` (0x10)** to set an accelerometer output data rate — something slow and
   unambiguous, 60 Hz or below. One register write.
3. **Poll `STATUS_REG` for XLDA**, or just read on a timer at first; at 60 Hz a 1 Hz read
   loop never misses.
4. **Burst-read six bytes from `OUTX_L_A` (0x2C)**, little-endian int16 per axis.
5. **Log X/Y/Z**, both raw counts and converted to m/s² or g using the configured
   full-scale.

Why this before a driver: it proves the bus wiring, the pull-ups, the address strap and
the part identity in one step, and every one of those is currently unverified. A driver
that fails to bind tells you almost nothing about which of the four is wrong.

**Sanity check for the log:** a board sitting flat should read roughly 0, 0, ±1 g. If the
magnitude of the vector is not ~1 g, the scaling or the register base is wrong. If it is
~1 g but on the wrong axis, that is only orientation and can wait.

## Stage B — a proper driver, once stage A prints sensible numbers

Same pattern as the VL53L9CX port already in this repo, which is the argument for it:
the shape is proven here and the reviewers of that code are us.

- Out-of-tree Zephyr module under `firmware/drivers/`, wrapping ST's
  `lsm6dsv16bx_STdC` HAL unmodified.
- Add the missing `USE_STDC_LSM6DSV16BX` Kconfig selection, or vendor the HAL the way
  ST's VL53L9 driver was vendored — it is BSD-3-Clause like the rest of stmemsc, so
  either is open. Vendoring keeps the build reproducible from a clone; selecting keeps it
  smaller. **Prefer selecting if the symbol can simply be added.**
- Implement Zephyr's sensor API: `sensor_sample_fetch` / `sensor_channel_get` for
  `SENSOR_CHAN_ACCEL_XYZ` and `SENSOR_CHAN_GYRO_XYZ`.
- Devicetree binding modelled on `st,lsm6dsv16x-i2c.yaml`.

Unlike the VL53L9CX, the sensor API fits this device well — it produces a handful of
scalar channels, not a 2268-zone frame — so there is no reason to invent a custom API
here.

## Where the devicetree node goes

**In an application overlay, not the board file.** `firmware_test/boards/` takes a
`water_sense_board_nrf54l15_cpuapp.overlay` that adds the IMU under the existing I²C
controller. That is the correct Zephyr mechanism for application-specific hardware and it
keeps the board file Victor's.

Stage A does not strictly need a node at all — it can take the bus with
`DEVICE_DT_GET(DT_NODELABEL(i2c21))` and a literal address — but adding the node early is
cheap and means stage B changes no wiring.

## Open questions for Victor

1. **The exact part marking.** 15BX or 16BX. Branches the whole plan.
2. ~~**The I²C address.**~~ **ANSWERED 2026-09-04: 0x6B** (SA0 strapped high). No
   collision with the VL53L9CX at 0x29.
3. **Is it on the same bus as the VL53L9CX** — SCL P1.08, SDA P1.13? Assumed yes, since
   that is the only I²C on the board.
4. **Are INT1/INT2 wired?** Not needed for stage A polling, and needed for anything
   interrupt-driven later.
5. **Are there external pull-ups on SDA/SCL?** The board's pinctrl sets no
   `bias-pull-up`. With two devices on the bus this stops being theoretical.

## One hazard worth stating before anyone debugs this bus

The VL53L9CX shares it, and it has two behaviours that will mislead:

- **Do not run a general `i2cdetect`-style scan to find the IMU.** The VL53L9CX does not
  support the empty START+STOP transactions such scans use to probe some address ranges,
  and it can wedge into NAK-everything. Probe 0x6A and 0x6B specifically.
- **The VL53L9CX will not acknowledge at all unless AP_CLK is running.** So a scan that
  finds the IMU and not the ToF sensor is not evidence of a ToF fault.

No address collision: 0x29 versus 0x6A/0x6B.
