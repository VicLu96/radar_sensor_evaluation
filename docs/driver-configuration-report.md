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
| **Community** | The hardware-validated community driver, cited for ST's reference timing |

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
- **50 ms / 50 ms**: ST's reference timing, via **Community**. *(Second-hand. If a
  first-party number exists it should replace these.)*
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
| 3 | Write VDDIO config | `VDDIO_CFG` (0x000D) | — |
| 4 | Write VDDA config | `VDDA_CFG` (0x000C) | — |
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

Square formats (8x6, 24x20) transmit a larger square array and crop on-device, so **their
field of view differs from the wide formats**. The driver warns. Any energy-versus-zones
sweep must stay inside one family.

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
2. **No repeated start, ever.** A read is START/write-index/STOP, then START/read/STOP —
   two complete transactions. `i2c_write_read_dt()` is wrong here. *(Source: datasheet
   "known limitations", and ST's own platform layer uses `I2C_PRIVATE_WITHOUT_ARB_STOP` on
   both halves.)* The failure mode is why this matters: a repeated start does not merely
   fail the transfer, it latches the device into NAK-everything until a clean STOP escapes
   it — so the first bad read makes the sensor look dead from then on.
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
| SYNC_IN must not float | §2.7.2 | ⚠️ driven if routed; otherwise **must be tied high on the board** |
| Default address 0x29, 7-bit | §2.8.1 | ✅ |

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
