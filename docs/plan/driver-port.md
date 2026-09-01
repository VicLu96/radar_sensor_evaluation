# Porting the ST ULD to Zephyr over I²C

Written 2026-08-31. This is stage 1 — everything else is blocked on it.

## The strategy in one line

**Implement ST's platform layer; do not rewrite ST's driver.**

ST's Ultra Lite Driver is deliberately platform-independent C: all hardware contact
goes through a small set of functions the integrator supplies. Porting it means writing
those functions against Zephyr's I²C API and wrapping the result as a Zephyr module.

**Why not rewrite it.** The ULD carries the init sequence, the firmware blob upload
protocol, calibration handling and the frame unpacking — much of it undocumented
outside the code, and some of it timing-sensitive. Rewriting means owning all of that
and having no reference when a frame comes back subtly wrong. The port is a few hundred
lines; a rewrite is the project.

## Layers

```
  application  (sampling, detection, BLE)
        |
  Zephyr device driver     vl53l9cx.c        our code, Zephyr idioms
        |                                     device API, devicetree, PM
  ST ULD                   vl53l9cx_api.c    ST's code, unmodified
        |
  platform layer           vl53l9cx_platform.c  our code, ~150 lines
        |
  Zephyr I²C               i2c_write_dt / i2c_burst_read_dt
```

**The ST layer stays unmodified.** Keeping it byte-identical to ST's release means ST
updates can be dropped in, and it keeps the boundary of "our bug" versus "their bug"
sharp — which matters when a bring-up goes quiet.

## The platform layer — the actual work

ST's ULD for this family expects roughly these (**VERIFY the exact signatures against
X-CUBE-53L9A1; they are stable across VL53L5CX/L8CX but must be confirmed for L9**):

| ST function | Zephyr implementation |
|---|---|
| `VL53L9CX_RdByte` | `i2c_write_read_dt` — 16-bit register index, 1 byte out |
| `VL53L9CX_WrByte` | `i2c_write_dt` — index + 1 byte |
| `VL53L9CX_RdMulti` | `i2c_write_read_dt` — index, then N bytes |
| `VL53L9CX_WrMulti` | `i2c_write_dt` — index + N bytes |
| `VL53L9CX_WaitMs` | `k_sleep(K_MSEC(n))` |
| `VL53L9CX_SwapBuffer` | endianness swap, pure C, no Zephyr needed |

### Three traps in that table

**1. Register indices are 16-bit, not 8-bit.** The address must be written
big-endian before the data. Zephyr's `i2c_reg_read_byte_dt` assumes an 8-bit register
address and **will not work** — use `i2c_write_read_dt` with a two-byte address buffer.
This is the single most common way this port goes wrong.

**2. `WrMulti` is called with the firmware blob**, tens of kilobytes in one logical
write. Zephyr's I²C API takes a buffer and a length; the blob must either be chunked or
passed via a scatter-gather message list. **Chunk it**, and make the chunk size a
Kconfig option — it interacts with the driver's stack/heap budget and is worth sweeping
during stage 4.

**3. `WaitMs` during blob upload is called often.** Using `k_sleep` yields the CPU,
which is correct and lets the system idle — but if a delay is genuinely sub-tick,
`k_busy_wait` is needed instead. Getting this wrong shows up as a blob upload that
either fails or takes far longer than it should.

## Devicetree

The binding needs more than a bus address, because the board can gate power domains:

```dts
&i2c1 {
    vl53l9cx: vl53l9cx@52 {
        compatible = "st,vl53l9cx";
        reg = <0x52>;
        status = "okay";

        /* board control lines - names to match Victor's schematic */
        xshut-gpios   = <&gpio1 4 GPIO_ACTIVE_LOW>;
        int-gpios     = <&gpio1 5 GPIO_ACTIVE_LOW>;
        power-gpios   = <&gpio1 6 GPIO_ACTIVE_HIGH>;  /* domain gate, stage 4 */
    };
};
```

- **`reg`** — the 7-bit address. ST documents these parts as `0x52`, which is the
  **8-bit** write address; Zephyr wants the **7-bit** address, i.e. `0x29`. **VERIFY** —
  this bites almost everyone once.
- **`int-gpios`** — data-ready interrupt. Polling works but burns energy; interrupt-
  driven is what stage 4 wants, so wire it from the start.
- **`power-gpios`** — the domain gate. Modelling it in devicetree now means stage 4 is
  a policy change, not a rearchitecture.

## Power management — build the hooks now, the policy later

Implement Zephyr's device PM actions in stage 1 even though stage 4 sets the policy:

| Action | Behaviour |
|---|---|
| `PM_DEVICE_ACTION_RESUME` | Assert power, release XSHUT, upload blob **if not retained**, start ranging |
| `PM_DEVICE_ACTION_SUSPEND` | Stop ranging, enter sensor standby, keep the rail up |
| `PM_DEVICE_ACTION_TURN_OFF` | Drop the power domain. Next resume pays the full blob reload |

Separating `SUSPEND` from `TURN_OFF` is the whole point: it makes the crossover in
[`implementation.md`](implementation.md#the-crossover-that-decides-the-architecture) a
runtime choice rather than a rebuild, so the stage-4 sweep is a configuration matrix
rather than four firmware variants.

## Blob storage

~84 KB at VL53L8CX scale, **VERIFY for L9**. Against 1.5 MB of flash this is
comfortable.

- Store as a `const uint8_t[]` in a dedicated section so its size is visible in the map
  file.
- Do **not** copy it to RAM to upload — stream it from flash in chunks.
- Log the upload duration at init behind a Kconfig flag. **This number is a paper
  input**, not just a debug line.

## API the driver exposes

Zephyr's `sensor` API is a poor fit — it is built around scalar channels, and this
device produces a 2268-zone frame. Fighting the abstraction produces an awkward driver.

**Expose a small custom API instead**, and say so plainly:

```c
int vl53l9cx_start(const struct device *dev, enum vl53l9cx_res res, uint8_t hz);
int vl53l9cx_stop(const struct device *dev);
int vl53l9cx_get_frame(const struct device *dev, struct vl53l9cx_frame *out,
                       k_timeout_t timeout);
```

with the frame as a plain struct of per-zone distance, status and optionally
reflectance. A `sensor_channel_get` wrapper returning a single centre-zone distance can
be added later for compatibility, but it should not shape the design.

## Bring-up order

Deliberately incremental — each step eliminates one class of fault:

1. I²C scan finds the device
2. Device ID register reads back the expected value
3. Blob uploads, sensor reports ready *(record the duration)*
4. One frame at the lowest resolution
5. One frame at 54×42
6. Continuous ranging with the data-ready interrupt
7. Frames stream to the host viewer

## Reference material

- **X-CUBE-53L9A1** — ST's package. The authority on init order and signatures.
- **UM3656** — its user manual.
- **`github.com/earlynerd/VL53L9-Arduino`** — a community port reporting working init,
  blob upload and frame reads. Different transport (I3C), same init sequence. **Read it
  before writing the port** — it is the nearest thing to a known-good reference.
- ST ULD user manuals for **VL53L5CX (UM2884)** and **VL53L8CX (UM3109)** — the L9 API
  follows the same shape, and these are better documented.
