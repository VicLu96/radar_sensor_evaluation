# VL53L9CX Zephyr driver

Out-of-tree Zephyr module wrapping ST's VL53L9 driver for I²C.

## Status

**Scaffolding, and part of it is now known wrong.** ST's driver arrived 2026-09-01
(X-CUBE-53L9A1) and it is a new generation, not the VL53L5CX / VL53L8CX ULD carried
forward. Three things follow:

1. **`vl53l9cx_platform.[ch]` must be rewritten.** It was written to the L5/L8
   convention — six `VL53L9CX_RdByte`-style functions returning `uint8_t`. ST's real
   contract is thirteen functions returning `int`, with an opaque `void *const p_dev`.
   See `docs/plan/st-package-audit.md` §1.
2. **`vl53l9cx.c`** — the Zephyr device wrapper (init, PM actions, frame plumbing) is
   still to write, and can now be written against a visible API rather than guessed at.
3. **The devicetree binding needs VDDA, VDDIO and the external clock**, which
   `vl53l9_init()` requires and which come from the schematic.

ST's sources are now in the repo — see `st/PROVENANCE.md`.

## Layout

```
zephyr/module.yml     registers this as an out-of-tree module
Kconfig               options, including the path to ST's sources
CMakeLists.txt        builds our files plus ST's, unmodified
dts/bindings/         st,vl53l9cx.yaml
include/vl53l9cx/     public API
st/                   ST's driver, byte-identical, BSD-3-Clause — do not edit
st-reference/         ST's own platform port for STM32H5 — reference, not built
vl53l9cx_platform.[ch]  THE PORT — ST's hardware hooks on Zephyr I2C (to rewrite)
vl53l9cx.c            (to write) Zephyr device driver
```

## The design decision

**ST's driver is used unmodified.** We implement the thirteen platform functions it
calls and wrap the result. Keeping their source byte-identical to the release means
their updates drop in, and it keeps the line between "our bug" and "their bug" sharp —
which is worth a great deal when a sensor goes quiet and there is no reference board.

## Three traps, all handled in `vl53l9cx_platform.c`

All three are confirmed against ST's source, 2026-09-01.

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

## Address — **VERIFY**, the sources disagree

The community Python driver uses **0x29** 7-bit (`0x52` 8-bit) and works over Linux
I²C. ST defines `VL53L9_DEFAULT_ADDRESS (0x52)` and passes it straight into
`I3C_PrivateTypeDef.TargetAddr`, which the STM32 HAL documents as a **7-bit** field.
One of the two is a shift error.

Expect the device at **0x29**, and check 0x52 if nothing answers. Probe the two
addresses **individually, in read-byte mode** — the device does not support the empty
START+STOP transactions a general `i2cdetect` sweep uses to probe some ranges, and such
a scan can wedge it.

**Before blaming the address, check the clock.** The sensor needs 6-27 MHz on AP_CLK
(12 MHz on every reference design) and **does not acknowledge its I²C address at all
until that clock is running** — see `docs/plan/st-package-audit.md` §7. A silent bus is
far more likely to be a missing clock than a wrong address.

## Power management

`SUSPEND` and `TURN_OFF` are deliberately distinct:

- **SUSPEND** — stop ranging, sensor standby, rail stays up, firmware retained
- **TURN_OFF** — drop the board's sensor power domain; the next resume pays a full
  firmware blob reload, which is seconds of I²C traffic

Keeping them separate makes the idle-strategy crossover a runtime choice rather than a
rebuild, so the stage-4 energy sweep is a configuration matrix instead of four firmware
variants. `vl53l9cx_last_boot_ms()` exposes the reload cost, because it is a
measurement the paper needs and not a debug detail.
