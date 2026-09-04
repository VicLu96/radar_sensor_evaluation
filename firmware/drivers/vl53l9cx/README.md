# VL53L9CX Zephyr driver

Out-of-tree Zephyr module wrapping ST's VL53L9 driver for I²C.

## Status

**Written, complete, and never compiled.** As of 2026-09-01 the port exists in full —
platform layer, Zephyr driver, devicetree binding, power management — written against
ST's real API rather than guessed at. What it has not done is meet a compiler or a
sensor.

Be precise about what that means:

| | |
|---|---|
| ST's 13 platform functions | **Implemented.** Signatures checked one by one against `st/vl53l9_platform.h` |
| Every ST function this driver calls | **Checked** against `st/vl53l9.h` — names, arity, argument types |
| Frame layout, byte counts, binning geometry | **Read out of ST's source**, not estimated |
| Compiles | **Unknown.** No toolchain on the machine this was written on |
| Runs | **Unknown.** No hardware |
| Three board values | **Missing.** `vdda-microvolt`, `vddio-microvolt`, `ext-clock-frequency` are `required: true`, so a board file that omits one fails at build time rather than misbehaving at run time |

So: first compile is expected to surface ordinary mistakes — a missing include, a
Zephyr API that moved. Treat that as normal. What should *not* need rework is the
shape of the port, because that came from ST's headers rather than from the VL53L5CX
family conventions the earlier scaffolding assumed.

## First bring-up, in order

The board has no known-good reference, so the order matters more than usual:

1. **Scope AP_CLK.** No clock, no ACK — the sensor is silent without it and looks dead.
2. **Probe 0x29** in read-byte mode. Not a general `i2cdetect` sweep: the device does
   not support the empty START+STOP transactions it uses to probe some ranges and such
   a scan can wedge it.
3. **Watch the blob upload on a logic analyser.** 9,865 bytes, chunked. Its duration is
   also a measurement the power model needs — `vl53l9cx_last_boot_ms()`.
4. **Log the status line from the first frame.** Eight health bits, and on this board
   they are the only second opinion available.
5. **Point it at an asymmetric scene** before trusting any spatial logic — the driver
   does not rotate or flip, and the community Python driver flips by default.

## Layout

```
zephyr/module.yml     registers this as an out-of-tree module
Kconfig               options
CMakeLists.txt        builds our files plus ST's, unmodified
dts/bindings/         st,vl53l9cx.yaml
include/vl53l9cx/     public API — frames, not Zephyr sensor channels
st/                   ST's driver, byte-identical, BSD-3-Clause — do not edit
st-reference/         ST's own platform port for STM32H5 — reference, not built
vl53l9cx_platform.c   THE PORT — ST's 13 hardware hooks on Zephyr I2C
vl53l9cx_private.h    config/data structs shared by the two .c files
vl53l9cx.c            Zephyr device driver: init, PM, frame plumbing
```

## The design decision

**ST's driver is used unmodified.** We implement the thirteen platform functions it
calls and wrap the result. Keeping their source byte-identical to the release means
their updates drop in, and it keeps the line between "our bug" and "their bug" sharp —
which is worth a great deal when a sensor goes quiet and there is no reference board.

## Four traps, all handled in `vl53l9cx_platform.c`

All four are confirmed against ST's source, 2026-09-01.

**1. Register indices are 16-bit.** Zephyr's `i2c_reg_read_byte_dt()` assumes an 8-bit
register address and will not work here. Every access writes a two-byte big-endian index
first. This is the most common way this port fails, and it presents as the device
acknowledging its address then returning nonsense.

**2. The firmware blob arrives as one huge `vl53l9_write()`.** 9,865 bytes in a single
logical write — `vl53l9.c:179`. Chunked via `i2c_transfer_dt()` with a two-message
transfer, so no contiguous index+payload buffer is ever allocated. Chunk size is a
devicetree property because it is a genuine tuning knob against a cost paid on every
cold start.

**3. `vl53l9_wait_ms` below one tick.** `k_sleep()` cannot resolve sub-tick, so a
requested 1 ms silently becomes a full tick — at 100 Hz ticks that stretches a blob
upload tenfold. The platform layer busy-waits below a tick and sleeps above it.

**4. No repeated start — ever.** A read is START/write-index/STOP, then START/read/STOP:
two separate transactions. `i2c_write_read_dt()` is wrong here. The part does not
support a repeated start between index and data (datasheet "known limitations"), and it
does not merely fail — it latches the device into **NAK-everything** until a clean STOP
escapes it, so the first bad read makes the sensor look dead from then on. ST's
legacy-I2C path does the same split.

This one bit us in writing: the first version of `vl53l9cx_platform.c` used
`i2c_write_read_dt()`, on the reasoning that one transaction cannot lose the index on a
multi-master bus. Sound for most I2C parts, wrong for this one — and it would have
produced exactly the "sensor is dead" symptom this repo's rules warn against misreading
as a software bug.

## Address — resolved

**0x29 is the 7-bit address.** Put that in devicetree `reg`.

ST's `VL53L9_DEFAULT_ADDRESS` is `0x52`, the 8-bit form, and their code shifts it down
in two places: `vl53l9_set_com_config()` writes `address >> 1` into the device's address
register, and the legacy-I2C branch of their platform layer shifts the target address
right by one before every transfer (`st-reference/vl53l9/vl53l9_platform.c`, both
`_i3c_read` and `_i3c_write`). Consistent throughout — 8-bit in ST's API, 7-bit on the
wire.

**Before blaming the address, check the clock.** The sensor needs 6-27 MHz on AP_CLK
(12 MHz on every reference design) and does not acknowledge its I2C address until that
clock is running — see `docs/plan/st-package-audit.md` section 7. A silent bus is far
more likely to be a missing clock than a wrong address.

## Power management

`SUSPEND` and `TURN_OFF` are deliberately distinct:

- **SUSPEND** — stop ranging, `VL53L9_POWER_ULTRA_LOW`, rail and clock stay up,
  firmware retained. Cheap to resume.
- **TURN_OFF** — drop XSHUT, drop the sensor power domain, and gate AP_CLK *only if the
  MCU drives it from a PWM*. Zero sensor standby current, but the next resume pays a full
  firmware blob reload: 9,865 bytes, about 250 ms at 400 kHz.

**On `water_sense_board`, AP_CLK is not gated.** It comes from the GRTC fast clock
output, which is configured at boot and has no runtime gate. So `TURN_OFF` removes the
sensor's draw and leaves a fixed clock term running underneath it — read
`docs/plan/ap-clk-always-on.md` before trusting any stage-4 energy number.

ST gives three power modes (`REGULAR`, `LOW`, `ULTRA_LOW`), which was not in the plan
and adds a third axis to the stage-4 sweep at the cost of a register write.

Keeping them separate makes the idle-strategy crossover a runtime choice rather than a
rebuild, so the stage-4 energy sweep is a configuration matrix instead of four firmware
variants. `vl53l9cx_last_boot_ms()` exposes the reload cost, because it is a
measurement the paper needs and not a debug detail.
