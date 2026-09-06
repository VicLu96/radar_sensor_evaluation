# VL53L9CX driver: what happens, when, and on whose authority

Written 2026-09-06 by reading the code, not from memory. Every step below cites the file
and line it comes from, and every decision cites its source or is marked as an assumption.

**Sources used throughout**

| Tag | Document / file |
|---|---|
| **UM3683** | *VL53L9CX programming guide*, Rev 3 — `docs/um3683-programming-guide-stmicroelectronics.pdf` |
| **UM3656** | *Getting started with X-CUBE-53L9A1*, Rev 1 — `vendor/x-cube-53l9a1/Documentation/` |
| **ST-drv** | ST's driver, vendored unmodified — `firmware_test/drivers/vl53l9cx/st/vl53l9.c` |
| **Ours** | `vl53l9cx.c`, `vl53l9cx_platform.c` |
| **Community** | The hardware-validated community driver. **Cite sparingly** — on 2026-09-06 two things credited to it turned out to have first-party sources in this repo, and one "datasheet" citation turned out to be community-only. |

Where something is inference rather than a quotation, it says **ASSUMPTION** and explains
what would settle it.

---

## Stage 0 — Build time: devicetree becomes the config struct

`VL53L9CX_DEFINE`, `vl53l9cx.c:1324-1346`.

| Property | Becomes | Notes |
|---|---|---|
| `reg` | `cfg->i2c.addr` | 0x29, 7-bit |
| `vdda-microvolt` | `cfg->vdda` | **`DT_INST_ENUM_IDX`** — the *index*, not the value |
| `vddio-microvolt` | `cfg->vddio` | same |
| `ext-clock-frequency` | `cfg->ext_clock_hz` | written to the device verbatim, in Hz |
| `blob-chunk-size` | `cfg->blob_chunk_size` | 1024 → the 9,865-byte blob goes in 10 writes |
| `power/xshut/int/sync-gpios` | GPIO specs | all optional (`_GET_OR`) |
| `pwms` | `cfg->clock_from_pwm` | absent on both our boards → clock is board-supplied |

> **The enum-index detail is a real hazard.** `DT_INST_ENUM_IDX` means the binding's enum
> *order* is the register value. Verified 2026-09-06: `vdda-microvolt: [2800000, 3300000]`
> against ST's `VDDA_2V8 = 0, VDDA_3V3 = 1` (`st/vl53l9.h:69-72`), and
> `vddio-microvolt: [1200000, 1800000]` against `VDDIO_1V2 = 0, VDDIO_1V8 = 1`. Both match.
> Reordering either enum for tidiness would invert what is written into the device, with no
> error anywhere.

Device is registered at `POST_KERNEL`, priority 90 (`CONFIG_VL53L9CX_INIT_PRIORITY`).

---

## Stage 1 — `vl53l9cx_init()` (`vl53l9cx.c:1175`)

Runs at boot. **Does not touch the sensor.** Only sets up the host side.

1. `LOG_INF("init: starting")` — every step logs *before* it can fail, so a failure names
   itself rather than producing a silent "device not ready".
2. Init semaphore + mutex.
3. Check the I²C bus is ready.
4. **`power-gpios`** → `GPIO_OUTPUT_INACTIVE | GPIO_INPUT`. The `GPIO_INPUT` is deliberate:
   it connects the input buffer so the *pad* can be read back. Without it, reading an
   output returns what you wrote, and a line held low by a short is invisible.
5. **`xshut-gpios`** → same, so the part starts held in reset.
6. **`sync-gpios`** → `GPIO_OUTPUT_INACTIVE`. Active-low pin, so inactive = physically
   high = cannot trigger a frame. If not routed, logs that it must be tied high on the
   board. *(Source: UM3683 §2.7.2 — Follower mode triggers on SYNC_IN **active low**.)*
7. **`int-gpios`** → `GPIO_INPUT` + `GPIO_INT_EDGE_TO_ACTIVE`, callback registered.
8. `pm_device_driver_init(dev, pm_action)` — **this is what actually boots the sensor**,
   via `PM_DEVICE_ACTION_TURN_ON`. Booting here as well would upload the blob twice and
   double-count `boot_ms`, which the power model depends on.

---

## Stage 2 — `PM_DEVICE_ACTION_TURN_ON` → `device_boot()` (`vl53l9cx.c:506`)

### 2a. `power_up()` (`vl53l9cx.c:285`)

```
power-gpios → high
    wait 500 ms          CONFIG_VL53L9CX_POWER_SETTLE_MS
    read back the pad    ← catches a rail held low by a short
clock_start()
XSHUT → low
    wait 50 ms           CONFIG_VL53L9CX_XSHUT_LOW_MS
XSHUT → high
    wait 50 ms           CONFIG_VL53L9CX_XSHUT_SETTLE_MS
    read back the pad
```

**≈600 ms from power enable to first I²C transaction.**

- **500 ms**: not a datasheet figure. Deliberately far longer than any plausible rail
  settle, to remove it from the suspect list during bring-up. **This is paid on every
  wake** and must be trimmed with a scope before the energy work — half a second per wake
  would dominate the duty cycle the paper is about.
- **50 ms / 50 ms**: **first-party.** ST's own
  `platform_power_reset()` does exactly this — XSHUT low, `HAL_Delay(50)`, high,
  `HAL_Delay(50)` — at
  `vendor/x-cube-53l9a1/Utilities/vl53l9-common/platform/platform_utils.c:75-81`, and
  their reference app calls it as the first thing it does. *(Corrected 2026-09-06: this
  was previously credited to the community driver as second-hand. The first-party source
  was in this repository the whole time.)* UM3683 gives no numeric t_boot or reset pulse
  width.
- The XSHUT low pulse is **explicit** even though the pin is already inactive from Stage 1:
  it has only been low for microseconds, which is a coincidence, not a reset.

**Compliance — UM3683 §2.5.1.** The three conditions to leave `POWER_OFF` are: all three
supplies up (AVDD, DVDD, IOVDD), XSHUT high at IOVDD level, external clock active. ST says
they may be met **in any order**, so this sequence is compliant. Note the sequence covers
conditions 2 and 3; condition 1 is the board's job — verified on the PCB 2026-09-06
(`DVDD` C12 → +1V2, `AVDD` E6/E7 → +3V3, `IOVDD` E8 → +1V8).

### 2b. `clock_start()` (`vl53l9cx.c:113`)

Both our boards supply AP_CLK from the board, so the driver takes the non-PWM path and
**starts nothing**. What it does do:

- If GRTC generates the clock (`VL53L9CX_APCLK_FROM_GRTC`, true when devicetree gives
  `&grtc` a `clkout-fast-frequency-hz`): request **SYSCOUNTER ACTIVE** and never release
  it, then read the bit back. Without this the clock output stops every time the CPU
  sleeps. *(Source: `zephyr/drivers/timer/nrf_grtc_timer.c:595` — `CONFIG_NRF_GRTC_ALWAYS_ON`
  issues the same request but is promptless and unselected in this tree, so it cannot be
  set from `prj.conf`.)*
- Otherwise (X-NUCLEO overlay): logs that the module clocks itself, and requests nothing —
  a needless request would inflate idle current, the one number this project must not
  inflate by accident.
- `CONFIG_VL53L9CX_HOLD_HFCLK` (default **n**) additionally holds the HF clock domain. Off
  because it is expected to be redundant.

### 2c. Probe — before any boot attempt (`vl53l9cx.c:537`)

Reads `DEVICE_ID` (register `0x0000`) in a retry loop: **600 ms budget, 10 ms gap**. A
deadline rather than an attempt count, because a NAKing device fails in microseconds and
gets many tries while a stuck bus costs a full transfer timeout and gets one or two.

Retrying is not defensive padding — the part **NAKs while its ROM boots**, so the first NAK
is expected. *(Source: **Community**, which polls through NAKs for up to 500 ms.)*

**On failure**, it runs a diagnostic ladder rather than just returning an error:
1. `i2c_recover_bus()` — clocks SCL to free a slave holding SDA. Success and failure are
   both informative: recovery working means a device held the bus; recovery changing
   nothing means the lines cannot reach a high level (pull-ups, or a short).
2. Probes **both** address candidates, 0x29 and 0x52.
3. `try_inverted_polarity()` — redrives power/XSHUT at inverted **raw** levels, testing an
   active-low line declared active-high without a rebuild.

**On success**, two checks:

- **DEVICE_ID must equal `0x53334C39`** ("S3L9"). *(Source: UM3683 §2.5.2, which lists
  this as a check the driver **must** perform.)* All-zeros / all-ones are called out
  separately as the bus idle level rather than a device answering.
- **`SYSTEM_FSM` (0x008C)** is read and named. *(Source: UM3683 Table 8.)* Expect
  `0x01 READY_TO_BOOT`.

> **Both of these were added on 2026-09-06 after reading UM3683.** Before that the driver
> read DEVICE_ID and logged it without comparing it, so anything that acknowledged counted
> as success.

### 2d. `vl53l9_init()` — ST's code, unmodified (`st/vl53l9.c:153`)

| # | Action | Register | Timeout |
|---|---|---|---|
| 1 | Wait for `READY_TO_BOOT` | `SYSTEM_FSM` | **4 ms** |
| 2 | Write external clock, in Hz | `EXT_CLOCK` | — |
| 3 | Write VDDIO config | `VDDIO_CFG` = **0x0439** | — |
| 4 | Write VDDA config | `VDDA_CFG` = **0x0438** | — |
| 5 | Write the 9,865-byte patch | `FWPATCH` (0x1800) | — |
| 6 | Set install-patch flag | `SETTING_INSTALL_PATCH` | — |
| 7 | Issue `COMMAND_BOOT` | `COMMAND` | **71 ms** |
| 8 | Wait for `STANDBY` | `SYSTEM_FSM` | **4 ms** |
| 9 | Verify patch major/minor | `PATCH_REVISION` | — |
| 10 | `_init_default_config()` — VCSEL channels, blanking, dithering for both contexts | — | — |

Duration is timed into `boot_ms`, exposed as `vl53l9cx_last_boot_ms()`, because it decides
whether powering the sensor down during idle beats keeping it in standby.

**Three deviations of ST's code from ST's own document, all in ST's code, not ours:**

1. **No DEVICE_ID check.** UM3683's §2.5.2 pseudo-code begins `CHECK DEVICE_ID ==
   0x53334C39`; `vl53l9_init()` goes straight to waiting for `READY_TO_BOOT`. **Our probe
   covers this gap**, so the combined path is compliant.
2. **VDDIO is written before VDDA**; the document's pseudo-code has VDDA first. Both are
   configuration registers consumed at `BOOT`, so order should not matter — **ASSUMPTION**,
   from the fact that neither takes effect before step 7.
3. **Both are written unconditionally**, where the document says to write them "if
   different from the default values". Writing the default value is semantically a no-op,
   so this is harmless.

*(Addresses corrected 2026-09-06: `0x000C`/`0x000D` are offsets from
`VL53L9_REGBASE_BOOT_SETTINGS` = 0x042C, not addresses. The registers are at 0x0438 and
0x0439.)*

Two further ST deviations found on review, both in ST's vendored code:

4. **`DSS_DEFAULT_INIT_LUT` (0x05D6) is never written.** UM3683 §2.5.4.3 says to set it
   to 3; the reset value is 0. `_init_default_config()` writes the other three DSS
   settings and skips this one.
5. **`_init_default_config()` ends with a read where a write is meant.**
   `st/vl53l9.c:983-985` assigns `data = 0x01000800` then immediately calls
   `vl53l9_read32(..., &data)`, overwriting it. UM3683 §2.5.5.1 says *write*
   `SOC_CABDT_DIST_SCALE`. Harmless for us only because `vl53l9_set_context()` writes that
   register correctly and we always call it.

> **The 4 ms wait for `READY_TO_BOOT` is tighter than it looks.** It polls at 1 ms
> intervals, four times. It is only safe because our 600 ms probe already ran and the 50 ms
> XSHUT settle already elapsed, so the part is known to be answering before ST's code is
> entered. Anyone who removes the probe removes that protection.

### 2e. Post-boot re-read

DEVICE_ID is read again and validated. A part that answered the probe and then stopped
being itself points at a supply or clock that is not holding.

### 2f. One retry, and it is a power cycle (`vl53l9cx.c:1149`)

If `device_boot()` fails: `power_down()`, wait **100 ms**, boot once more. A power cycle
rather than another probe, because the probe already retried the read — what this covers is
a part that came up in a bad state or a rail that had not settled. **Exactly one retry**: a
boot loop hides a hardware fault behind an occasional success.

---

## Stage 3 — `configure_signalling()` (`vl53l9cx.c:740`)

Read-modify-write of `hw_config`. Field meanings verified against `st/vl53l9.h:111-122`:

| Field | Set to | Meaning |
|---|---|---|
| `output_interface` | `true` | I3C-style register output, **not** CSI-2 |
| `signaling_mode` | `true` | interrupt pad, not in-band interrupt |
| `interrupt_pad_mode` | `false` | CMOS push-pull |

`interrupt_pad_mode` is marked **VERIFY** in the code: open-drain would be correct if the
INTR line is shared or pulled to a different rail. On both current boards it is a dedicated
line, so CMOS is right.

Then **sync mode is read, logged, and set to `MANUAL`**:

- ST's `_init_default_config()` never writes `SYNCHRO`, so after boot the register holds its
  reset value. `VL53L9_SYNC_SLAVE` is 0 and registers usually reset to 0 — **ASSUMPTION**,
  which is exactly why the code *reads it first and logs what it found* rather than
  assuming. In SLAVE mode frames are triggered by SYNC_IN going low, and on the custom
  board nothing drives that pin.
- `MANUAL` is the safe resting state: frames start only on an explicit I²C command, so
  nothing electrical can start an exposure. *(Source: UM3683 §2.7.2.)*

---

## Stage 4 — Ranging

**Resolution** (`apply_resolution`, `vl53l9cx.c:879`) sets context `LONG` and the binning
divisor. Buffer size is obtained from `vl53l9_get_raw_buffer_size()` rather than our own
table — asking ST rather than trusting ourselves.

Square formats — **4x4, 8x6 and 24x20** — have a different field of view from the wide
formats, so any energy-versus-zones sweep must stay inside one family. The driver warns.

*(Corrected 2026-09-06, twice over. The `geom` table had `4x4` flagged `wide = true`, but
ST sets `FORMAT_SQUARE` for binning 24 (`st/vl53l9.c:489-491`) — so the warning never fired
for it. Fixed. And the crop is **not** on-device in the sense of saving bus traffic:
`vl53l9_get_frame()` sizes the transfer from the full raw array, so all 576 zones of a
24x24 cross the wire and `unpack()` crops on the host. That is what `tx_rows` is for.)*

**Two paths:**

- `vl53l9cx_capture()` — `MANUAL` mode, `start`, `trigger_frame`, wait, read, `stop`.
  One frame, device idle either side.
- `vl53l9cx_start()` — switches to `AUTONOMOUS` with a frame period, free-running.

**Waiting** uses the interrupt semaphore when `int-gpios` exists, otherwise polls
`frame-ready` every 5 ms — a fallback that exists so a board without the line still works,
not so it works well.

**Frame layout** (`vl53l9cx.c:816-830`), verified against `vl53l9_get_frame()` and ST's
`vl53l9_utils_parse_frame()`: **plane-major, not interleaved** —
`[depth u16 × zones][amplitude][ambient][dss][status line 100]`, all little-endian. Depth
carries millimetres in **bits 14:0** with a validity flag in bit 15; dropping the mask
yields distances around 32 m.

---

## Stage 5 — Power management (`pm_action`, `vl53l9cx.c:1109`)

| Action | What happens |
|---|---|
| `SUSPEND` | stop streaming, `set_power_mode(ULTRA_LOW)`. Rail, clock and firmware retained. |
| `RESUME` | `set_power_mode(REGULAR)` |
| `TURN_OFF` | XSHUT low → `clock_stop()` → power rail off. Zero standby current. |
| `TURN_ON` | full `device_boot()` + `configure_signalling()` |

The distinction is deliberate: it makes the idle-strategy crossover a runtime choice
instead of four firmware builds. `TURN_OFF` costs a full 9,865-byte blob reload on the next
wake — ~250 ms at 400 kHz. **Prediction, not result**; `vl53l9cx_last_boot_ms()` is what
turns it into one.

> On the **X-NUCLEO-53L9A1** there is no `power-gpios`, so `TURN_OFF` drops XSHUT only. That
> board cannot carry the duty-cycling work.

---

## I²C transaction rules (`vl53l9cx_platform.c`)

Three things that are not negotiable and are the classic ways this port fails:

1. **16-bit BIG-endian register index, LITTLE-endian data.** Zephyr's
   `i2c_reg_read_byte_dt()` assumes an 8-bit register address and will not work.
2. **No repeated start on the synchronous path.** A read is START/write-index/STOP, then
   START/read/STOP — two complete transactions. `i2c_write_read_dt()` is wrong here.
   *(Source: ST's own platform layer uses `I2C_PRIVATE_WITHOUT_ARB_STOP` on both halves of
   `_i3c_read()`, `Utilities/.../vl53l9_platform.c:329,346`.)*

   **Two corrections, 2026-09-06.** The "datasheet known limitations" citation was wrong:
   it traces to `vendor/vl53l9cx-python/README.md`, a **community** document, and no
   first-party source for it exists in this repository. Under this repo's own sourcing
   rule it must be labelled as such. And "ever" is too strong — ST's *async* legacy-I²C
   path does use `I2C_PRIVATE_WITH_ARB_RESTART` (`:389`). The claim that a repeated start
   latches the part into NAK-everything is likewise uncited. What is certain is that ST's
   synchronous path splits the transaction, and so do we.
3. **The blob is chunked** at `blob-chunk-size`, carrying the index forward by the offset,
   using a two-message transfer so no copy is needed.

Errnos are logged distinctly because they mean different things: `-EIO` = no acknowledge,
the bus is fine and nothing is at this address; `-ETIMEDOUT` = stuck bus, suspect pull-ups.

---

## Compliance summary against UM3683

| Requirement | Source | Status |
|---|---|---|
| Three supplies up before boot | §2.5.1 | ✅ verified on the PCB; not modelled in firmware (no register exists) |
| XSHUT high to reach `READY_TO_BOOT` | §2.5.1 | ✅ driven and read back |
| External clock active | §2.5.1 | ✅ board-supplied; GRTC keep-alive added 2026-09-06 |
| Any order permitted | §2.5.1 | ✅ |
| Check DEVICE_ID = `0x53334C39` | §2.5.2 | ✅ **ours; ST's own driver omits it** |
| Write `EXT_CLOCK` in Hz | §2.5.2 | ✅ ST-drv |
| Write `VDDA_CFG` / `VDDIO_CFG` | §2.5.2 | ✅ ST-drv (order swapped vs. the prose) |
| Patch → `INSTALL_PATCH` → `BOOT` | §2.5.2 | ✅ ST-drv |
| Verify patch revision | §2.5.2 | ✅ ST-drv |
| Frame signalling via INTR pad | §2.7.1 | ✅ `configure_signalling()` |
| SYNC_IN not left floating | *our reasoning* | ⚠️ driven if routed; otherwise **must be tied high on the board**. §2.7.2 only says Follower triggers on SYNC_IN active low — it says nothing about floating inputs. |
| Default address 0x29, 7-bit | §2.8.1 | ✅ |
| External clock 6–27 MHz | Table 11 | ✅ **8 MHz is in spec** — `EXT_CLOCK` 0x042C takes plain Hz, reset 12 MHz |
| Read calibration data after boot | §2.5.3 | ❌ **not done** — see below |
| `DSS_DEFAULT_INIT_LUT = 3` | §2.5.4.3 | ❌ not done (ST's omission, inherited) |
| Switchover / pulse-width profile | §2.5.5.1 | ❌ not done; resets are close but not equal |
| Reboot on laser safety error | §2.4 | ❌ **not done** — "the driver's responsibility" |

**The four ❌ rows were missing from this table until 2026-09-06.** Listing only the boot
path made the driver look more compliant than it is.

---

## Gaps I found while writing this — recommendations, not changes

I did not modify the driver for this report. Three things are worth your call:

1. **`vl53l9_get_status()` is never called, and it reports `pll_lock`.**
   `st/vl53l9.h:124-139` defines a status block with an error bitfield: `vhv_overvoltage`,
   `vhv_undervoltage`, `spad_supply_overload`, `hvboost_limit`, `sof_outside_blanking`,
   **`pll_lock`**, `ref_array`, `internal_fw`. `pll_lock` is the device's own answer to the
   AP_CLK question, and `spad_supply_overload` / `vhv_undervoltage` speak directly to the
   100 mA fold-back. Calling this on a boot failure would turn several of our inferences
   into readings. **This is the single highest-value addition available.**

2. **An ST bug in that same function.** `st/vl53l9.c:820-823` reads the five laser-driver
   status bytes into `status->laser_driver` — index 0 — on every iteration, instead of
   `&status->laser_driver[i]`. Elements 1-4 are left uninitialised. If we start using
   `vl53l9_get_status()`, only `laser_driver[0]` is trustworthy. ST's file is vendored
   unmodified on purpose, so this belongs in a wrapper, not a patch.

3. **A misleading log line in `try_inverted_polarity()`** (`vl53l9cx.c:467`). It prints
   `"power-gpios driven high, pad reads %d"` immediately after driving the pin **raw low**.
   The reading is real; the words are copy-pasted from `power_up()` and describe the
   opposite of what just happened. Diagnostic-only, but this is the path used when things
   are already confusing.

Additionally, still outstanding from earlier sessions and unchanged by this review: the
**500 ms power settle** needs trimming with a scope before any energy measurement, and
`interrupt_pad_mode` (CMOS vs open-drain) is marked VERIFY against the schematic.


---

# Expert review, 2026-09-06 — what was actually wrong

Two independent specialist reviews (nRF54L15/Zephyr platform, and ST VL53L9CX protocol)
were run against this driver. They converged on the same two critical defects, both since
verified directly and fixed.

## FIXED — the firmware patch upload could never have worked

`vl53l9_write()` issues the index and payload as two `i2c_msg` entries with `I2C_MSG_STOP`
only on the second. That is the correct Zephyr idiom for one START…STOP transaction — and
it is exactly the pattern `i2c_nrfx_twim.c:95` detects as *"merge these"*, routing both
through an internal buffer capped at
`MAX(zephyr,concat-buf-size, zephyr,flash-buf-max-size)`.

**Both default to 16 bytes.** With `blob-chunk-size = 1024`, every chunk asked for 1026 and
got `-ENOSPC`.

A second, independent trigger for the same failure: `g_vl53l9_fw_patch` is `const`, so it
links into RRAM — **verified at `0x0000d0dc`, section `R`** — and TWIM EasyDMA cannot source
from there. `nrf_dma_accessible_check()` fails and forces the same 16-byte buffer even
without concatenation.

Fixed in the application overlay with both properties at 1040. Costs 1,024 bytes of RAM
once (measured: 44,208 → 45,232), because the two share the same buffer.

Note the near miss that hid this: ST's largest default-config write is 2 + 14 = **exactly
16 bytes**, and the check is `>` not `>=`. One byte of margin on the path that works.

## FIXED — `device_boot()` discarded `vl53l9_init()`'s return code

The return was overwritten two statements later and never read, and a failed post-boot
`get_device_id()` fell through to `return 0`. Every distinguishable boot failure ST can
report — `TIMEOUT` on `READY_TO_BOOT`, `PLATFORM` on the patch write, `TIMEOUT` on `BOOT`
or `STANDBY`, `INTERNAL` on a patch-version mismatch — was swallowed and re-emerged as a
generic `-EIO` three functions away.

This is what would have hidden the bug above, and it is exactly the failure mode the rest
of this driver is written to prevent. Both returns are now checked.

## ADDED — ask the device why it failed

`vl53l9_get_status()` existed and was never called. It is now called on boot failure and
logs the error bitfield. **`pll_lock` is the device's own verdict on AP_CLK**, and
`vhv_undervoltage` / `spad_supply_overload` speak to the supply fold-back — turning two
standing inferences into readings for one transaction. UM3683 §2.4 also makes rebooting on
a laser safety error *"the driver's responsibility"*, which we still do not do.

ST's own bug is worked around rather than patched: `vl53l9_get_status()` reads all five LDD
status bytes into element 0 (`st/vl53l9.c:820-823`, missing `+ i`), so `laser_driver[]` is
not printed.

## OPEN — the AP_CLK explanation is weaker than this document claimed

The nRF reviewer challenged the causal story behind the SYSCOUNTER fix, and the challenge
holds up:

- CLKOUT_FAST divides the GRTC's **hfclock** (`pclk`, 16 MHz fixed-clock), while
  `SYSCOUNTER.CLKCFG.CLKSEL` selects among **low-frequency** sources. Different clock trees.
- Upstream Zephyr enables CLKOUT_FAST with no ACTIVE request at all.
- The GRTC has a separate `STATUS.CLKOUT.READY` handshake, suggesting CLKOUT makes its own
  clock request.

What *is* confirmed is that `AUTO_KEEP_ALIVE` lets the SYSCOUNTER drop out at WFI. Whether
CLKOUT_FAST follows it is **unproven**, and `CONFIG_VL53L9CX_HOLD_HFCLK` — the mechanism
with the more defensible clock-tree story — is off by default. **Settle this with a scope
before believing the 8 MHz is fixed.**

## OPEN — decisions for Victor

| | Finding | Why it matters |
|---|---|---|
| **Row order** | UM3683 §2.5.4.1.1: the first streamed byte is **bottom-left**, streamed bottom to top. `unpack()` does not flip, so row 0 is the bottom row — but `vl53l9cx.h` claims a top-left origin. | Every spatial decision is vertically mirrored, and it will not look wrong on a symmetric test scene. |
| **Calibration** | §2.5.3 and ST's reference app both read calibration data after boot. We never call `vl53l9_get_calib_data()`. | Distances are **raw, uncorrected and radial** — not calibrated perpendicular. Any accuracy number today must be labelled uncalibrated. |
| **Exposure** | `vl53l9_set_exposure()` is never called, so `NB_SHOT_STEP_n` stays at its reset of **0**. | If frames arrive but every zone reads invalid, this is the first suspect. Was also blocked by the concat bug. |
| **Frame period** | `vl53l9cx_start()` accepts any period, but ST rejects anything outside 10 ms – 1 s. The header advertises 5–20 s. | A 5 s period returns an unexplained `-EIO`. The 5–20 s cadence belongs to the `capture()` path. |
| **INT pin** | Armed before the sensor is powered, on a pin with no pull, never disarmed on `TURN_OFF`. | A floating CMOS input with an armed edge interrupt, on a board whose point is a multi-month battery figure. |
| **`&clock` enabled** | Pulls in an **unbounded** LFCLK spin-wait at PRE_KERNEL_2, for a feature that is compiled out. | Silent hang if the 32.768 kHz crystal is ever absent or slow. |
| **Boot time** | ~7 s worst case blocking boot on the failing path; the 600 ms probe budget buys only two tries against a 500 ms TWIM timeout. | Also: the 2026-09-06 log gap may have been the main thread asleep in init, not RTT overflow. |
| **54x42 reads** | ~306 ms at 400 kHz against a 500 ms transfer timeout. | Raise the timeout before the full-resolution sweep. |

## CLEARED by review — no longer open questions

**8 MHz AP_CLK is in spec.** UM3683 Table 11: `EXT_CLOCK` is plain Hz, legal range
**6–27 MHz**. This is not why the part is silent.

Also cleared: the boot sequence matches §2.5.2 (with our probe covering ST's missing
DEVICE_ID check); `set_com_config()` is not needed for a single I²C device; index is
big-endian and data little-endian, both correct; no `SWITCH_TO_FAST_CLOCK` is required
because every standby read we make is ≤ 4 bytes and all large reads happen while
streaming; the frame layout, the 100-byte ISL offsets and all six binning values are
right; the sync-mode ordering is exactly ST's; `SYNCHRO` really does reset to `SLAVE`
(Table 12), so that was never an assumption; GPIOTE mapping, pad readback,
`i2c_recover_bus()`, the PM state machine and the GRTC pinctrl are all correct.

**Nothing found in software explains the missing first ACK.** It stays a hardware question:
AP_CLK present and stable at IOVDD level, IO level shifting, and the supply.
