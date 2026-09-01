# VL53L9CX Zephyr driver

Out-of-tree Zephyr module wrapping ST's Ultra Lite Driver for I²C.

## Status

**Scaffolding — does not build yet.** What is here is the part that does not depend
on having ST's source: the platform binding, the devicetree binding, the public API and
the module plumbing. Two things are still missing:

1. **ST's ULD**, which is licensed and deliberately not vendored. Download
   **X-CUBE-53L9A1** from st.com into `vendor/x-cube-53l9a1/`.
2. **`vl53l9cx.c`** — the Zephyr device wrapper (init, PM actions, frame plumbing).
   It cannot be written correctly until ST's API is in front of us, and guessing at it
   would produce code that looks finished and is not.

## Layout

```
zephyr/module.yml     registers this as an out-of-tree module
Kconfig               options, including the path to ST's sources
CMakeLists.txt        builds our files plus ST's, unmodified
dts/bindings/         st,vl53l9cx.yaml
include/vl53l9cx/     public API
vl53l9cx_platform.[ch]  THE PORT — ST's hardware hooks on Zephyr I2C
vl53l9cx.c            (to write) Zephyr device driver
```

## The design decision

**ST's driver is used unmodified.** We implement the six platform functions it calls
and wrap the result. Keeping their source byte-identical to the release means their
updates drop in, and it keeps the line between "our bug" and "their bug" sharp — which
is worth a great deal when a sensor goes quiet and there is no reference board.

## Three traps, all handled in `vl53l9cx_platform.c`

**1. Register indices are 16-bit.** Zephyr's `i2c_reg_read_byte_dt()` assumes an 8-bit
register address and will not work here. Every access writes a two-byte big-endian index
first. This is the most common way this port fails, and it presents as the device
acknowledging its address then returning nonsense.

**2. The firmware blob arrives as one huge `WrMulti`.** Tens of kilobytes in a single
logical write. Chunked via `i2c_transfer_dt()` with a two-message transfer, so no
contiguous index+payload buffer is ever allocated. Chunk size is a devicetree property
because it is a genuine tuning knob against a cost paid on every cold start.

**3. `WaitMs` below one tick.** `k_sleep()` cannot resolve sub-tick, so a requested 1 ms
silently becomes a full tick — at 100 Hz ticks that stretches a blob upload tenfold. The
platform layer busy-waits below a tick and sleeps above it.

## Address

ST documents the device as `0x52`. That is the **8-bit** write address; devicetree
`reg` wants the **7-bit** address, `0x29`. **VERIFY** — this one bites almost everyone
once, and the symptom is a device that never acknowledges.

## Power management

`SUSPEND` and `TURN_OFF` are deliberately distinct:

- **SUSPEND** — stop ranging, sensor standby, rail stays up, firmware retained
- **TURN_OFF** — drop the board's sensor power domain; the next resume pays a full
  firmware blob reload, which is seconds of I²C traffic

Keeping them separate makes the idle-strategy crossover a runtime choice rather than a
rebuild, so the stage-4 energy sweep is a configuration matrix instead of four firmware
variants. `vl53l9cx_last_boot_ms()` exposes the reload cost, because it is a
measurement the paper needs and not a debug detail.
