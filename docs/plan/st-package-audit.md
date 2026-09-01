# X-CUBE-53L9A1 — what it gives us, and what it changes

Audited 2026-09-01 against **X-CUBE-53L9A1 / STM32CubeExpansion_53L9A1_V1.0.0**,
supplied by Victor. The package is held locally at `vendor/x-cube-53l9a1/` (untracked,
39 MB); the parts we build against are copied into the repo — see *Licensing* below.

**Verdict: yes, this is everything stage 1 needs.** It also invalidates part of the
scaffolding and corrects two numbers the paper depends on.

---

## What is in it

| Path in package | What it is | Size |
|---|---|---|
| `Drivers/BSP/Components/vl53l9/` | **The driver.** `vl53l9.c/h`, `vl53l9_reg.h`, `vl53l9_patch.h`, `vl53l9_platform.h` | 2,242 lines |
| `Utilities/vl53l9-common/` | **ST's own reference platform port** — the thing we are writing the Zephyr equivalent of | ~600 lines |
| `Drivers/STM32H5xx_HAL_Driver/`, `Drivers/CMSIS/` | STM32H5 HAL and CMSIS | irrelevant, ~35 MB |
| `Projects/NUCLEO-H563ZI/` | Demo application | reference only, SLA-licensed |
| `Middlewares/ST/vl53l9-transform-c`, `media-object` | Post-processing libraries | SLA-licensed, not needed |
| `dm01264776.pdf` | Package documentation | |

The driver is **not** the VL53L5CX / VL53L8CX "ULD" carried forward. It is a new
generation with a different API, different file names and a different platform
contract. Every assumption the scaffolding inherited from the L5/L8 convention is
wrong.

---

## 1. The platform contract — the scaffolding is wrong

`firmware/drivers/vl53l9cx/vl53l9cx_platform.h` was written to the L5/L8 convention:
six functions named `VL53L9CX_RdByte` and friends, returning `uint8_t`, taking a named
`VL53L9CX_Platform *`. **None of that is right.**

ST's actual contract (`vl53l9_platform.h`, verbatim) is **thirteen** functions:

```c
int vl53l9_read      (void *const p_dev, uint16_t address, uint8_t *p_values, uint32_t size);
int vl53l9_read8     (void *const p_dev, uint16_t address, uint8_t  *p_value);
int vl53l9_read16    (void *const p_dev, uint16_t address, uint16_t *p_value);
int vl53l9_read32    (void *const p_dev, uint16_t address, uint32_t *p_value);
int vl53l9_read_async(void *const p_dev, uint16_t address, volatile uint8_t *p_values, uint32_t size);
int vl53l9_write     (void *const p_dev, uint16_t address, uint8_t *p_values, uint32_t size);
int vl53l9_write8    (void *const p_dev, uint16_t address, uint8_t  value);
int vl53l9_write16   (void *const p_dev, uint16_t address, uint16_t value);
int vl53l9_write32   (void *const p_dev, uint16_t address, uint32_t value);
int vl53l9_wait_ms   (void *const p_dev, uint32_t delay_ms);
int vl53l9_get_config_vddio    (void *const p_dev, vl53l9_vddio_t *voltage);
int vl53l9_get_config_vdda     (void *const p_dev, vl53l9_vdda_t  *voltage);
int vl53l9_get_config_ext_clock(void *const p_dev, uint32_t *ext_clock);
```

Differences that matter:

- **`int` return with `VL53L9_ERROR_*` codes** (0, -1 … -6), not `uint8_t` 0/255.
- **`void *const p_dev` is opaque.** ST never touches the struct, so the Zephyr
  `struct device *` can be passed straight through. This is *easier* than the old
  convention, not harder.
- **Sized accessors are called directly.** `read8/16/32` and `write8/16/32` are not
  wrappers ST builds on top of `read`/`write` — they are ours to implement, and
  `vl53l9.c` calls them constantly.
- **No `SwapBuffer`.** Endianness is handled inside the sized accessors.
- **Three config getters.** `vl53l9_init()` calls all three and writes the values into
  the device (`vl53l9.c:163-176`). They are **mandatory and board-specific** — see the
  open questions below.
- **`read_async`** exists for a DMA-backed split frame read, paired with
  `vl53l9_get_frame_async()` / `_async_ack()`. The synchronous `vl53l9_get_frame()`
  path is complete on its own, so async can return `VL53L9_ERROR_PLATFORM` during
  bring-up and be implemented later if the transfer needs to overlap compute.

**The three traps documented in the driver README survive unchanged** — 16-bit
big-endian register index, the blob as one huge write, sub-tick `wait_ms`. They are
Zephyr facts, not API facts, and the audit confirms all three are real.

## 2. The bus — I²C is fine, and ST's own port proves it

ST's reference platform layer is written against the STM32H5 **I3C** peripheral
(`HAL_I3C_*`). That looks alarming for an I²C design, and is not.

Every transfer uses the peripheral's **legacy I²C private-transfer** mode —
`I2C_PRIVATE_WITHOUT_ARB_STOP` and `I2C_PRIVATE_WITH_ARB_RESTART`, defined in
`stm32h5xx_hal_i3c.h`. On the wire these are ordinary I²C transactions: write the
2-byte big-endian register index, then a repeated start and the payload. That maps
one-to-one onto Zephyr's `i2c_transfer_dt()` with a two-message list.

`vl53l9_interface.h` also enumerates the bus explicitly:

```c
typedef enum { PLATFORM_BUS_I2C = 1, PLATFORM_BUS_I3C = 2, PLATFORM_BUS_CSI = 4 } platform_bus_type_t;
```

**I²C is a first-class supported configuration, not a downgrade.** The 2026-08-31
interface decision stands, now with a source.

The frame *output* interface is a separate setting: `vl53l9_hw_config_t.output_interface`
selects CSI-2 or I3C, and `signaling_mode` selects an in-band interrupt versus a
dedicated interrupt pad. **We want the interrupt pad**, since there is no IBI on plain
I²C.

## 3. Frame sizes — the budget was 9% optimistic, and the dynamic range is half what we claimed

`vl53l9.c:65-84` gives exact buffer sizes: three 16-bit planes per zone, plus a DSS
array, plus a fixed **100-byte status line**. The repo's arithmetic counted the zone
data only.

| Binning | Resolution | Zones tx | Frame bytes | @400 kHz | Format |
|---|---|---|---|---|---|
| 2 | 54×42 | 2268 | **14,842** | ~404 ms | wide |
| 4 | 24×20 | 576 (24×24, cropped) | **3,844** | ~105 ms | square |
| 6 | 18×14 | 252 | **1,738** | ~47 ms | wide |
| 8 | 12×10 | 120 | **880** | ~24 ms | wide |
| 12 | 8×6 | 64 (8×8, cropped) | **516** | ~14 ms | square |
| 24 | 4×4 | 16 | **204** | ~6 ms | wide |

Two corrections:

1. **54×42 is 14,842 bytes, not 13,608** — the 1,134-byte DSS array and the 100-byte
   status line were missed. At 400 kHz that is **~404 ms, not ~370 ms**, and the bus
   ceiling is ~2.5 fps rather than ~2.7. Immaterial for room dwell at 0.1 Hz; it
   matters for the doorway demo, which was already marginal.
2. **The dynamic range is 72.8×, not ~150×.** The status line is a fixed 100-byte floor
   that dominates the smallest mode — at 4×4, **half the bus traffic is status**. The
   energy-accuracy curve therefore flattens hard at the low end, and dropping below
   12×10 buys almost nothing on the bus. This is a more interesting finding than a
   clean 150× would have been, and it belongs in the paper.

## 4. Binning and field of view — the open question is answered

`vl53l9_set_binning()` (`vl53l9.c:455-508`) splits the six modes into two families:

- **Wide, no crop** — 54×42, 18×14, 12×10. Same optical field, zones merged. These are
  a **like-for-like** comparison.
- **Square, cropped** — 24×20 (24×24 with `y_offset=2`), 8×6 (8×8 with `y_offset=1`),
  4×4. These transmit a square array with an on-device crop window, so the vertical
  field is **not** the same.

So the paper's central curve should be built on **binning 2 / 6 / 8** — three clean
points, 2268 → 252 → 120 zones, an 18.9× span with the field held constant. The square
modes remain usable but cover a different footprint and must be reported as such. The
DECISIONS open question ("does binning preserve the field of view?") resolves to **yes
within the wide family, no across families**.

## 5. What else the API hands us

Read `vl53l9.h` for the full surface. The parts that change plans:

- **`vl53l9_set_power_mode()`** — `REGULAR`, `LOW`, `ULTRA_LOW`. A third axis for the
  stage-4 sweep that was not in the plan, and it is a register write rather than a
  power-domain change, so it is cheap to characterise.
- **`vl53l9_set_frame_period(us)`** plus **`vl53l9_trigger_frame()` / `poll_frame()`** —
  autonomous streaming *and* single-shot on demand. Single-shot is exactly the dwell
  use case: wake, trigger, read, power down.
- **`vl53l9_set_exposure(context, ms)`** and two **contexts** (`SHORT` / `LONG`) with
  independent binning and exposure. The sensor holds two configurations and switches
  between them, which makes the event-triggered hybrid (4×4 watch → 54×42 burst) a
  context switch rather than a reconfiguration.
- **`vl53l9_get_raw_buffer_size(binning, &size)`** — no need to hard-code the table
  above; ask the driver.
- **`vl53l9_set_com_config(address, instance_id)`** — the device address is settable at
  runtime, so multi-sensor becomes a later option rather than a board respin.
- **`vl53l9_get_status()`** — per-frame health bits: VHV over/under-voltage, SPAD supply
  overload, HV boost limit, PLL lock, reference array, internal firmware. **Log these
  from day one.** On a board with no known-good reference this is the closest thing to
  a second opinion, and several of the bits are exactly what a marginal supply rail
  looks like.
- **Firmware patch v0.17, 9,865 bytes**, uploaded by `vl53l9_init()` as a single
  `vl53l9_write()` (`vl53l9.c:179`). Confirms both the blob size recorded on
  2026-08-31 and the chunking trap.
- **`VL53L9_CALIB_DATA_SIZE` = 2,332 bytes** via `vl53l9_get_calib_data()` — factory
  calibration is retrievable. Whether it must be re-fetched after a power cycle is
  **VERIFY**, and it matters: 2.3 KB on every wake is ~63 ms of bus time on top of the
  blob reload.

## 6. Still VERIFY

- **The I²C address conflicts between sources.** ST defines
  `VL53L9_DEFAULT_ADDRESS (0x52)` and passes it straight into
  `I3C_PrivateTypeDef.TargetAddr`, which the HAL documents as a 7-bit field. The
  community Python driver uses **0x29** 7-bit (`0x52` 8-bit) and demonstrably works
  over Linux I²C. One of the two is a shift error. **A bus scan settles it in a minute
  at bring-up** — expect the device at 0x29, and check 0x52 if nothing answers.
- **VDDA, VDDIO and the external clock frequency** are now blocking rather than
  curiosities: `vl53l9_init()` writes all three into the device, and a wrong value
  misconfigures the analogue front end. Needs Victor's schematic.
- **Whether calibration data survives a power cycle**, per above.

## Consequences for the devicetree binding

`vl53l9_device_t` (`vl53l9_interface.h`) is effectively ST's own statement of what a
board must provide, and the binding should mirror it:

| ST field | Devicetree |
|---|---|
| `address` | `reg` |
| `vdda`, `vddio` | enum properties — **from the schematic** |
| `ext_clock` | `ext-clock-frequency`, Hz |
| `xshut` | `xshut-gpios` |
| `intr` | `int-gpios` |
| `instance_id` | implicit per node |

Plus a power domain or regulator for the `TURN_OFF` path, which ST models as
`platform_power_enable()` / `_disable()` rather than as a property.

---

## Licensing — why only part of the package is tracked

`Package_license.md` carries a per-component SBOM:

- **`Drivers/BSP/Components/vl53l9/` — BSD-3-Clause.** Redistributable with the
  copyright notice retained. Copied byte-identical to `firmware/drivers/vl53l9cx/st/`.
- **`Utilities/vl53l9-common/` — BSD-3-Clause.** Copied to
  `firmware/drivers/vl53l9cx/st-reference/` as read-only reference for the port.
- **Middlewares and the NUCLEO projects — SLA0111.** Redistribution is permitted under
  conditions, but clause 5 forbids redistributing in a way that subjects the package to
  open-source terms. Not needed, so **not tracked**.
- CMSIS and the STM32H5 HAL are open-source but irrelevant and large. **Not tracked.**

The full package stays in `vendor/x-cube-53l9a1/`, which is gitignored. The earlier note
that ST packages are "licensed, never vendored" was written before the terms were read;
it holds for the package as a whole, and not for the BSD-3-Clause driver, which is the
part we build against.
