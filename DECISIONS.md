# Decision Log (append-only)

Newest entries at the bottom. Never edit or delete a past entry — if a decision is
reversed, append a new one referencing the old. An edited past entry is a bug.

```
## YYYY-MM-DD — [Decision]
What:
Why:
Expected to be wrong if:
```

---

## 2026-08-31 — `main` created as an orphan branch; stocks CLI left alone
What: The repository's only branch was `claude/stocks-2vn27c`, containing a
stock-price CLI unrelated to radar or ToF work — the output of a one-word "stocks"
prompt aimed at this repo while it was empty. Radar work now lives on a new orphan
`main` with no shared history; the stocks branch is untouched.
Why: The two share nothing. An orphan branch keeps the histories clean and leaves the
stocks work recoverable rather than deleted.
Note: `origin/HEAD` currently points at `claude/stocks-2vn27c`, so the stocks branch is
still GitHub's default. Worth repointing to `main` once pushed. The stocks README's
claim that deleting the branch "removes it entirely" is stale — that branch *is* the
default, and deleting it would empty the repo.
Expected to be wrong if: Victor wants the stocks tool kept and developed, in which case
it belongs in its own repository rather than on a branch here.

## 2026-08-31 — I²C, not I3C
What: The host interface to the VL53L9CX is I²C. The part supports both on shared
SDA/SCL, with I3C reaching 12.5 MHz after dynamic address assignment.
Why: Victor's decision. Far simpler bring-up, and whether the nRF54L15 has an I3C
peripheral at all is unconfirmed.
Consequence, stated so it is not rediscovered later: a full 54×42 frame is roughly 9 KB,
so transfer costs ~225 ms at 400 kHz or ~90 ms at 1 MHz. **The bus, not the sensor, caps
frame rate at roughly 4–10 fps** and is a significant share of per-frame energy. The
target application wants ~1 Hz, so this is an acceptable trade — but it forecloses any
future high-frame-rate work and belongs in the paper as a stated limitation.
Expected to be wrong if: reduced-resolution modes turn out unavailable *and* the
application needs more than a few fps — then I3C becomes necessary and the MCU choice
needs re-examining.

## 2026-08-31 — The paper is an energy characterisation, not a people counter
What: The contribution is the energy-accuracy trade-off of high-resolution dToF sensing
— energy per frame broken down by phase, accuracy versus zone count, and a measured
battery operating point. Counting people is the vehicle, not the claim.
Why: People counting is solved and commercially deployed; a paper reporting good
counting accuracy is a datasheet with citations. What nobody can look up is what 2268
zones cost in energy and how much of that resolution counting actually needs.
Expected to be wrong if: the VL53L9CX exposes only one resolution mode, which collapses
the central curve to a point. Fall back to frame rate and duty cycle as the swept axis.
Also wrong if ST or a competitor publishes the same characterisation first — the part
is months old, so this is a real deadline rather than a hypothetical.

## 2026-08-31 — Custom PCB only: no DK, no ST evaluation board
What: Development targets a single custom PCB carrying the ISP2454-LL and the
VL53L9CX. Victor writes the Zephyr board files and has a Power Profiler Kit II. There
is no nRF54L15-DK and no STEVAL-VL53L9 / X-NUCLEO-53L9A1.
Why: Victor built the hardware; buying reference boards to duplicate it is avoidable
cost, and the firmware abstraction is genuinely unaffected — a board file is a board
file.
Consequence, which is NOT neutral and is the reason this is recorded: the plan had used
"DK first, always" as its main bring-up risk control. Without a known-good reference, a
silent sensor has four suspects at once — assembly, board design, our driver port, and
the init sequence — and no cheap way to separate them. The mitigation is replaced
rather than dropped: staged bring-up gates with the I2C address ACK (gate 1.2) as the
hardware/software divider, a logic analyser promoted to primary diagnostic instrument,
and the community VL53L9-Arduino port read as a reference init sequence.
Also promoted to Phase 0: whether the board can measure the sensor and MCU rails
separately. The paper claims a per-component energy breakdown, so a shared rail forces
either a shunt or a weaker differential measurement, and that is a schematic question
best answered before anything is built.
Expected to be wrong if: bring-up stalls at a gate for more than about a week with no
way to tell hardware from software apart. At that point one ST evaluation board becomes
much cheaper than the time being spent, and buying it is the right call rather than a
concession.

## 2026-08-31 — Four stages, power optimisation last
What: Driver -> BLE telemetry to a web interface -> people counting -> power
optimisation. The board can gate each power domain separately during idle.
Why: Victor's sequencing, and it is right. Optimising power before the algorithm exists
means optimising against a guessed frame rate, resolution and wake pattern, and any
architecture built on that guess later constrains the algorithm. Doing it last also
means the paper's measurements are taken against a working system rather than a stub.
One exception carved out: the firmware blob reload time must be measured during stage 1,
because it decides the entire stage-4 architecture and is nearly free to capture while
the driver is being brought up.
Expected to be wrong if: the sensor turns out to have no usable standby state, in which
case power architecture stops being a late optimisation and becomes a stage-1
constraint - every wake would pay a multi-second blob reload and the duty cycle would
have to be designed around it from the start.

## 2026-08-31 — ST driver used unmodified; we write only the platform layer
What: ST's ULD source stays byte-identical to their release. We implement the six
platform functions it calls (RdByte, WrByte, RdMulti, WrMulti, WaitMs, SwapBuffer) on
Zephyr I2C, wrap it in an out-of-tree module, and expose a small custom API.
Why: The ULD carries the init sequence, blob upload protocol, calibration and frame
unpacking, much of it undocumented outside the code and some of it timing-sensitive.
Rewriting means owning all of it with no reference when a frame comes back subtly
wrong. The port is a few hundred lines; a rewrite is the project.
Also decided: the public API is NOT Zephyr's sensor API. That API is built around scalar
channels fetched one at a time, and this device produces a 2268-zone frame - forcing it
through sensor_channel_get would mean either 2268 calls or a channel that lies about
what it returns.
Expected to be wrong if: ST's L9 driver turns out to differ enough from the
VL53L5CX/L8CX convention that the platform layer needs rewriting anyway, or if it is
distributed under a licence that forbids the integration shape we want.

## 2026-08-31 — Sensor facts source-verified from a community I2C driver
What: Could not fetch ST's X-CUBE-53L9A1 automatically — it sits behind a licence
acceptance on st.com, which is Victor's click to make rather than something to
automate. Instead cloned two BSD-3-Clause community drivers into vendor/:
VanBruce/vl53l9cx-python (pure-Python I2C, hardware-validated — our exact transport)
and earlynerd/VL53L9-Arduino (I3C via PIO).

Resolved from the Python driver's source, replacing five VERIFY items:
- Firmware blob: 9,865 bytes, patch v0.17, extracted byte-for-byte from ST's
  X-CUBE-53L9A1 v1.0.0 with documented provenance and SHA-256
- SIX resolution modes: 54x42, 24x20, 18x14, 12x10, 8x6, 4x4, set by a binning
  register. 24x20 and 8x6 transmit square arrays with an on-device crop
- Frame layout: three uint16 per zone (depth, amplitude, ambient) = 6 bytes/zone.
  Depth is 15-bit mm with a VALID flag in bit 15
- I2C address 0x29 7-bit (0x52 is the 8-bit form), 16-bit register indices

Two consequences that change the plan:

1. The blob is 9,865 bytes, not the ~84 KB extrapolated from the VL53L8CX. At
   400 kHz that is ~250 ms — about one full-resolution frame read. This REVERSES
   the earlier conclusion: full power-down almost certainly beats standby at any
   duty period beyond a fraction of a second, so the architecture should default
   to TURN_OFF between readings.

2. Six resolution modes spanning 2268 down to 16 zones — 142x in zones, ~150x in
   bus time. The paper's central energy-accuracy curve has six real points across
   two orders of magnitude, which is the best news the project has had.

New open question that matters to the central claim: does binning preserve the
field of view or narrow it? The crop offsets on the square formats suggest zones
are merged within the same FoV, but if low-resolution modes see a smaller area
then accuracy-vs-zones is not a like-for-like comparison.

Expected to be wrong if: the community driver diverges from ST's official package —
it is a port, not the authority. Re-verify against X-CUBE-53L9A1 once downloaded,
particularly the register offsets, which the author notes were ported against patch
version 0.17 specifically.

## 2026-08-31 — Full 54x42 resolution, frame rate traded for I2C bandwidth
What: Run the sensor at full 54x42 and lower the frame rate to fit I2C, rather than
using a reduced binning mode.
Why: Victor's decision, and it is the right one for the contribution. Resolution is what
makes the "two people abreast" claim, which is the only thing distinguishing this sensor
from a 64-zone part costing a fraction as much. Frame rate is the cheaper thing to
spend.
Consequence, worked out in docs/plan/frame-rate-budget.md rather than discovered during
experiments: a 54x42 frame is 13,608 bytes, so ~370 ms at 400 kHz and a ceiling of
~2.7 fps. A person walking at 1.4 m/s under a 2.8 m mount is in view at head height for
only ~0.8 s, which at 2.7 fps is about TWO frames - enough to notice something passed,
not enough to track, establish direction, or separate two people. At 1 MHz it is ~5
frames, which works.
So the maximum I2C clock is promoted from a tuning detail to a GO/NO-GO item, and mount
height becomes a design parameter rather than a convenience - 3.5 m instead of 2.8 m
buys 75% more time in view.
Also motivates an event-triggered hybrid: 4x4 continuously at 96 bytes a frame to answer
"is anything there", bursting to 54x42 only while someone is crossing. Average energy
then follows doorway traffic rather than the clock, and it preserves the resolution claim
exactly where it matters. This is a better paper result than a fixed-rate sweep.
Expected to be wrong if: 1 MHz is unavailable AND the ceiling cannot go higher, in which
case full resolution at a fixed rate cannot track walking people and the hybrid stops
being an optimisation and becomes mandatory.

## 2026-08-31 — Scope is room occupancy where people dwell, not doorway counting
What: The target is monitoring a room where people stay for a while, not a highly
dynamic environment. Lead application becomes room / desk-cluster occupancy and dwell;
doorway counting drops to a secondary demo of the same pipeline.
Why: Victor's scoping.
What it fixes: the frame-rate crunch disappears. Dwell needs 0.05-0.2 Hz rather than
3-5 fps, so 54x42 over 400 kHz is comfortable, the 1 MHz question drops back to a tuning
detail, and the duty cycle falls to a few percent - which makes full power-down clearly
correct and the multi-month battery claim a comfortable margin rather than a stretch.
What it breaks, and it is not small:
1. Coverage. 54 x 42 degrees at 2.8 m gives roughly a 3 x 2 m floor patch - a desk
   cluster, not a room. This was harmless for a doorway, which is narrow by definition,
   and is now the binding constraint on the whole test setup.
2. The hard problem moves from timing to segmentation. A doorway counter can lean on
   motion; a person sitting still for forty minutes is, to a depth sensor, furniture.
   Background models WILL absorb stationary people given hours of frames at 0.1 Hz, and
   the failure is silent - occupancy quietly reads zero while the room is full, which is
   the worst possible failure for an HVAC or safety application.
Approaches recorded in docs/plan/room-occupancy.md, including one worth testing early:
depth is reported as 15-bit millimetres, and a seated person is never perfectly still
while furniture is. Millimetre-scale variance between frames minutes apart may separate
occupants from objects with no motion tracking at all. Whether the noise floor permits
it is unknown and a static-scene noise characterisation answers it cheaply.
Expected to be wrong if: the intended room is much larger than one unit covers, in which
case either the scope narrows to "desk cluster" explicitly or the product becomes
multi-unit, which is a different paper.

## 2026-09-01 — X-CUBE-53L9A1 acquired; ST's BSD-3-Clause driver tracked in-repo
What: Victor supplied X-CUBE-53L9A1 (STM32CubeExpansion_53L9A1_V1.0.0). The full 39 MB
package lives at `vendor/x-cube-53l9a1/` and stays gitignored. The two BSD-3-Clause
pieces we build against are copied byte-identical into the repo and ARE tracked:
`Drivers/BSP/Components/vl53l9/` to `firmware/drivers/vl53l9cx/st/`, and
`Utilities/vl53l9-common/` to `firmware/drivers/vl53l9cx/st-reference/`.
Why: the earlier rule ("ST packages are licensed, never vendored") was written before
the terms were read. The package SBOM in `Package_license.md` licenses the driver and
the reference platform port as BSD-3-Clause; only the middleware and the NUCLEO demo
projects are SLA0111, and neither is needed. Tracking the driver makes the build
reproducible from a clone; excluding the SLA parts and the 35 MB of STM32H5 HAL and
CMSIS keeps the repo honest and small.
Consequence: stage 1 is unblocked. Full audit in `docs/plan/st-package-audit.md`.
Expected to be wrong if: a future release relicenses the component, or ST's terms are
read differently by whoever reviews the paper's artifact release. The copyright notices
are retained in every file, which is what BSD-3-Clause asks for.

## 2026-09-01 — The platform scaffolding is wrong and will be rewritten
What: `firmware/drivers/vl53l9cx/vl53l9cx_platform.[ch]` was written to the
VL53L5CX / VL53L8CX ULD convention — six functions, `uint8_t VL53L9CX_RdByte(...)`, a
named `VL53L9CX_Platform` struct. ST's VL53L9 driver is a new generation and none of
that is right. The real contract is thirteen functions returning `int`, taking an
opaque `void *const p_dev`, including sized accessors (`read8/16/32`, `write8/16/32`)
that ST calls directly, an async DMA read, and three board-config getters.
Why: source-verified against `vl53l9_platform.h`.
Consequence: the rewrite is the immediate next task, and it is a smaller job than it
sounds — the opaque `p_dev` means the Zephyr `struct device *` passes straight through,
and there is no `SwapBuffer` to get right. Three genuine gains: `read_async` can be
stubbed for bring-up because the synchronous frame path is complete; the sized
accessors localise every endianness decision; and the three config getters (VDDA,
VDDIO, external clock) turn a hardware curiosity into a **blocking** schematic question,
because `vl53l9_init()` writes all three into the device and a wrong value
misconfigures the analogue front end rather than failing loudly.
What survived the audit unchanged: the strategy (implement the platform layer, never
modify ST's driver) and all three documented traps — 16-bit big-endian register index,
the firmware blob as one 9,865-byte write, and sub-tick `wait_ms`.
Expected to be wrong if: nothing here is guesswork; it is ST's header.

## 2026-09-01 — Frame sizes corrected; the energy-accuracy curve narrows and changes shape
What: two numbers the paper depends on were wrong, both from counting zone data only.
1. A 54x42 frame is **14,842 bytes, not 13,608** — the 1,134-byte DSS array and a fixed
   100-byte status line were missed (`vl53l9.c:65-84`). At 400 kHz that is ~404 ms
   rather than ~370 ms, and the bus ceiling is ~2.5 fps rather than ~2.7.
2. The span across the six modes is **72.8x, not ~150x**. The 100-byte status line is a
   fixed floor: at 4x4 the frame is 204 bytes, of which half is status.
Why: source-verified against ST's driver.
Consequence: correction 1 is immaterial for room dwell at 0.1 Hz and mildly worse for
the doorway demo, which was already marginal. Correction 2 matters more, and is
arguably a better result than the clean 150x would have been - the curve **flattens
hard at the low end**, so dropping below 12x10 buys almost nothing on the bus, and the
optimum sits mid-range rather than at the bottom. A fixed per-frame overhead that
dominates the cheapest mode generalises to any zone-count sweep on a serial bus, which
is a more portable finding than one part's numbers.
Also resolved: **binning preserves the field of view within the wide family only.**
54x42, 18x14 and 12x10 merge zones with no crop; 24x20, 8x6 and 4x4 transmit a square
array with an on-device crop window and cover a different vertical field
(`vl53l9_set_binning()`). So the paper's central curve is binning 2 / 6 / 8 - three
like-for-like points spanning 2268 to 120 zones, 18.9x - and the square modes are
reported separately. This closes the open question raised on 2026-08-31.
Expected to be wrong if: integration time, not bus time, dominates at low resolution -
which would change the shape of the energy curve without changing these byte counts.
Still VERIFY, and now the most valuable cheap measurement at bring-up.

## 2026-09-01 — The I2C address is settled: 0x29, and ST's sample has a shift bug
What: the 7-bit address is 0x29. `VL53L9_DEFAULT_ADDRESS (0x52)` is the 8-bit form.
Why: ST's own driver decides it. `vl53l9_set_com_config()` writes `address >> 1` into
the device's address register and `vl53l9_get_com_config()` reads it back shifted left
(st/vl53l9.c:203-227). So ST treats their `address` field as the 8-bit form throughout,
and their STM32 sample passing 0x52 into a HAL field documented as 7-bit is a bug in the
sample rather than evidence of a second address. This closes the VERIFY opened earlier
today.
Consequence: devicetree `reg = <0x29>`. Still probe it rather than assume, and probe it
individually in read-byte mode - not with a general i2cdetect sweep, which uses empty
START+STOP transactions the device does not support and can wedge it.
Expected to be wrong if: the board straps the device to a non-default address.

## 2026-09-01 — Stage 1 driver written in full, against ST's API, without a compiler
What: the complete Zephyr port now exists - `vl53l9cx_platform.c` (ST's 13 functions),
`vl53l9cx.c` (init, PM, frame plumbing), `vl53l9cx_private.h`, a rewritten devicetree
binding, and a corrected public API. The old L5/L8-shaped scaffolding is gone.
Why: Victor asked to prepare the port while the three board values are still outstanding.
Everything except those three values is knowable from ST's source, and waiting would
have left the whole of stage 1 idle for a schematic lookup.
What is verified, and what is not - stated plainly because the difference matters:
- VERIFIED: all 13 platform signatures against st/vl53l9_platform.h; every ST function
  the driver calls, checked name-by-name against st/vl53l9.h; the frame wire layout
  (three PLANES then DSS then a 100-byte status line, little-endian, depth in bits 14:0)
  against vl53l9_get_frame() and ST's own parse helper; binning geometry and buffer
  sizes against vl53l9_set_binning() and RAW_BUFFER_SIZE.
- NOT VERIFIED: it has never been compiled. There is no C toolchain on this machine and
  no nRF Connect SDK workspace. First build will surface ordinary mistakes.
Three design decisions worth recording:
1. `p_dev` is the Zephyr `struct device *` passed straight through. ST never
   dereferences it, so the port needs no shadow struct - this is why the new contract is
   easier than the one the scaffolding assumed, not harder.
2. `vl53l9_read_async()` returns VL53L9_ERROR_PLATFORM rather than quietly running
   synchronously. A caller that believes a transfer is in flight and reads early gets a
   torn frame, which looks exactly like a sensor fault. The synchronous frame path is
   complete without it.
3. The three board values are `required: true` in the binding, with no defaults. A
   board file that omits one fails to build. Defaulting them would produce firmware that
   runs and lies, because a wrong VDDA or VDDIO misconfigures the analogue front end
   rather than failing loudly.
Also changed the public API: `vl53l9cx_start()` now takes a period in milliseconds
rather than a rate in `uint8_t` Hz, which could not express the 0.05-0.2 Hz that room
dwell actually needs, and a `vl53l9cx_capture()` single-shot entry point was added
because that - not autonomous streaming - is the dwell path that makes the duty cycle
low enough to matter.
Expected to be wrong if: the first compile shows a Zephyr API has moved, or the frame
orientation needs a flip - the hardware-validated Python driver flips 180 degrees by
default and this driver deliberately does not, leaving orientation to the board file.

## 2026-09-01 — If the MCU must source AP_CLK, PWM is probably the wrong peripheral
What: the binding accepts `clock-pwms` so the nRF54L15 can generate AP_CLK, and the
driver starts it before any I2C contact and gates it on TURN_OFF. But 12 MHz is a
demanding ask of a general-purpose PWM: from a 16 MHz base a divider gives 16, 8 or
5.33 MHz, not 12, and an 83 ns period leaves no duty resolution.
Why it is recorded rather than solved: which way this goes depends on the schematic. If
the board carries its own oscillator the question disappears. If not, the options are a
clock output or a TIMER/GPIOTE/PPI path instead of PWM, or - since the legal range is
6-27 MHz - choosing a divider-friendly frequency such as 8 MHz and writing that into
`ext-clock-frequency`.
Consequence: the devicetree can express the intent either way, so this does not block
the port. It does block bring-up if the board has no oscillator.
Expected to be wrong if: the nRF54L15 PWM has a clock source or mode that reaches 12 MHz
cleanly - worth ten minutes with the product specification before building anything
exotic.

## 2026-09-01 - No repeated start: a bug in the port, found by checking a claim
What: reads are START/write-index/STOP then START/read/STOP - two separate I2C
transactions. `i2c_write_read_dt()`, which emits a repeated start between index and
data, is WRONG for this part and has been removed from `vl53l9cx_platform.c`.
Why: the device does not support a repeated start between the index write and the data
read (datasheet "known limitations", via the hardware-validated community Python
driver). It does not fail cleanly - it latches into NAK-everything until a clean STOP
escapes it, so the first bad read poisons every later one and the sensor presents as
dead. ST's own legacy-I2C path does the same split: both phases use
I2C_PRIVATE_WITHOUT_ARB_STOP ("Stop between each I2C Private message") issued as two
separate HAL transactions (st-reference/vl53l9/vl53l9_platform.c, _i3c_read). Only their
DMA path uses I2C_PRIVATE_WITH_ARB_RESTART.
How it got in: the original scaffolding chose one transaction with a repeated start on
the general reasoning that it cannot lose the register index on a multi-master bus. That
reasoning is sound for most I2C parts and wrong for this one. It was found while
checking whether AP_CLK is used in I2C mode - not by reviewing the code, which three
passes over that file had not caught.
Consequence: on a multi-master bus another master can now interleave between index and
data. The part gives no choice; if a second master is ever added the answer is bus-level
locking, not a repeated start.
Correction to the 2026-09-01 address entry above: ST's STM32 sample is NOT buggy. Their
platform layer shifts the target address right by one in the legacy-I2C branch of both
_i3c_read and _i3c_write, so 0x52 becomes 0x29 on the wire. The conclusion (0x29 is the
7-bit address) is unchanged and now has two independent confirmations in ST's own code.
Expected to be wrong if: an erratum lifts the limitation for some silicon revision - but
the failure is severe enough that the split is worth keeping regardless.

## 2026-09-01 - AP_CLK is the sensor's system clock, and is required in I2C mode
What: AP_CLK is not a bus signal and is not tied to the interface choice. It is the
clock the sensor's digital core runs on. ST's command set makes it explicit:
COMMAND_SWITCH_TO_EXT_CLOCK (0x8) "turn off the pll and switch the system clock to the
external clock", COMMAND_SWITCH_TO_FAST_CLOCK (0x7) "turn on the pll and switch the
system clock to the fast clock" (st/vl53l9.c:102-103). Both sources derive from AP_CLK -
the external clock directly, the fast clock through a PLL that locks to it, and there is
a pll_lock error bit in the status word showing the PLL is not free-running.
Why it is not bus-specific: vl53l9_init() writes VL53L9_REGADDR_EXT_CLOCK first,
unconditionally, before anything about the output interface is configured, and the
register lives in BOOT_SETTINGS. ST's interface header keeps bus type and clock
configuration as separate orthogonal fields. Nothing makes the clock conditional on I3C
or CSI-2.
Consequence: SCL clocks the bus; AP_CLK clocks the sensor. Both are needed. Confirmed on
the I2C side by the community Python driver, which runs plain I2C on Linux and specifies
6-27 MHz at IOVDD level, +/-100 ppm, with no clock meaning no ACK at all.
Expected to be wrong if: the device clocks its I2C slave front-end from SCL and answers
basic register reads without AP_CLK. That would be unusual and would not change the
requirement - the FSM those reads interrogate still needs a core clock - but it is the
one part of this that rests on a community README rather than on ST's source. A scope on
AP_CLK during the first probe settles it.

## 2026-09-01 - AP_CLK set to 8 MHz, not the reference design's 12 MHz
What: `ext-clock-frequency = <8000000>` in the board file, with the MCU generating it on
a PWM channel.
Why: Victor's call, and the arithmetic supports it. The sensor accepts 6-27 MHz. A
16 MHz PWM base clock divides to 16, 8 and 5.33 MHz - 12 MHz is not reachable (it would
need COUNTERTOP = 1.33), and 5.33 MHz is below the sensor's 6 MHz minimum. 8 MHz is the
only frequency in the legal window an nRF PWM can produce exactly from a 16 MHz base,
which makes it the right choice rather than a compromise.
Consequence: this board deliberately does not copy ST's 12 MHz reference configuration.
The value is written into the device by vl53l9_init(), so the register and the pin agree
as long as ext-clock-frequency and the clock-pwms period cell agree - both are set to
8 MHz / 125 ns in the board DTS.
Still VERIFY, and it is the first thing to check on a scope: 8 MHz means COUNTERTOP = 2
and a duty of exactly one tick. Some nRF PWM hardware requires COUNTERTOP >= 3, which
would cap this path at 5.33 MHz - BELOW the sensor's minimum. If that is the case the
answer is not another frequency but another mechanism: a TIMER toggling a pin via
GPIOTE/DPPI, or an oscillator fitted to the board.
Expected to be wrong if: the nRF54L15 PWM has a base clock other than 16 MHz, which
would change every number above.

## 2026-09-01 - Board file, app and west manifest created; three placeholders in them
What: a Zephyr board definition (`firmware/boards/pbl/vl53l9_node`, target
`vl53l9_node/nrf54l15/cpuapp`), a bring-up application (`firmware/app`), and a west
manifest pinning the nRF Connect SDK.
Why: Victor asked for them, having earlier owned the board files himself. Nothing here
is a substitute for the schematic - it is the scaffolding that makes the schematic the
only remaining input.
What is real and what is a placeholder, because the difference decides what breaks:
- REAL: the structure, the driver wiring, 8 MHz AP_CLK, the 0x29 address, 400 kHz I2C,
  and the gate order in the app.
- PLACEHOLDER: every pin number in the pinctrl dtsi; VDDA (2.8 V) and VDDIO (1.8 V),
  which are ST's reference values and not measurements of this board; the SDK revision
  in west.yml; the flash partition sizes.
- LIKELY WRONG: the peripheral instance names. The nRF54L15 numbers serial peripherals
  by power domain, so there is no `i2c1` - the instances are in the 20s and 30s. `i2c21`
  is used as the placeholder Victor asked for, and `pwm20`, `uart20`, `gpiote20`,
  `cpuapp_sram` and `cpuapp_rram` need the same check against the installed SDK's
  nrf54l15_cpuapp.dtsi. These are confined to the board DTS and its pinctrl file.
Mitigation rather than hope: the app's gate 0 prints VDDA, VDDIO, AP_CLK and the address
from devicetree before touching the sensor, so a wrong placeholder is visible in the
first line of console output rather than being diagnosed later as a sensor fault.
Two smaller decisions: the sensor rail is a plain `power-gpios` line rather than a
regulator-fixed node, because the driver already sequences it against XSHUT and AP_CLK
and a regulator would put a second consumer on the same pin. And the driver now selects
PWM unconditionally rather than conditionally on `clock-pwms` being present - a little
flash on a board with its own oscillator, against a conditional select that fails
obscurely on a first build.
Expected to be wrong if: the ISP2454-LL module does not expose the pins these
peripherals need, which would move the assignment rather than the design.

## 2026-09-01 - Two build-blocking defects found by auditing the board wiring
What: fixed before the first build attempt, both found by cross-checking the DTS against
the binding against the driver macros rather than by reading the code again.
1. `clock-pwms` could never have worked. Zephyr's PWM_DT_SPEC_GET macros expand through
   DT_PWMS_CTLR_BY_IDX, which is hardwired to a property literally named `pwms` - there
   is no by-property-name variant. The property is renamed to `pwms` with
   `pwm-names = "apclk"` for readability. This would have failed at devicetree macro
   expansion with an error naming neither the property nor the driver.
2. `zephyr,code-partition = <&slot0_partition>` was set with no bootloader in the build.
   The image would have linked and flashed at 0x10000 and the chip would never have
   jumped to it - a dead board, at exactly the moment when the sensor, the clock, the
   pins and the address are all still unproven and nothing is trusted. Removed, with a
   comment saying to add it back alongside sysbuild and MCUboot rather than before.
Why it is worth an entry: both were invisible to reading. They came out of a mechanical
cross-check - every property in the DTS against every property in the binding against
every DT macro in the driver - which is now the thing to do before claiming any of this
is ready.
Also corrected the binding's prose: ST's STM32 sample is not buggy about the address, it
shifts 0x52 down to 0x29 in the legacy-I2C branch of its platform layer.
Expected to be wrong if: nothing here is judgement - both are mechanical facts about
Zephyr.

## 2026-09-04 - Victor's board files are off limits to Claude, absolutely
What: everything under `firmware_nrf_board_testing/boards/` - the `water_sense_board`
definition - is Victor's. Claude must never edit those files: not to fix them, not to
reformat them, not to add a node, not even when they are the direct cause of a build
failure. Report and wait.
Why: Victor's instruction, and it is the right division. He owns the hardware and the
schematic; a board file edited by someone who cannot see either is a silent way to
introduce a fault that presents as a firmware bug.
Consequence: hardware the driver needs - the VL53L9CX node, the AP_CLK PWM, the I2C
speed - goes in an application-level `.overlay` in the app directory. That is not a
board file, and it is the correct Zephyr mechanism for exactly this, so nothing is lost.
Recorded in CLAUDE.md under Hard rules.

## 2026-09-04 - The fitted module is the ISP2454-LX, not the -LL
What: Victor confirmed the part is the **LX** variant. The repo said "-LL" throughout,
from the 2026-08-31 research pass, where it was already flagged that -LL/-LX/-LP share a
footprint and the distinction was unverified.
Consequence: corrected in CLAUDE.md, CONTEXT.md and docs/hardware/mcu-isp2454ll.md. The
filename keeps the old name so history stays greppable.
Still VERIFY: what actually differs between -LL and -LX. If it is only the RF front end
or antenna option, nothing in this project changes. If it changes available pins or
memory, the pin map and partition table are affected. Worth ten minutes with the Insight
SiP datasheet before the first build, not after.

## 2026-09-04 - The board file and the SDK pin are from different nRF54L15 generations
What: `water_sense_board` is written against the nRF54L15 **preview** generation - it
includes `nordic/nrf54L15_M33.dtsi`, selects `SOC_NRF54L15_M33`, and uses the
**hardware-model v1** board layout (`boards/arm/<board>/` with `Kconfig.board`,
`Kconfig.defconfig`, `<board>_defconfig`). `west.yml` is currently pinned to NCS v2.9.0,
which is a later generation: SoC dtsi `nrf54l15_cpuapp.dtsi`, symbol
`SOC_NRF54L15_CPUAPP`, and hardware-model v2 boards (`boards/<vendor>/<board>/` with
`board.yml`, board targets like `board/nrf54l15/cpuapp`).
Why it matters: these are not interchangeable. Under the v2.9-era SDK the board's include
path and SoC symbol do not exist, and hardware-model v1 is deprecated or removed
depending on the Zephyr version underneath.
Resolution: the board file cannot move, so **the SDK pin moves**. Blocked on Victor
saying which nRF Connect SDK version he actually has installed - that single answer
decides the pin, and probably decides several of the review findings too.
Expected to be wrong if: his SDK is new enough that hardware-model v1 is gone entirely,
in which case the board files need migrating and that is his call, not a pin change.

## 2026-09-04 - SDK pinned to NCS v3.3.0, which makes the board migration mandatory
What: `west.yml` now pins nRF Connect SDK v3.3.0, the version Victor has installed.
Why: the pin had to move because the board file cannot. But 3.3 is a bigger jump than
the earlier v2.9.0 guess, and it converts "the board file is from an older generation"
from an inconvenience into a hard block: hardware-model v1 board layouts are not
discovered at all by the Zephyr 4.x line NCS 3.x is built on, and neither
`nordic/nrf54L15_M33.dtsi` nor `SOC_NRF54L15_M33` exists in that tree.
Consequence: `water_sense_board` must be migrated to hardware-model v2 and current
nRF54L15 naming before anything builds. **No application overlay can work around it** -
the failures are in the board's own includes and chosen nodes, which are resolved before
overlays are merged. Since board files are Victor's, the migration is his.
Full findings and ready-to-paste snippets in docs/hardware/water-sense-board-review.md.
Expected to be wrong if: NCS 3.3 retains a hardware-model v1 compatibility path. Worth
one check against zephyr/boards/nordic/nrf54l15dk/ in the installed tree, which is the
authoritative template and settles every naming question in this entry at once.

## 2026-09-04 - Partition table proposed at 1428 KB, not 512 KB
What: a partition layout spanning the application core's full 1428 KB of RRAM -
mcuboot 64K, image-0 668K, image-1 668K, storage 28K, ending exactly at 0x165000.
Proposed only: it lives in a board file and Claude does not edit those.
Why: the current table covers 512 KB of 1428 KB, leaving about two thirds of the memory
unmapped, and its 220 KB image slots are tight for a build that already carries ST's
driver and a 9,865-byte firmware blob and will later carry BLE. Victor's own
`water_sense_board.yaml` already states `flash: 1428`, so the numbers were right and only
the partitions had not caught up.
A no-MCUboot variant is offered alongside it - one 1400 KB image partition and no
`zephyr,code-partition` - because it also removes the "image links at 0xc000 and the
chip never jumps to it" failure, which is the worst thing to be debugging on a board
where nothing else is proven yet.
Expected to be wrong if: the FLPR core is to be used and needs its own RRAM region, in
which case the top of the map shrinks and every figure above moves.

## 2026-09-04 - SPI chip select is driven by the port file, not by Zephyr
What: CS on P2.05 is handled in software in the port file. Victor's decision. The
earlier review finding ("cs-gpios is missing") is withdrawn - the omission is deliberate.
Why it is fine: SPIM does not drive chip select itself; Zephyr's driver just toggles a
GPIO from `cs-gpios`. A port layer toggling the same pin does what the driver would have
done, and holding CS across several transactions is the normal arrangement for
SD-over-SPI.
The consequence that does need settling: the board file already contains an `sdhc0` node
(`zephyr,sdhc-spi-slot`, `reg = <0>`, with a `zephyr,sdmmc-disk` child). That is Zephyr's
SD-over-SPI stack and it expects `cs-gpios[0]` on the controller. The two approaches
cannot coexist - once a SPI device has a cs-gpios entry Zephyr asserts and de-asserts
around every transfer, and manual toggling on top of that produces glitches that read as
card timeouts. So either `sdhc0` stays and the board gains `cs-gpios`, or the port file
owns CS and the `sdhc0`/`mmc` nodes come out. Victor's call, on his file.
Unchanged either way: `&gpio2` must be enabled. Pinctrl does not need the GPIO driver but
a software-driven pin does, so that finding stands whichever route is taken.
No bearing on the VL53L9CX, which is on I2C and has no chip select.

## 2026-09-04 - Board-file rule amended, and the board migrated at Victor's request
What: the "absolutely forbidden" rule on board files is amended to "hands off by
default, edit only when Victor asks in that message". He asked, so `water_sense_board`
was migrated to hardware model v2 and NCS 3.3 naming, AP_CLK was added on P0.13, and the
partitions were resized.
Why the amendment is recorded rather than just acted on: the prohibition was written
into CLAUDE.md two messages earlier at Victor's instruction. Quietly working around it
would leave the file lying about how this repo operates. The default has not changed -
only an explicit instruction in the current message authorises an edit, and it
authorises that edit only.
The pre-migration board files are recoverable at commit ec65298, unchanged.

## 2026-09-04 - AP_CLK on P0.13, 8 MHz, from PWM
What: `pwm20` drives P0.13 at 8 MHz for the VL53L9CX, added to the board file.
Why 8 MHz was already decided (16 MHz / 2 is the only exact frequency inside the
sensor's 6-27 MHz window). What is new is the pin.
Two things to verify at build time, both self-revealing:
1. That P0.13 exists on this SoC. On the nRF54L15 the P0 port is short - the bulk of the
   GPIO is on P1 and P2, which is where every other pin on this board sits. If P0.13 is
   a MODULE pin from the ISP2454-LX pinout rather than an SoC port.pin, the devicetree
   will say so by name.
2. That `pwm20` can reach P0. The nRF54L15 groups peripherals and pins into power
   domains and a peripheral cannot drive a pin outside its own. If it cannot, the fix is
   a different PWM instance, not a different pin.
Still VERIFY on a scope regardless: 8 MHz needs COUNTERTOP = 2 and a one-tick duty.

## 2026-09-04 - Board migrated: what changed and what was deliberately not decided
Migrated `water_sense_board` to hardware model v2 for NCS 3.3. Preserved unchanged:
every pin Victor assigned, the ADC configuration, the BT_CTLR default.
Changed: hwmv1 -> hwmv2 layout; `nrf54L15_M33.dtsi` -> `nrf54l15_cpuapp.dtsi` and the
SoC symbol with it; `flash0`/`sram0` -> `cpuapp_rram`/`cpuapp_sram`; partitions from
512 KB to the full 1428 KB with MCUboot dropped and no `zephyr,code-partition`, so a
plain build links at 0 and runs; `gpio1` and `gpio2` enabled; `i2c1` -> `i2c21` at
400 kHz; `spi2` -> `spi20`; RTT console added.
Deliberately NOT decided, and left visible instead: `sdhc0` is **disabled rather than
deleted**. With chip select owned by the port file it cannot work as written, but
deleting it would be choosing Victor's SD architecture for him. Disabled means the board
builds and boots either way, and the node carries a comment saying exactly which two
options exist.
Also not added: the VL53L9CX node. It belongs in an application overlay, so the board
file stays about the board.
Expected to be wrong if: the peripheral instance names. `i2c21`, `spi20`, `pwm20`,
`gpiote20/30` are chosen to match the pin domains but are the remaining guess in this
file. A "node does not exist" error means diffing against
zephyr/boards/nordic/nrf54l15dk/ in the installed SDK, which settles all of them at once.

## 2026-09-04 - Correction: SPI chip select is P0.00, not P2.05
What: Victor corrected the pin. CS is **P0.00**. The earlier entry on this page giving
P2.05 stands as written, per the append-only rule, and is superseded by this one.
Consequence, and it is small because CS was never in the devicetree: chip select is
driven in software from the port file, so it appears only in comments. Those are
corrected, along with the `cs-gpios = <&gpio0 0 GPIO_ACTIVE_LOW>` fallback in the review
document. No functional change to any board file - the diff is comment lines only.
Two things this does change:
1. `&gpio0` is no longer merely enabled out of caution. It is the port chip select lives
   on, so it has to be enabled, and it happens to be the one the original board file
   already had right.
2. **P2.05 is now free.**
Worth noting for the domain question: a plain GPIO has no power-domain tie to the SPI
peripheral, so CS on P0 alongside SPI signals on P2 is fine. Only pinctrl signals are
constrained to their peripheral's domain - which is exactly why AP_CLK on P0.13 still
needs checking and CS on P0.00 does not.

## 2026-09-04 - P0.13 does not exist; AP_CLK needs a real pin, and PWM may be the wrong source
What: verified against the installed SDK (C:/ncs/v3.3.0) rather than reasoned about.
`nrf54l_05_10_15.dtsi` gives gpio0 `ngpios = <7>`, gpio1 `<16>`, gpio2 `<11>` - so the
pins are P0.00-P0.06, P1.00-P1.15, P2.00-P2.10. **P0.13 is not a pin on this SoC.**
CS on P0.00 is fine. Every other assigned pin checks out: SCL P1.08, SDA P1.13, SCK
P2.01, SDI P2.04, SDO P2.02.
The dangerous part: this would NOT have failed at build time. NRF_PSEL() encodes port and
pin into an integer and nothing validates the pin exists, so the build would have
succeeded, the PSEL register would have selected nothing, and the result would be a
silent dead clock - which on this project means a VL53L9CX that never acknowledges its
I2C address and reads as a dead sensor. A previous entry claimed this was self-revealing
at build time. That was wrong. The pwm20 node is therefore disabled rather than left
hopeful.
Second finding, same source: **PWM cannot drive port 0 at all.** PWM_OUT appears only on
P1 (and P3 on other parts) across every Nordic board in the tree, and P0 carries only
uart30 and GRTC pin functions - P0 is the 30 power domain, pwm20/21/22 are 20-domain.
So even a valid P0 pin would not work with this PWM.
Third finding, and it is an improvement rather than a workaround: **GRTC has a fast clock
output**, `clkout-fast-frequency-hz`, and the binding's own example is literally
`<8000000>`. That is our exact frequency from a purpose-built clock output, with none of
the COUNTERTOP-of-2 marginality that makes 8 MHz a stretch on a PWM. Nordic routes
GRTC_CLKOUT_FAST to P1.08 on the DK.
Blocked on Victor: which pin is AP_CLK actually on, in SoC port.pin terms? P2.05 is free
since CS moved. If the routing allows a GRTC clkout pin, prefer that over PWM.

## 2026-09-04 - Every peripheral instance name in the migration was correct
What: checked all of them against the SDK instead of leaving them as the "remaining
guess". `i2c21`, `spi20`, `pwm20`, `gpiote20`, `gpiote30`, `gpio0/1/2`, `cpuapp_rram`,
`cpuapp_sram` and `adc` all exist in `nrf54l_05_10_15.dtsi`. `wdt0` does not - the
instances are `wdt30`/`wdt31`, so dropping that alias was right.
The partition table is exact: `nrf54l15.dtsi` sets `cpuapp_rram` to
`reg = <0x0 DT_SIZE_K(1428)>` and `cpuapp_sram` to 188 KB, which is precisely the
1428 KB the new table spans and the figure Victor's original .yaml already carried.
Consequence: the migration's naming is no longer a guess, and the build output confirms
it - devicetree resolved as far as
`/soc/peripheral@50000000/spi@c6000/sdhc@0/mmc`, which means every label reference
resolved before validation failed on a missing property.

## 2026-09-04 - The build error itself: disk-name, and it predates the migration
What: `zephyr,sdmmc-disk` marks `disk-name` required; the `mmc` node had no such
property. Added `disk-name = "SD"`, and set that node `status = "disabled"` to match its
already-disabled parent - devicetree validates any node that is enabled, regardless of
whether its parent is, which is why a disabled sdhc0 did not suppress it.
Why it is worth recording: this was in Victor's original board file unchanged and would
have failed identically on any SDK version. It is not a migration artifact.

## 2026-09-04 - Pin map settled: CS P2.05, AP_CLK P0.00, and AP_CLK moves to GRTC
What: the two corrections resolved. **CS is P2.05**, as originally given. **AP_CLK is
P0.00**, not P0.13. The 2026-09-04 entry recording "CS is P0.00" was a mis-attributed
correction - it was about AP_CLK - and stands unedited per the append-only rule,
superseded here.
Final map, every pin now checked against the SoC's real port sizes (gpio0 ngpios 7,
gpio1 16, gpio2 11): SCL P1.08, SDA P1.13, SCK P2.01, SDI P2.04, SDO P2.02, CS P2.05,
AP_CLK P0.00. All valid.
The consequence that matters: **AP_CLK moves from PWM to the GRTC fast clock output.**
PWM cannot drive port 0 - PWM_OUT appears only on P1 across Nordic's boards, and P0 is
the 30 power domain while pwm20/21/22 are 20-domain. GRTC can: the DK routes
GRTC_CLKOUT_32K to P0.04.
This is an improvement, not a workaround. From nrf_grtc_timer.c the divider is
`base / (requested * 2)` with base = pclk = 16 MHz, and the driver #errors above
base / 2. So 8 MHz is exactly the maximum and lands on divider 1 - no marginality, unlike
the COUNTERTOP-of-2 the PWM route needed. It is also the only usable value: the next step
down is 4 MHz, below the sensor's 6 MHz minimum. The frequency chosen for PWM reasons
turns out to be the only frequency GRTC can give us, arrived at independently.
Cost, and it is a real one for stage 4: the GRTC clock output is configured at boot and
runs continuously, with no runtime gate exposed by Zephyr. AP_CLK can no longer be
switched off with the sensor power domain the way the driver was designed to. Either
accept an always-on 8 MHz output and measure what it costs, or move AP_CLK to a P1/P2
pin on a PWM - gateable, but marginal at 8 MHz. Decide with the Power Profiler, not in
advance.
Driver consequence: the sensor node will have no `pwms` property, so the driver takes its
existing `clock_from_pwm = false` path and treats AP_CLK as a board-supplied oscillator.
That path already exists and is now accurate.

## 2026-09-04 - IT BUILDS. Three further board fixes, all found by actually compiling
What: the bring-up image links. FLASH 33,032 B of 1428 KB, RAM 7,672 B of 188 KB,
`merged.hex` generated for `water_sense_board/nrf54l15/cpuapp`.
This was done by running the build here, against Victor's own installed SDK at
C:/ncs/v3.3.0 with the toolchain at C:/ncs/toolchains/936afb6332 - not by reasoning about
it. Every remaining question about instance names, pin encodings and memory sizes is now
answered by artifacts rather than by argument.
Three fixes it forced, none of which any amount of reading would have found:
1. **`config BT_CTLR / default BT` aborts the build under NCS 3.3** - "defined without a
   type", because it is parsed before BT_CTLR has one. The board-level symbol is
   `HAS_BT_CTLR`, which is what Nordic's own nrf54l15dk uses. Same intent, survives the
   ordering.
2. **`&grtc` needs `status = "okay"`, and that is nothing to do with AP_CLK.** The GRTC
   is this SoC's system timer and the SoC dtsi ships it disabled. Without it
   CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC is never defined and the build dies inside
   sys/clock.h with "operator '==' has no left operand", naming nothing useful.
3. **`owned-channels` and `child-owned-channels` are required on `&grtc`.** The timer
   driver #errors without them. Values copied from Nordic's
   nrf54l_05_10_15_cpuapp_common.dtsi: all 12 owned, 3-4 lent to FLPR, 7-11 to Zero
   Latency IRQs.
Verified in the generated artifacts rather than assumed: `clkout-fast-frequency-hz` is
0x7a1200 = 8,000,000; the pinctrl psel is 0x37000000, which decodes to function 55
(GRTC_CLKOUT_FAST), port 0, pin 0 - so AP_CLK really is on P0.00 at 8 MHz. CONFIG_FLASH_SIZE
is 1428 and CONFIG_SRAM_SIZE 188, so the partition work is right.
One warning left, non-fatal: SB_CONFIG_PARTITION_MANAGER is enabled and deprecated. The
build works; worth turning off in sysbuild.conf when convenient, since this board uses
DTS partitioning.
Expected to be wrong if: nothing here. It compiled and linked. What it does NOT prove is
that any of it is correct on the actual hardware - the pins, the rails and the clock are
still only as right as the schematic they came from.

## 2026-09-04 - AP_CLK stays always on; measure it rather than design around a guess
What: accept the continuously running 8 MHz GRTC clock output on P0.00 and measure its
cost. Victor's decision. No board change - always-on is what the board already does.
Why it is the right default: the alternative was to contort the design around a number
nobody has, and this repo's rule is that estimates size decisions and never enter the
paper.
Two things recorded so the measurement actually happens, in
docs/plan/ap-clk-always-on.md:
1. **The clock term does not scale with duty cycle.** Everything else gets cheaper as
   the sensor is gated harder, which is the whole of stage 4. This does not move. So it
   grows as a fraction exactly as the work succeeds, and at the low-duty limit it sets a
   floor on the battery claim. A term that is 3% of the budget today and 30% after
   stage 4 is not noise, it is the result.
2. **The pin is probably not the expensive part.** Switching 10 pF at 1.8 V and 8 MHz is
   about 260 uW (estimate, capacitance assumed, sizing only). The term that decides it is
   holding the 16 MHz pclk domain up continuously, which is what stops the SoC reaching
   its deepest idle. That has to come off the Power Profiler.
The measurement is a clean A/B on the MCU rail with the sensor held off: idle current
as-built, then with `clkout-fast-frequency-hz` deleted and rebuilt. One rebuild, no extra
instrumentation. Do it BEFORE the stage-4 sweep, because if the term is large it changes
what that sweep means.
The escape hatch, and it is why accepting this now costs nothing: **reversible in
firmware, no respin.** `nrfy_grtc_clkout_set(NRF_GRTC, NRF_GRTC_CLKOUT_FAST, false)` is a
static inline in the nrfx HAL already linked into this build, so the driver's
clock_start/clock_stop - today no-ops on this board - can gate the output directly in a
few lines, in the place the design already has for it. That matters because the AP_CLK
trace is fixed to P0.00 and PWM cannot reach port 0, so "move it to a gateable pin" would
otherwise have meant a board respin.
Two things to confirm if we ever take that path: the sensor needs the clock before it
answers at all, so gating must sequence with TURN_ON (the driver's existing ordering
already does this); and GRTC is also the system timer, so confirm on the bench that
disabling only the output leaves the kernel timebase undisturbed - a subtly broken
timebase would poison every timing number the paper reports.
Also corrected today: the driver's TURN_OFF no longer claims to gate AP_CLK, in both the
README and the PM comment, and the driver now says at init that AP_CLK is board-supplied
and not gated - visible in the first lines of console output rather than buried in a doc.

## 2026-09-04 - Renamed firmware_nrf_board_testing to firmware_test, for path length
What: Victor renamed the folder. Git recorded it as 13 pure renames - 0 insertions, 0
deletions, content untouched - so history follows the files. The CMake `project()` name
went with it, which matters: sysbuild names a subdirectory after the project, so the
rename shortens the build path twice over.
Why: Windows' 260-character path limit, which is what stopped the build. The arithmetic:
  old  100 (build dir) + 27 (sysbuild subdir) + 137 (cracen object path) = 264  FAIL
  new   87              + 14                  + 137                      = 238  OK
22 characters of margin, which is thin but real. If it bites again the answer is
`west build -d c:/b`, which is how the build was verified working here earlier today.
Worth being clear that the error message is actively misleading: the compiler reports
"opening dependency file ... No such file or directory", which reads as a missing file
or a broken toolchain. Nothing in the firmware or the board files was ever wrong.
Note for the next build: the stale `firmware_test/build/` directory still carries the old
project name in its CMakeCache and generated devicetree. It is gitignored, but a
non-pristine rebuild against it would be confusing. Use `-p always` or delete it.
Historical entries above keep the old folder name, per the append-only rule.

## 2026-09-04 - LSM6DSV..BX IMU: planned, and there is no Zephyr driver for it
What: Victor has an LSM6DSV15BXTR (marking to be confirmed) on the board, on I2C, and
wants accelerometer XYZ printed over the RTT log. Planned only - no code written, at his
instruction. Plan in docs/plan/imu-lsm6dsv-bx.md.
What the SDK check turned up, and it changes the shape of the work:
1. **No in-tree Zephyr driver for any ..BX variant.** NCS 3.3 ships lsm6ds0, lsm6dsl,
   lsm6dso, lsm6dso16is, lsm6dsv16x and a family driver covering lsm6dsv320x, lsm6dsv80x
   and ism6hg256x. None of them is this part.
2. **The closest driver would reject it**: LSM6DSV16X_ID is 0x70, LSM6DSV16BX_ID is 0x71,
   and lsm6dsv16x checks WHO_AM_I.
3. **Loosening that check would be worse than useless.** OUTX_L_A is 0x28 on the 16X and
   0x2C on the 16BX - the accelerometer output block moved. A driver told to ignore the
   ID would read four bytes off and return plausible nonsense, which is the failure mode
   this project has already been bitten by twice.
What does exist: ST's HAL at modules/hal/st/sensor/stmemsc/lsm6dsv16bx_STdC/, present but
with no USE_STDC_LSM6DSV16BX Kconfig symbol, so not wired into the build. A small gap,
not a missing dependency.
Open and blocking the branch: **the exact part marking.** stmemsc ships lsm6dsv16b and
lsm6dsv16bx, not lsm6dsv15bx. If it really is a 15BX, ST's HAL for it is not in this SDK
and has to come from st.com the way X-CUBE-53L9A1 did.
Approach, staged so the cheap thing proves the expensive thing is worth doing:
- **Stage A**: about sixty lines in the existing test app, no driver at all. Read
  WHO_AM_I, set an ODR, burst-read six bytes, log XYZ. This proves the bus wiring, the
  pull-ups, the address strap and the part identity in one step - all four currently
  unverified, and a driver that fails to bind would not say which one was wrong. WHO_AM_I
  also settles the part question: 0x70 means the in-tree driver works as-is and stage B
  collapses to enabling it.
- **Stage B**: an out-of-tree driver wrapping ST's HAL, same pattern as the VL53L9CX
  port. Unlike that one, Zephyr's sensor API fits here - a handful of scalar channels,
  not a 2268-zone frame - so no custom API is warranted.
The IMU node goes in an application overlay, not the board file.
Hazard recorded for whoever debugs this bus: the VL53L9CX shares it, wedges on the empty
START+STOP transactions a general i2cdetect scan uses, and does not acknowledge at all
without AP_CLK running. Probe 0x6A and 0x6B specifically. No address collision with 0x29.

## 2026-09-04 - IMU address is 0x6B; memory protection disabled
What: the LSM6DSV..BX sits at I2C 0x6B (SA0 strapped high). No collision with the
VL53L9CX at 0x29. And memory protection is off at Victor's instruction:
CONFIG_ARM_MPU=n, CONFIG_HW_STACK_PROTECTION=n.
Where the MPU setting lives, and why: `firmware_test/prj.conf`, not the board defconfig.
prj.conf overrides the board default, so the effect is the same, and it keeps the board
file Victor's while letting the choice travel with the application that wants it. Say so
if it should be board-wide instead.
HW_STACK_PROTECTION goes with it rather than as an extra opinion - it is implemented
using the MPU, so leaving it enabled with the MPU off is unsatisfiable rather than
stricter.
Verified rather than assumed: rebuilt, and both symbols are absent from the generated
.config. FLASH fell from 33,032 to 31,452 bytes, which is the MPU code going away.
What it costs, recorded so it is a decision and not a default: a stack overflow now
corrupts adjacent memory silently instead of faulting where it happens. On a build that
will carry a 14.8 KB frame buffer and a 9.8 KB firmware blob, that is precisely the class
of fault it was most likely to have caught. Worth turning back on if anything starts
behaving inexplicably.
Stage A of the IMU plan now has everything it needs. The part marking remains open, and
stage A does not need it - reading WHO_AM_I is what answers it.

## 2026-09-04 - IMU stage A written: WHO_AM_I and accel XYZ over RTT
What: the bring-up app now probes the LSM6DSV..BX at 0x6B, identifies it, configures the
accelerometer and logs XYZ once a second. No driver, no overlay, no Kconfig - direct
register access with Zephyr's standard 8-bit-register I2C helpers. Builds clean:
FLASH 37,388 B, RAM 7,744 B.
Register facts taken from ST's headers in the SDK rather than from memory:
WHO_AM_I 0x0F, CTRL1 0x10 (ODR_XL [3:0], OP_MODE_XL [6:4]), CTRL8 0x17 (FS_XL [1:0]),
STATUS_REG 0x1E bit 0 = XLDA, and 0.061 mg/LSB at +/-2g from
lsm6dsv16bx_from_fs2_to_mg(). Configured for 60 Hz high-performance at +/-2 g.
Three deliberate choices worth recording:
1. **WHO_AM_I selects the output register base.** 0x28 on the 16X, 0x2C on the 16BX. The
   app refuses to read anything until it knows which, because reading the wrong base
   returns four bytes of neighbouring registers and two of real data - numbers that look
   like measurements and are not.
2. **CTRL1 and CTRL8 are read back after writing.** On an unproven bus a write that is
   silently dropped and a write that lands are indistinguishable otherwise, and that
   distinction is most of what this exercise is for.
3. **A magnitude check runs on every sample.** Whatever the orientation, a board at rest
   measures one gravity, so |a| outside 800-1200 mg warns and names the likely cause -
   full scale or register base, not the sensor. Integer sqrt, so no float printf support
   is needed.
IMU failure is non-fatal: the heartbeat continues, because "the MCU runs but the IMU does
not" is a state worth observing rather than a reason to stop.
Not done, and not needed for this: no devicetree node, no application overlay. Stage B in
docs/plan/imu-lsm6dsv-bx.md still stands for a real driver.
Expected to be wrong if: the part is neither 0x70 nor 0x71, in which case the app says so
and stops rather than guessing - and the bus is proven working by the fact that something
answered at all.

## 2026-09-04 - CORRECTION: the 16BX accel block did not move, its axes are reversed
What: the first hardware run of IMU stage A returned X drifting around zero with Y and Z
exactly 0 on every sample. Diagnosis: the code read six bytes from 0x2C. It should read
from 0x28.
The earlier entry on this page - "OUTX_L_A is 0x28 on the 16X and 0x2C on the 16BX, the
accelerometer output block moved" - is **wrong**, and stands unedited per the append-only
rule. What is actually true, from ST's headers side by side:
  addr   LSM6DSV16X    LSM6DSV16BX
  0x28   OUTX_L_A      OUTZ_L_A
  0x2A   OUTY_L_A      OUTY_L_A
  0x2C   OUTZ_L_A      OUTX_L_A
**The block base is 0x28 on both. The BX stores its axes in reverse order: Z, Y, X.**
LSM6DSV16BX_OUTX_L_A is 0x2C because X is last, not because anything moved.
How the mistake was made, since it is worth not repeating: I grepped for OUTX_L_A in both
headers, saw two different addresses, and concluded the block had moved - without listing
the neighbouring registers to see what the addresses actually meant. One symbol compared
across two parts, treated as if it characterised the whole block. The log said so
immediately and unambiguously: two axes reading exactly zero is a base-address error,
never a scaling error, because scaling cannot produce an exact zero twice a second.
Fixed by reading the block at 0x28 always and selecting an axis-offset table from
WHO_AM_I: {0,2,4} for the 16X, {4,2,0} for the 16BX.
What this changes about stage B, and it sharpens the argument rather than weakening it:
an in-tree lsm6dsv16x driver pointed at a 16BX would not fail loudly. It would read the
right six bytes and silently swap X and Z - a plausible gravity vector pointing the wrong
way, on a device where nothing looks broken. That is a stronger reason to write a proper
driver than the one originally given.
Also corrected: the magnitude warning now distinguishes the two failures it can see. Two
axes at exactly zero means the base address is wrong; three plausible-but-small values
mean the full scale is.

## 2026-09-04 - VL53L9CX driver integrated into firmware_test, and it COMPILES
What: the ToF driver is wired into the bring-up application. It captures a 12x10 frame
every five seconds and prints it as a grid of distances alongside a summary line.
FLASH 56,876 B, RAM 41,008 B - 21% of 188 KB, mostly the driver's 14,842-byte raw buffer
and the application's ~18 KB frame struct, both static.
**This is the first time that driver has ever been compiled.** It was written 2026-09-01
against ST's headers with no toolchain available, and every commit since has said so. It
built with no errors and no warnings from our code. The thirteen platform functions, the
frame unpacking, the PM actions and the devicetree binding all hold up.
How it is wired, and the shape is deliberate:
- The sensor node is an **application overlay**, not the board file. The board describes
  the board; the sensor's configuration travels with the application that uses it, and
  water_sense_board stays Victor's.
- The driver comes in as an out-of-tree module via EXTRA_ZEPHYR_MODULES in the app's
  CMakeLists, so nothing outside this repository needs configuring.
- No `pwms` property on the node, on purpose: AP_CLK is board-supplied from GRTC, so the
  driver takes its clock_from_pwm = false path and does not try to gate a clock it does
  not own. That path already existed and is now exercised.
Three properties are deliberately absent because their pins have not been given, and each
costs something worth naming rather than discovering:
- **xshut-gpios** - no deterministic reset, so a sensor left in a bad state stays there
  until the board is power-cycled by hand.
- **int-gpios** - frame-ready is polled, which keeps the CPU awake across integration.
  Fine for a bench test, wrong for the energy work.
- **power-gpios** - the sensor domain is never dropped, so TURN_OFF does nothing and the
  multi-month battery claim has no mechanism behind it.
All three belong in the overlay rather than the board file when the pins are known.
Still placeholders, and the one thing to confirm before trusting a reading:
**vdda-microvolt and vddio-microvolt** are ST's reference values, not measurements of
this board. A wrong rail misconfigures the analogue front end rather than failing loudly,
which is why the application prints both at startup.
On the log format: the grid is the point, not the statistics. A mean and a min/max can
look perfectly healthy over a frame of nonsense; a grid of distances either has the shape
of the scene in it or it does not, and a human sees that instantly. 12x10 rather than
54x42 because it is 880 bytes against 14,842 and sits in the WIDE family, so it shares
the full field of view rather than a cropped one.
Expected to be wrong if: it compiles, which is all this proves. Whether the sensor answers
depends on AP_CLK being real on P0.00, on XSHUT being tied high somewhere on the board,
and on those two rail values.

## 2026-09-04 - Every VL53L9CX board value now confirmed; no placeholders left
What: Victor supplied the last of it. VDDA 3.3 V, VDDIO 1.8 V (per the datasheet),
interrupt P0.01, XSHUT P1.07, power enable P0.02, AP_CLK 8 MHz on P0.00. All six are in
the application overlay and verified in the generated devicetree - vdda 0x325AA0,
vddio 0x1B7740, ext-clock 0x7A1200, power-gpios gpio0 pin 2, xshut-gpios gpio1 pin 7,
int-gpios gpio0 pin 1 with flag 0x1 (active low). Builds: FLASH 56,784 B, RAM 41,008 B.
**VDDA changed from the placeholder.** It was 2.8 V, ST's reference value; the board is
3.3 V. That is exactly the class of error the placeholder warnings existed for - it would
not have failed loudly, it would have misconfigured the analogue front end and returned
plausible rubbish.
Interrupt polarity is settled rather than assumed: ST's hw_config exposes only CMOS
versus open-drain (VL53L9_REGADDR_INTR_OUTPUT_MODE) with no polarity field, so it is
fixed by the part, and the hardware-validated community driver states plainly that INTR
is active-low. The driver arms GPIO_INT_EDGE_TO_ACTIVE, so it waits on a falling edge.
What the three GPIOs buy, now that they exist:
- **power-gpios** gives PM_DEVICE_ACTION_TURN_OFF something to do. The multi-month
  battery claim now has a mechanism behind it rather than an intention.
- **xshut-gpios** gives deterministic reset, so a sensor left in a bad state can be
  recovered in firmware instead of by power-cycling the board.
- **int-gpios** replaces polling, which stops the CPU spinning through the sensor's
  integration time - the exact energy this design exists to avoid spending.
New failure mode worth naming before it is met: with the interrupt wired, a missed edge
and a dead sensor look identical from the application - both are a capture that times out
at exactly 2 s. If everything else looks healthy and captures time out on the dot,
suspect the interrupt line before the sensor. The driver still has its polling path;
removing int-gpios from the overlay falls back to it and distinguishes the two in one
rebuild.
One hardware check, raised once because it is the kind that destroys parts rather than
wasting time: VDDIO is 1.8 V and every digital pin on the sensor - SDA, SCL, XSHUT, INTR,
AP_CLK - sits in that domain with an absolute maximum of 1.98 V. The nRF54L15 drives its
GPIOs at its own VDD. If the module runs above 1.8 V, those five lines need level
shifting. Presumably handled in the design, but it is worth one look at the schematic.

## 2026-09-05 - CSI-2 (DATA_P/N, CLK_P/N) is never used by this firmware
What: Victor asked whether the MIPI CSI-2 differential pairs must be connected or may
float. Firmware answer: they are never used, and no firmware change depends on them.
Evidence, from ST's driver rather than inference:
- `_init_default_config()`, run at the end of `vl53l9_init()`, writes
  `VL53L9_REGADDR_OUTPUT_IF = VL53L9_OUTPUT_I3C` (vl53l9.c:953). The register/I3C output
  path is ST's own default, not something we opt into.
- Our driver then sets `hw.output_interface = true` explicitly in configure_signalling(),
  so it is selected twice over.
- `vl53l9_start()` validates CSI settings only when `OUTPUT_IF == VL53L9_OUTPUT_CSI2`
  (vl53l9.c:584). On our path that check is skipped entirely, so a CSI configuration that
  would be invalid never matters.
- Frames come back through `vl53l9_get_frame()` over the register bus. Nothing in the
  data path touches the CSI transmitter.
So the CSI-2 transmitter is never enabled and never carries data. If the pairs are
floating today, nothing this firmware does will change that.
**Where the answer stops.** Whether unused D-PHY pads may be left floating is an
electrical question answered by the VL53L9CX datasheet's pin table, which is not in this
repository - X-CUBE-53L9A1 ships driver source, not the device datasheet. The general
case for unused differential OUTPUTS is that floating is acceptable, since they are
driven pins rather than high-impedance inputs, but ST occasionally specifies otherwise
and the failure mode there is EMI or leakage rather than anything that shows up in a log.
Worth one look at the datasheet pin description; not worth blocking on.
Also worth stating because it could otherwise waste time: this has no bearing on the
current bring-up failure. The sensor not booting cannot be caused by the CSI pins, since
the transmitter is not enabled at any point before or during boot.

## 2026-09-05 - The ToF failure is electrical: -ETIMEDOUT, not a NAK
What: repeated `errno -116` (-ETIMEDOUT) on the very first I2C index write, twice per
boot attempt and identical after a full power cycle, with a 500 ms rail settle. The bus
is electrically stuck. This is not a sensor fault and not a firmware fault.
Why the distinction is the whole diagnosis: an unclocked, unpowered or absent I2C slave
is PASSIVE. It does not drive SDA or SCL. With working pull-ups the master sees the lines
idle high, clocks out the address, gets no acknowledge, and returns -EIO in microseconds.
A timeout means the MASTER could not complete the transfer - SCL or SDA never reached the
level it was waiting for. Only two things do that: no pull-ups, or something holding a
line low.
**The strongest evidence is historical.** The LSM6DSV IMU worked on this same bus, same
pins, same 400 kHz, and read accelerometer data correctly. The bus was electrically sound
then. The IMU has since been removed and the bus now hangs. That points hard at the IMU
assembly having carried the SDA/SCL pull-ups, which the board's pinctrl does not provide
(no `bias-pull-up`, flagged in the 2026-09-04 board review and three times since).
Answering Victor's clock question, since it was asked and deserves a real answer:
**ST specifies no AP_CLK settle time anywhere.** Two independent places say so.
`vl53l9_init()` opens with `_wait_for_state(FSM_STATE_READY_TO_BOOT, 4)` - a **4 ms**
budget, so ST expects the part to be answering I2C within 4 ms of init being called. And
ST's own reference platform (st-reference/platform/platform_utils.c) contains exactly
four delays, all 50 ms, and every one of them is XSHUT sequencing - there is nothing
about the clock at all. Their `platform_power_enable()` is literally "XSHUT high, wait
50 ms".
So our 500 ms rail settle plus 50/50 XSHUT is already an order of magnitude more generous
than ST's own reference, which uses 50 ms in total. More delay cannot help, and the clock
cannot produce a timeout in any case - a missing clock gives a NAK, not a hung bus.
Added as a measurement rather than a fix: on timeout the driver now calls
i2c_recover_bus() once and reports what happened. If recovery frees the bus, a device was
holding it - real and fixable. If recovery changes nothing, the lines cannot reach a high
level at all, which is pull-ups or a short. The failure is as informative as the success,
which is the point.
Next action is a multimeter, not a rebuild: SDA and SCL to VDDIO with the board powered
and idle should read 1.8 V. Near 0 V settles it.

## 2026-09-05 - Pull-ups were the bus fault. -ETIMEDOUT is gone; now a clean NAK
What: adding `bias-pull-up` to the I2C pinctrl changed the failure from `errno -116`
(-ETIMEDOUT) to `errno -5` (no acknowledge). That is a different fault, and the change
proves the previous diagnosis.
Why it is conclusive rather than suggestive: the timing changed too. Failed attempts now
return in about 10 ms - the probe's own retry gap - instead of consuming a full 500 ms
CONFIG_I2C_NRFX_TRANSFER_TIMEOUT each. The controller is completing transfers. The bus is
electrically healthy.
So the SDA/SCL pull-ups were missing or inadequate, exactly as the -ETIMEDOUT signature
said and as the history suggested (the IMU worked on this bus until it was removed).
**This should not be left resting on the SoC's internal pull-ups.** They are roughly
13 kOhm against the 2.2-4.7 kOhm usual for 400 kHz. It works on a short trace and it is
marginal - fit external pull-ups properly rather than leaving pinctrl to do it.
What remains: the sensor does not acknowledge at 0x29. The suspect list is now exactly
four items, one of which software can eliminate, so it does:
on failure the driver now probes BOTH address candidates (0x29 and 0x52) with targeted
two-byte writes - not a general scan, which this part does not tolerate - and reports
which if either acknowledges. If neither does, the address is off the list and what is
left is AP_CLK on P0.00, the rail on P0.02, and XSHUT on P1.07, in that order.
AP_CLK is the leading candidate on the evidence available: the community driver is
explicit that the part will not acknowledge its address at all without it, and the clock
has never been confirmed on a scope. The app already prints the GRTC clkout enable bit at
startup, which is the software half of that question.

## 2026-09-06 - The supply hits its 100 mA limit on power enable. Likely the root cause
What: Victor reports the bench supply going into a 100 mA current limit the moment
power-gpios (P0.02) is asserted.
**This plausibly explains everything observed so far**, and it explains it better than
anything left on the suspect list. A supply in fold-back holds the rail below the
sensor's operating minimum, so the part never boots - which produces exactly the symptom
set we have: a healthy I2C bus, clean NAKs at both candidate addresses, correct pin
levels, and nothing wrong in firmware. Every software check has passed because software
was never the problem.
What is known about the current, and what is not. **No startup or inrush figure is
available in anything this repository holds** - X-CUBE-53L9A1 ships driver source, not the
device datasheet, and neither community driver states one. What can be derived: the repo
records 150 mW typical system power (source: ST, 2026-08-31). If most of that sits on the
3.3 V analogue rail, that is **about 45 mA average while ranging**. So a 100 mA limit is
only around twice the steady-state average before any transient is considered - and this
is a VCSEL part, where the laser fires in short bursts and peak current is far above
average. A 100 mA limit is plausibly below the peak the part draws in normal operation,
never mind at switch-on into decoupling capacitance.
Worth noting the firmware cannot see this. The pad readback added yesterday reads P0.02,
which is the ENABLE GPIO and will read high correctly; it is the switched rail downstream
that collapses. No software check can distinguish a rail that is enabled from a rail that
is enabled and folded back.
Distinguishing inrush from a fault, since the response differs:
- **Inrush** - brief spike into decoupling capacitance, current then falls to the
  operating level and the rail recovers. Remedy is a higher limit, or a soft-start.
- **Overload or short** - current sits at the limit and the rail stays down. Remedy is
  finding the short.
Next actions, in order, and none of them are firmware: raise the limit to 500 mA or more
and retry; measure the rail voltage while enabled; and with power disabled measure
resistance from the sensor rail to ground, where a few ohms means an assembly fault.
Also: record the real figures when measured. The energy model needs the actual peak and
average, not the 150 mW headline, and this is the moment they become measurable.

## 2026-09-06 - Reviewed the radar_shield KiCad files: two findings
What: Victor supplied the schematic and PCB. Reviewed by extracting pad-to-net from the
PCB (the authoritative connectivity) and pin names from the schematic symbol, then
comparing the two. Full write-up in docs/hardware/radar-shield-review.md.
**Finding 1: SDA and SCL appear crossed at the sensor.** The symbol names ball A11 SCL
and A12 SDA; the nets attached are /SDA and /SCL respectively. Every other signal
matches, and the nets are consistent from connector J6 inward, so whatever the host calls
SDA arrives at the ball the symbol calls SCL.
This fits every symptom exactly. With clock and data exchanged the device never sees a
valid I2C clock and cannot acknowledge at any address - which is what the bench shows:
healthy bus, clean NAKs, correct pin levels, silence at both 0x29 and 0x52. It also
explains why the LSM6DSV worked on the same bus, since the crossing is at the sensor's
own balls rather than on the bus itself.
What it does NOT settle is whether the symbol is right. Either the symbol is correct and
the schematic wires SDA to the SCL ball, or the symbol's names are swapped and the nets
were labelled by true function - two errors cancelling, hardware fine. Only the datasheet
ball map distinguishes them and it is not in this repository. But swapping TWIM_SCL and
TWIM_SDA in the board pinctrl settles it in one boot with no hardware change.
**Finding 2: the 100 mA limit is expected, not a fault.** No short exists. Every
two-terminal part was checked: no component has both pads on one net, every capacitor
sits supply-to-GND, each inductor runs switch-node to output. What /Power_Enable does is
start FOUR regulators simultaneously - an SIP4282 load switch into 20.1 uF of
+VBat_switched, plus three MIC23150 bucks into 4.8 uF each. 34.5 uF on one enable.
The load switch alone accounts for it: charging 20.1 uF at 3.7 V draws about 740 mA over
a 100 us ramp, or about 74 mA over 1 ms, before the converters charge anything. (Estimate
from schematic capacitance, for sizing, not measured.) A 100 mA bench limit is below this
design's normal switch-on transient.
The two findings interact and must be cleared in order: a supply in fold-back holds the
rails below the operating minimum, so the part never boots and produces the same silence
as finding 1. Raise the limit first, then retest I2C. The other order proves nothing.
Also worth recording for the paper: the energy model currently carries ST's 150 mW
headline and nothing measured. Inrush, per-rail steady current and the VCSEL peak all
become measurable on this board once the limit is raised, and all three matter more than
the headline does.

## 2026-09-06 - No level shifting on the shield, and the rail tracks the current limit
What: two things landed together. Victor reports that raising the supply current limit
lets the rail rise closer to its set voltage. And the netlist shows the shield has **no
level shifting anywhere**: /SDA, /SCL, /XSHUT, /AP_CLK, /Interrupt and /SYNC_IN run
straight from connectors J2 and J6 to the sensor balls, with no shifters, no series
resistors and no protection. V_Host, brought in on J3 pin 2 and the obvious reference for
a shifter, connects to nothing at all.
What the bench behaviour means: a rail that rises as the limit is raised is NOT a hard
short - a hard short holds the voltage near zero whatever the limit. It is a load drawing
whatever it is allowed, so V is roughly I_limit x R_effective. The distinguishing
question is whether the current SETTLES once the rail is up. If it drops to an operating
level, the earlier inrush explanation stands. If it stays pinned at the limit, this is a
sustained overload and something is conducting that should not be.
The leading candidate for that, given the netlist: **IO overvoltage**. The sensor's IOVDD
is 1.8 V and every digital pin lives in that domain with an absolute maximum of 1.98 V.
If the host drives its GPIOs above 1.8 V - and the nRF54L15's GPIO levels follow its VDD,
which may be 3.0 or 3.3 V - then all six signals are overdriven, the pin protection
diodes conduct, current flows from the signals into IOVDD, and the part can be damaged or
latched. That produces exactly what is being seen: excess current, a rail that tracks the
limit, and a device that never answers.
This was flagged once on 2026-09-04 as a check worth making and not pursued. It should
have been pursued.
Test, and it needs no rework: power the shield through J3 alone with J2 and J6
disconnected. Normal current means the fault arrives through the signal lines and the IO
domain is the problem. Still-excessive current means the fault is on the shield itself.

## 2026-09-06 - The always-on AP_CLK was not always on: SYSCOUNTER slept with the CPU
What: Victor measured 8 MHz on P0.00 as intermittent - on and off, not absent. Root cause
found in Zephyr's GRTC timer driver: the fast clock output is a divider on the GRTC
SYSCOUNTER, and this build's CONFIG_NRF_GRTC_TIMER_AUTO_KEEP_ALIVE=y keeps the SYSCOUNTER
awake only "when any core is in active state" (its own Kconfig help). The application
heartbeats and sleeps, so the clock tracked CPU activity.
Fix: clock_start() now calls nrfx_grtc_active_request_set(true), once, never released.
Zephyr's own symbol for this - CONFIG_NRF_GRTC_ALWAYS_ON - is promptless and nothing in
this tree selects it, so it cannot be set from prj.conf; the driver issues the request
directly. Safe because GRTC initialises at PRE_KERNEL_1 and the driver runs at POST_KERNEL.
Also changed, in the APPLICATION OVERLAY and not the board file:
- GRTC pinctrl overridden to NRF_DRIVE_H0H1. Standard drive is ~0.5 mA; slewing 1.8 V
  across ~15 pF in 10 ns needs I = C dV/dt = 2.7 mA. Estimate, for sizing only.
- The sleep state deliberately keeps driving. The board's grtc_sleep carries
  low-power-enable, which disconnects the pin - a way to stop a clock with nothing in
  software appearing to do so.
- &clock enabled, so CONFIG_VL53L9CX_HOLD_HFCLK (default n) can hold the high-frequency
  domain up as a fallback. Off by default because it is expected to be redundant, and a
  redundant clock request inflates idle current - the exact number this project must
  measure honestly.
Rejected first attempt, recorded because it was wrong in an instructive way: the initial
hypothesis was the HF clock domain stopping under the divider, implemented as
nrf_clock_control_hfxo_request(). That does not link - the API is nRF54H only
(clock_control_nrf54h_hfxo.c, CONFIG_CLOCK_CONTROL_NRF54H_HFXO). The link error was the
thing that forced reading the GRTC driver, which is where the real mechanism was.
Consequence for the paper: the AP_CLK A/B in docs/plan/ap-clk-always-on.md must be
re-taken with this firmware. The SYSCOUNTER now runs through idle, so the enabled row
carries that cost. That is the honest figure - it is what the design actually pays - but
any number measured before 2026-09-06 no longer describes this build.
Builds: FLASH 63,616 B, RAM 41,136 B.

## 2026-09-06 - Bring up on the X-NUCLEO-53L9A1, to get a known-good reference at last
What: the custom shield still never acknowledges. SDA/SCL crossing is now ruled out - the
LSM6DSV works on the same bus on the same board, which finding 1 of the KiCad review could
not have known. So the remaining causes are AP_CLK, level shifting and the supply, and
ST's X-NUCLEO-53L9A1 removes all three: onboard 12 MHz oscillator (SW1 = INT), level
shifters referenced to a jumper-selected host voltage (J1), and its own regulators.
Prepared firmware_test/overlays/x-nucleo-53l9a1.overlay, applied over the existing board
overlay via EXTRA_DTC_OVERLAY_FILE. Builds 2026-09-06: FLASH 63,408 B, RAM 44,208 B, with
every override confirmed in the generated devicetree.
Facts, all from UM3656 Rev 1 (vendor/x-cube-53l9a1/Documentation), Table 1 and 3.3.2.2:
- D15 SCL, D14 SDA, D0 INTR (falling edge), D1 XSHUT (REVERSED POLARITY), D11 CLK_IN
  (only when SW1 = EXT), A3 SYNC_IN (follower mode only).
- .vdda must be 2V8. The custom board is 3V3. Carrying that over would not fail loudly -
  it configures the analogue front end, so it would return plausible rubbish.
- SW1 INT gives the onboard 12 MHz; EXT takes 12.5 MHz from the host and ST's own
  firmware does not support it yet.
- J6 SENSOR IOVDD to 1V8. J1 EXT IOVDD must match host VDD.
Three things this changes that are worth naming:
- XSHUT polarity INVERTS relative to the custom board. Getting it backwards holds the part
  in reset all session while every log line looks healthy - indistinguishable from the
  failure already being chased.
- power-gpios is DELETED because no such pin exists. PM_DEVICE_ACTION_TURN_OFF then has
  nothing to drive, so this board cannot carry the duty-cycling work or the multi-month
  battery claim. It proves the driver and measures active-mode energy. Nothing more.
- The GRTC clkout is deleted, and the driver's SYSCOUNTER keep-alive now compiles out with
  it via VL53L9CX_APCLK_FROM_GRTC. That request only ever made sense when GRTC generated
  AP_CLK; on a self-clocking sensor board it is pure idle current, which is the one number
  this project cannot afford to inflate by accident.
Free consequence: idle current under this overlay IS the "AP_CLK removed" arm of the A/B
in docs/plan/ap-clk-always-on.md, outstanding since 2026-09-04. Take it while the board is
on the bench.
Still VERIFY: the shield's supply pins and current (UM3656 is the software manual and does
not state them), whether the shield carries its own I2C pull-ups, and the nRF54L15's GPIO
voltage - which is now needed to set J1 correctly, not just to assess overvoltage risk.

## 2026-09-06 - UM3683 added; it explains the whole bring-up failure in one sentence
What: Victor supplied UM3683 Rev 3, the VL53L9CX programming guide, now tracked at
docs/um3683-programming-guide-stmicroelectronics.pdf. Notes in
docs/research/um3683-power-on-and-boot.md.
Section 2.5.1 gives three necessary conditions to leave POWER_OFF: all three supplies
(AVDD, DVDD, IOVDD) up, XSHUT high at IOVDD level (Victor flagged this), and the external
clock active. Any order. Then the sentence that matters: "If at any point, one of the three
conditions becomes invalid, the sensor returns to the off state."
So an intermittent AP_CLK does not degrade communication - it RESETS THE PART. The clock
was running in bursts that tracked CPU activity, so the sensor was being returned to
POWER_OFF continuously, and a device in POWER_OFF cannot acknowledge at any address. The
clean NAKs, the correct pin levels and the silence at both 0x29 and 0x52 are all exactly
what that failure looks like. It also means testing I2C before the clock was continuous
could never have proven anything - condition 3 is a precondition for the device being on
the bus at all, not for good data.
Two driver gaps this exposed, both fixed:
- DEVICE_ID at register 0x0000 must read 0x53334C39 ("S3L9"). Section 2.5.2 lists this as
  a check the driver MUST perform. Ours read it and logged it without ever comparing it,
  which passes as long as anything acknowledges - a wrong part at the address, or a
  half-powered device returning zeros, both counted as success. Now validated at the point
  the clean path and the bus-recovery path converge, with all-zeros and all-ones called out
  separately as the bus idle level rather than a device.
- SYSTEM_FSM at 0x008C (Table 8) reports the state machine directly: NONE, READY_TO_BOOT,
  STANDBY, STREAMING. The driver now logs it after a successful probe. Expect
  READY_TO_BOOT. Reading NONE from a part that just answered is the exact signature of a
  supply or clock that is present but not holding - the 2.5.1 failure made observable
  instead of inferred.
Also confirmed, from the other direction: the default I2C address is 0x29 stated as 7-bit
(2.8.1), which closes the 0x29-vs-0x52 question the README argued on reasoning alone; and
SYNC_IN is active low in Follower mode only (2.7.2), so SYNC_MANUAL keeps it from starting
an exposure.
One thing the driver does not model: DVDD. Section 2.5.1 names three supplies, the driver
carries vdda and vddio, and the custom board's +1V2 rail is the third. Nothing in firmware
configures it, but it is a power-on condition and belongs on the bench checklist.
Note on the boot defaults (2.5.2): AVDD 2.8 V and IOVDD 1.8 V are the device defaults and
are only written when they differ. The X-NUCLEO-53L9A1 sits exactly on them. The custom
board at 3.3 V needs VDDA_CFG written - which is why the 2.8 V placeholder carried until
2026-09-04 would have failed quietly rather than loudly: it is a configuration write, not
a check.
Builds both configurations: custom board FLASH 64,612 B, X-NUCLEO FLASH 64,316 B, RAM
44,208 B.

## 2026-09-06 - DVDD confirmed at 1.2 V; all three sensor supplies verified correct
What: Victor confirmed DVDD = 1.2 V. Checked against the PCB the same day by extracting
pad-to-net for the sensor footprint (U1, 42 pads) and comparing with the schematic symbol's
pin names: DVDD C12 -> +1V2, AVDD E6/E7 -> +3V3, IOVDD E8 -> +1V8, VBAT_LDD B1 and
VBAT_RX D1 -> +VBat_switched.
So condition 1 of UM3683 section 2.5.1 - "the three power supplies must be activated" - is
satisfied by design, and AVDD/IOVDD match the overlay's vdda-microvolt and vddio-microvolt.
Supplies are eliminated as a cause of the I2C silence, PROVIDED they are actually up at the
time, which the 100 mA fold-back made doubtful and a raised current limit settles.
Decided NOT to add a dvdd-microvolt devicetree property. DVDD appears once in UM3683 (as a
power-on condition) and nowhere in ST's driver; the only configuration registers are
VDDA_CFG (0x000C) and VDDIO_CFG (0x000D). There is no DVDD register to write, so the
property would be a value nothing reads, implying a configurability that does not exist.
Recorded as a hardware fact and a bench check instead.
Checked while in the area, because the failure mode is silent: vdda and vddio reach ST's
code through DT_INST_ENUM_IDX, so the BINDING'S ENUM ORDER IS THE REGISTER VALUE. Verified
vdda-microvolt enum [2800000, 3300000] against VDDA_2V8 = 0 / VDDA_3V3 = 1, and
vddio-microvolt enum [1200000, 1800000] against VDDIO_1V2 = 0 / VDDIO_1V8 = 1. Both match.
Reordering either enum would invert the value written into the device and misconfigure the
analogue front end with no error anywhere - worth knowing before anyone tidies the YAML.

## 2026-09-07 - apclk-always-on snippet, to settle the AP_CLK mechanism on the bench
What: Victor is doing one more test on the custom board and asked for firmware with AP_CLK
definitively always on. Added firmware_test/snippets/apclk-always-on/, ticked in the nRF
Connect extension alongside the default (custom board) configuration.
It engages both mechanisms at once: the GRTC SYSCOUNTER ACTIVE request that has been in the
default build since 2026-09-06, and CONFIG_VL53L9CX_HOLD_HFCLK=y, which holds the
high-frequency clock domain via clock_control_on() and never releases it.
Why both: the expert review on 2026-09-06 showed the SYSCOUNTER-gates-CLKOUT reasoning is
unproven. CLKOUT_FAST divides the GRTC hfclock (pclk, a 16 MHz fixed-clock) while
SYSCOUNTER.CLKCFG.CLKSEL selects among low-frequency sources, and upstream Zephyr enables
CLKOUT_FAST with no ACTIVE request at all. The AUTO_KEEP_ALIVE premise is confirmed - the
SYSCOUNTER does drop out at WFI - but whether the clock output follows it is not. Rather
than argue it further, hold both and let the scope decide.
The three outcomes are written down in docs/plan/ap-clk-always-on.md so the reading is not
made up after the fact: continuous only with the snippet means mechanism 2 is real and the
default build is wrong; continuous both ways means mechanism 1 sufficed; still intermittent
means it is not clock gating at all and the search moves to the pin and the board.
Two hardening fixes made at the same time, because turning HOLD_HFCLK on is exactly what
would have exposed them:
- DEVICE_DT_GET(DT_NODELABEL(clock)) and the nrfx GRTC calls were guarded by runtime
  IS_ENABLED() branches, so they survived only by dead-code elimination. DEVICE_DT_GET
  emits a __device_dts_ord_<N> reference from the front end, so HOLD_HFCLK=y on a build
  without &clock enabled would have been a link error naming nothing useful. Both are now
  #if guards.
- The driver's Kconfig now selects NRFX_GRTC on nRF54L. It was calling into nrfx GRTC while
  selecting only I2C and PWM, and built purely because the GRTC happens to be the system
  timer in this configuration.
Not to be used for energy measurement: HFXO runs continuously under this snippet.

## 2026-09-07 - The bring-up diagnostics were being silently dropped by RTT
What: Victor flashed the always-on build and got "VL53L9CX not ready" with NO driver
output at all - and said "but no retries". The retries had run. The whole ladder had run:
3.7 seconds elapsed before main(), which is almost exactly the worst-case failing path
(probe, bus recovery, both address candidates, inverted polarity, power cycle, second
attempt). Every message of it was thrown away.
Cause, in zephyr/subsys/logging/backends/log_backend_rtt.c:178-186 and 206-237: when a
write fails CONFIG_LOG_BACKEND_RTT_RETRY_CNT times the backend sets host_present = false,
and from then on the do/while exits immediately on every call - each subsequent message is
dropped instantly, silently, with no retry. Once an RTT viewer attaches and one write
succeeds, on_write() sets host_present = true again, which is why everything from main()
onwards appeared perfectly.
So the driver's diagnostics run at POST_KERNEL, in a burst, before a viewer is reliably
attached - precisely the window that latches the flag.
MY 2026-09-06 RTT CHANGE DID NOT FIX THIS AND MADE BOOT SLOWER. I switched to
LOG_BACKEND_RTT_MODE_BLOCK believing blocking would stop the drops. The latch exists in
both modes; block mode only adds RETRY_CNT x RETRY_DELAY_MS = 20 ms per message before it
gives up. The commit message at the time claimed the log gap was a buffer overflow and
that blocking would cure it. The first half was right, the second was not.
Fix, and deliberately not more log tuning: vl53l9cx_retry_boot() is now public API, and
main() calls it every ten heartbeats while the sensor is not ready. Same code path, same
messages, but emitted at a moment when the log is demonstrably working - you have just
watched the heartbeat that precedes it. That removes the dependency on catching boot
output at all, which no amount of buffer sizing can guarantee.
Buffer also raised 4 KB -> 16 KB, recorded as a mitigation and not the fix.
Cost: RAM 45,232 -> 57,520 B (12 KB of that is the buffer), 29.9% of 188 KB. FLASH
66,404 B.

## 2026-09-10 - IMU refitted and re-enabled; the bus finally has a control
What: Victor refitted the LSM6DSV15BXTR, so CONFIG_APP_ENABLE_IMU goes back to y and the
accelerometer is now read on every heartbeat rather than once at startup.
Why it matters beyond the IMU: it is the SECOND DEVICE ON THE SAME I2C BUS, and therefore
the only control this project has. Every VL53L9CX NAK so far has had two possible
explanations - a bus fault or a sensor fault - and main.c has been saying so in as many
words since the part was unfitted. With the IMU answering at 0x6B, SDA P1.13, SCL P1.08,
the pull-ups, the pinctrl and the 400 kHz bitrate are all proven, and a silent 0x29 is
unambiguously on the ToF side.
Reading it every beat rather than once is deliberate: a single startup probe proves the bus
worked at t=0, whereas a per-heartbeat read re-exercises it once a second for as long as
the board runs. If the ToF keeps NAKing while the IMU keeps answering in the same log, that
is not a bus problem, and it is proven continuously rather than inferred from one probe.
The existing sanity check carries over: a board at rest reads one gravity, and
imu_read_and_log() warns if |a| is outside 800-1200 mg. Two axes at exactly 0 means the
block base is wrong; three plausible but small means the scale is. That is the check that
caught the 16BX reversed axis order (Z,Y,X) on 2026-09-05.
No driver changes. All three functions - pick_variant(), imu_configure(),
imu_read_and_log() - were already written and were only gated off.
FLASH 69,340 B, RAM 60,784 B.

## 2026-09-10 - Bus PROVEN by the IMU, and P0.02 is now being held low
What: first log with the IMU refitted and read every heartbeat. Two findings, one of them
new since 2026-09-07.
1. THE I2C BUS IS PROVEN. WHO_AM_I = 0x71 (LSM6DSV16BX), accel steady at |a| = 995-996 mg
at rest across ten heartbeats, while in the SAME log the VL53L9CX gives 58 NAKs at 0x29 and
no ACK at 0x52. SDA P1.13, SCL P1.08, the pull-ups, the pinctrl and 400 kHz all work. Every
previous NAK had two possible explanations; it now has one, and it is on the ToF side.
2. P0.02 IS BEING HELD LOW, AND THIS IS NEW. The retry logs "power-gpios driven high, pad
reads 0 <-- HELD LOW". On 2026-09-07 the identical retry read 1. The pin is configured
GPIO_OUTPUT_INACTIVE | GPIO_INPUT so this is the actual pad, not the output register: the
nRF54L15 drives P0.02 high for 500 ms and the net does not get there.
What changed in between is the IMU rework. That makes a solder bridge or debris on
/Power_Enable the first thing to check - it is the enable input of the load switch and all
three regulators, so if it cannot be pulled high the sensor has no rails at all, and UM3683
2.5.1 condition 1 fails. That would explain the NAKs completely and independently of
everything else on the list.
Cannot yet distinguish a short on the net from a damaged nRF output driver (the
no-level-shifting overvoltage risk from 2026-09-06 would fail exactly this way). Measuring
P0.02 with the shield disconnected separates them in one minute.
Also fixed: I added a second imu_read_and_log() call without noticing the loop already had
one, so every heartbeat logged a good sample followed by "no new accel sample (STATUS=0x04)"
- the second read finding XLDA clear because the first had just consumed the data. Harmless,
but it read like an ODR fault. One call site now, with a comment saying so.
I2C bus speed, checked against UM3683 Table 1 on request: ST's quoted I2C read times imply
~650-700 kbit/s effective (14900 B in 200 ms is 134,100 bits, so 670 kbit/s), i.e. Fast-mode
Plus rather than the 400 kHz we run. NO MINIMUM IS SPECIFIED and I2C is static, so bus speed
cannot cause a NAK - this is a throughput ceiling, not a bring-up factor. At 400 kHz a 54x42
frame needs ~335 ms, so ~2.4 fps against ST's quoted 4.
FLASH 69,308 B, RAM 60,784 B.

## 2026-09-10 - THE SENSOR WORKS. First depth frames from the VL53L9CX.
What: "VL53L9CX ready. Firmware blob upload took 306 ms", followed by repeated
"ToF 12x10 in 40 ms - 73/120 zones valid" with distances from 897 mm to 9568 mm and a
device frame counter incrementing 1, 2, 3. After nine days of clean NAKs the part boots,
uploads its 9,865-byte patch and ranges.
What actually fixed it, in the order the evidence supports:
- zephyr,concat-buf-size / zephyr,flash-buf-max-size raised from the 16-byte default to
  1040 (2026-09-06, found by both expert reviews). The blob upload returned -ENOSPC on
  every chunk before this and could never have completed. 306 ms measured against ~250 ms
  predicted at 400 kHz, so the upload is behaving exactly as modelled.
- device_boot() no longer discarding vl53l9_init()'s return code, which is what had been
  hiding the above.
- P0.02 held high by the application. The pad now reads 1 at assert and stays 1 across
  every heartbeat, so the 2026-09-10 "HELD LOW" reading was the retry's own power cycling
  seen mid-cycle, NOT a short and NOT a damaged pin. Recorded because I had ranked a
  rework solder bridge as the leading suspect and that was wrong.
Three things the first working log exposed:
- get_frame failed (-3) = ST INVALID_STATE on the very first capture, 13 ms after boot:
  the device was not in STREAMING yet. Later captures are clean, so it is a startup race,
  not a defect. Now named in the log instead of surfacing as a bare -EIO.
- One -EAGAIN frame timeout at 7.9 s, then nothing since. Worth watching.
- The -EAGAIN message claimed "with no int-gpios this is polled" unconditionally, which is
  FALSE on this board - int-gpios is wired to P0.01. It now reports the actual mode and
  says that a missed edge and a stalled sensor look identical from there.
Added, because the data was being captured and thrown away: the device health line from
the status trailer, ERROR_CODE at byte 60 and ERROR_STATUS at byte 62 (UM3683 Table 7,
offsets verified 2026-09-06). pll_lock among those bits is the device's own verdict on
AP_CLK - present is not the same as good enough.
Also cut the per-second PWR_EN line down to on-change only. A line every second saying
nothing costs RTT bandwidth that the ten grid rows per frame actually need.
Still open: no IMU output in this log across 22 heartbeats despite CONFIG_APP_ENABLE_IMU=y,
so imu_ok was false. The startup probe result is not in the captured log, so the reason is
unknown. Needs the first second of a fresh capture.
FLASH 71,204 B, RAM 60,792 B.
## 2026-09-10 - Full resolution: 54x42, and what it costs
What: Victor wants maximum resolution first and duty-cycling afterwards, framerate to be
decided later. TOF_RES is now VL53L9CX_RES_54X42 - the whole array, 2268 zones.
Three consequences, all measured from the geometry rather than guessed, and each needing
a change:
- FRAME SIZE 14,842 bytes (3 planes x 2268 x 2, plus 1134 DSS, plus the 100-byte status
  line). At 400 kHz that is ~334 ms of bus time against 40 ms for 12x10.
- I2C TIMEOUT had to go up. The nRF TWIM default is 500 ms, so a 334 ms read would have
  sat at 67% of budget with only bus overhead in reserve, and a timeout there calls
  i2c_nrfx_twim_recover_bus() - which would have presented as a sensor fault rather than
  as the tight deadline it was. Now 2000 ms. The ST protocol review flagged this on
  2026-09-06 as something to fix before running the full-resolution arm.
- LOG VOLUME. The grid is 42 rows of 216 characters, about 8.9 KB per capture, arriving as
  a burst. RTT drops that once the backend latches host_present=false. The grid is now
  behind CONFIG_APP_LOG_FULL_GRID (default y, and it says so when off), and the RTT buffer
  goes 16 KB -> 32 KB. Turn the grid off for energy runs, where the summary and the
  amplitude split carry the information and the grid is only bandwidth.
Capture timeout raised 2 s -> 5 s; the old value was sized when a read took 40 ms.
RAM 60,792 -> 77,176 B (40.1% of 188 KB), almost all of it the larger RTT buffer. The frame
struct was already full-size: it is static and dimensioned VL53L9CX_ZONES_FULL, so
switching resolution costs nothing there.
FOR THE ENERGY WORK, the number that matters is the 334 ms. It is CPU-awake,
sensor-active time paid on every frame, it does not shrink with duty cycle, and it is what
duty-cycling has to amortise. It is also the strongest argument for Fast-mode Plus: UM3683
Table 1 quotes I2C reads implying ~1 MHz, which would cut it to ~134 ms. That needs
clock-frequency changed in the board file (Victor's) and probably stronger pull-ups, so it
is his call and not urgent until the sensor work settles.
## 2026-09-10 - The sensor stops streaming by itself. UM3683 2.4 names one cause.
What: with recovery restored and the frame wait made interrupt-assisted, the failure is
now clean and repeatable at BOTH 54x42 and 24x20:
  frame wait failed (-11): FSM 0x02, FRAME_READY 0x00
  device is in STANDBY with NO frame ready: it left streaming without producing one
Boot is perfect every time - device id 0x53334c39, READY_TO_BOOT, blob in 306 ms, retry
recovers cleanly. The device enters STREAMING and then quits without ranging.
UM3683 section 2.4 describes exactly one mechanism that does that: "Faults related to laser
emission are directly checked by the VCSEL driver. In case of an error or fault, the laser
driver immediately stops the laser emission and reports an error to the firmware. The
firmware then stops the streaming ... The laser driver switches to safe mode. It is the
driver's responsibility to reboot the device and to restart the streaming when a laser
safety error occurs."
That fits everything observed: boot works and the blob uploads because neither fires the
VCSEL; ranging fails because it does; and a reboot recovers, exactly as ST says it must.
It also fits this board's history - VBAT_LDD comes straight off the load switch, and the
100 mA supply fold-back has been on the open list since 2026-09-06. A rail that holds for
logic but sags when the laser fires would produce precisely this.
NOT YET CONFIRMED. The error bits are sticky (2.4.1) and we were not reading them on this
path. The driver now calls log_device_status() when it finds STANDBY-with-no-frame, and
reads the five LDD_STATUS bytes individually to work around ST's indexing bug
(st/vl53l9.c:820-823 reads all five into element 0).
Also learned, and it corrects a documented assumption: SYNCHRO after a fresh boot reads
2 (AUTONOMOUS), not 0 (SLAVE). The driver has always read-then-set rather than assuming,
which is why this never bit - but docs/research/um3683-power-on-and-boot.md states the
reset default is SLAVE on the strength of Table 12, and the device disagrees. Either the
patch firmware sets it during boot or the table describes the pre-patch reset value.
Next: read the log line. A non-zero ERROR_STATUS or LDD_STATUS confirms the laser fault and
moves this to the supply. All zeros means something else stops the streaming and the search
reopens.
## 2026-09-11 - Expert review: two verified findings change the project, not the plan
Three specialist reviews (BLE, people detection/tracking, dToF physics) read the BLE and
counting plan adversarially. Full record in docs/plan/expert-review-2026-09-11.md. I verified
the severe claims against UM3683 Rev 3 before recording them.
VERIFIED 1 - THE SENSOR CANNOT RANGE BEYOND 9.6 m. UM3683 2.6.1 verbatim: "a fixed ranging
period of 64 ns (which corresponds to a maximum ranging distance of 9.6 m)". 64 ns x c/2 =
9.6 m. It is a GATE, not a link budget - a photon arriving later is never counted, and it
explains the bench exactly: valid returns spanned 0.9-9.5 m. Consequence: the floor is inside
the window only where sin(depression) > h/9.6, so at 2.7 m that is above 16.3 deg. At 30 deg
tilt about 17% of rows are permanently blind. Tilt is bounded from below by physics, roughly
41 deg minimum for margin, giving about 10 m2 of floor - only 1.6x the overhead footprint that
room-occupancy.md already called insufficient. The 30 deg option is deleted from the design
space.
VERIFIED 2 - 150 mW IS THE WRONG PROFILE. UM3683 Table 23 gives two: "VR headset / precision
mode", 5 ms exposure, 150 mW; and "Outdoor lidar / ambient mode", 16 ms exposure, 450-800 mW.
We run 16 ms. Every energy figure in this repo, and CLAUDE.md's hard rule, is anchored to the
precision profile, so the battery claim is 3-5x optimistic. ST gives a RANGE because DSS opens
the array under ambient light, which means power draw is a function of how sunlit the room is.
GOOD NEWS, also verified: our firmware implements the ambient profile correctly and completely
- exposure 16 ms, context LONG, switchover 650, rtn_short_offset 2, DSS_LONG, step number 6,
power mode Regular, cal_prog_offset -8/-1 via set_context(LONG). Nothing is misconfigured, so
amplitude 9 against ambient 13 is the physics at that range and not a setup error. That closes
a question the reviewer raised.
NOT YET VERIFIED, and the highest-value measurement available: the amplitude 9 / ambient 13
reading was taken at 12x10 BINNING, which sums about 19 native zones and therefore carries
sqrt(19) = 4.4x the SNR of 54x42. If that scaling holds, per-zone amplitude at full resolution
is below 1 LSB and the project's central premise - 2268 zones - does not survive. One hour to
settle: the same static scene at both resolutions.
Also verified from Table 22: DSS computation at full resolution costs 13.5 ms per frame in
parallel with exposure, so there is a fixed floor at 54x42 that no exposure reduction beats.
And LP exit is 3.5 ms, so duty cycling has three rungs rather than two - the reviewer estimates
standby beats full power-down by about 6x at 0.1 Hz, inverting a decision room-occupancy.md
called "clearly correct".
MY OWN ERRORS, corrected in the plan: the UUID base contained the letter l, which is not a hex
digit, so nothing would have compiled. The phase-9 privacy gate (strings | grep for a UUID)
could NEVER have worked because BT_UUID_128_ENCODE emits binary, and I was going to cite it as
architectural proof in a paper. The advertisement payload is 8 bytes and I wrote 10. The
Background Model characteristic cannot be a GATT Read because attributes cap at 512 bytes and
it is 4.8 KB. And my energy justification for connectionless advertising was simply wrong -
BLE is under 1% of the budget either way, and what makes streaming expensive is the SENSOR
duty cycle, not the radio.
Largest unactioned algorithmic finding: nothing in the design splits merged people, and with
3-5 people merging is the normal case. The 3x3 despeckle FILLS the gap between two adjacent
people, then 8-connectivity merges them, and segmentation runs on a binary mask that has
already discarded the 20-60 cm depth step between them. Needs open-only despeckle,
4-connectivity, a depth-similarity join, and an explicit split stage.
## 2026-09-11 - The laser fault is real, but my diagnostic named the wrong cause
What: the first log with full device diagnostics. ERROR_CODE 0x0F00, ERROR_STATUS bit 7 set,
all other bits clear, LDD status reading 00 02 00 00 00.
THE VERDICT MY CODE PRINTED WAS WRONG AND WOULD HAVE COST A DAY. It said "*** PLL NOT LOCKED
- this is AP_CLK. The device cannot lock to the external clock it is being given." UM3683
Table 16 shows ERROR_STATUS 0x0066 is a register of ERROR bits - bit 5 SET means a PLL lock
error. It read 0, which means the clock is FINE. I had the sense inverted, so the message
fired on precisely the healthy case and pointed at a subsystem with nothing wrong with it.
THE REAL ANSWER is ERROR_CODE 0x0F00 = CABDT_ERROR_LDD_FAULT, which UM3683 Table 17 glosses
as "CABDT LDD fault (check laser driver error)". And section 2.4.1 explains bit 7: "If bit 7
of ERROR_STATUS is set, the error code provides additional information" - so internal_fw=1 is
not an internal firmware error, it is a pointer to ERROR_CODE. Every supply bit - VHV
over/under-voltage, SPAD supply overload, current limit - is CLEAR. So the sensor did not see
its own rails collapse; the laser driver reported a fault to it.
The "00 02 00 00 00" is also not trustworthy. ST's vl53l9_get_status() reads all five
LDD_ERROR_STATUS bytes into element 0 (st/vl53l9.c:820-823, missing the + i), so elements 1-4
are stack residue. ldd[0] read 0x00. The verdict now keys off ERROR_CODE and ldd[0] only.
WHAT CHANGED SINCE IT LAST WORKED, and it is almost certainly the cause. Victor is right that
frames came out before. The working captures predate commit 6e33a18, which was the first to
call vl53l9_set_exposure(). Until then NB_SHOT_STEP_n sat at its RESET OF ZERO - zero shots
per step - so the VCSEL barely fired. That explains both halves of the old behaviour: frames
arrived, and amplitude was 9 against ambient 13 with 73 of 120 zones valid. Now exposure is
16 ms, the VCSEL fires for real, and the laser driver faults.
That lines up with everything else found this week: UM3683 Table 23 puts the 16 ms ambient
profile at 450-800 mW against the 150 mW precision profile every document assumed, and the
board has a history of a 100 mA supply fold-back. The hypothesis is that VBAT_LDD cannot
deliver the VCSEL current once the shots are real.
Testable in one build: sweep CONFIG_VL53L9CX_EXPOSURE_MS down. If the fault rate falls
monotonically with exposure, it is the supply.
Also added, at Victor's request: the firmware now prints its git version on RTT at boot,
stamped by CMake from git describe, alongside resolution, exposure and I2C speed. A bench log
that cannot be matched to a commit cost most of this session.
FLASH 76,852 B.
## 2026-09-11 - THE IMU HAS STOPPED ANSWERING TOO. The board changed.
What: the 2026-09-11 bench log opens with "WHO_AM_I read failed (-5) - nothing answered at
0x6b". On 2026-09-10 the same IMU read WHO_AM_I = 0x71 and streamed accelerometer data with
|a| steady at 995-996 mg across ten heartbeats. It is now silent.
WHY THIS MATTERS MORE THAN THE LASER FAULT. The IMU was the bus control - the whole reason it
was refitted. Its loss is not a second unrelated bug; it is independent evidence that
SOMETHING ON THE BOARD CHANGED between 2026-09-10 and 2026-09-11, and the laser fault appeared
in the same window.
The bus itself is NOT the explanation. In the same log the VL53L9CX answers on the same bus:
"sensor answered on attempt 1, device id 0x53334c39". So 0x29 ACKs and 0x6B NAKs on one bus at
one moment - the bus, the pull-ups, the pinctrl and the bitrate all work, and the IMU
specifically is unpowered, damaged, or disconnected.
That makes the control run more important, not less, and it puts real weight on the branch I
had ranked second: if legacy-config also fails, the firmware is exonerated and the board is
the answer. Two independent devices degrading in the same window is not a coincidence worth
explaining away.
ALSO: the legacy-config snippet failed to apply TWICE in the nRF Connect extension, and both
bench runs were wasted. The WRITES banner added earlier today caught it the second time - the
log says "NOT the control - a snippet may not have applied" on line six. Rather than fight the
build configuration a third time, the control now lives in prj.conf, so a plain Build IS the
control. Marked TEMPORARY with the revert instruction, same as the AP_CLK hold on 2026-09-07.
## 2026-09-11 - Both devices stopped in the SAME session, and it is the session the VCSEL first fired
What: Victor asked why it worked and then did not. Reconstructing the timeline from git and the
bench logs makes the correlation sharp enough to lead with.
  09-10 09:05  IMU re-enabled            IMU WORKS - WHO_AM_I 0x71, |a| 995-996 mg
  09-10 16:49  working-2026-09-10        ToF WORKS - 12x10 frames, 73/120 zones valid
  09-10 17:33  commit 6e33a18            set_exposure() CALLED FOR THE FIRST TIME
  09-10 eve    next bench log            ToF fails AND no IMU output across 22 heartbeats
  09-11        today                     ToF LDD-faults, IMU NAKs at 0x6b
Before 6e33a18 the driver never called set_exposure, so NB_SHOT_STEP_n sat at its reset of ZERO
shots per step and the VCSEL barely fired. After it, 16 ms of real shots. BOTH DEVICES STOPPED
WORKING IN THAT SESSION. I had been treating the IMU as an unrelated loose end and noting it as
"still open"; it is not unrelated, and the timestamps say so.
THE MECHANISM THAT FITS, stated as inference and not fact. The sensor drawing what UM3683
Table 23 specifies for this profile - 450-800 mW, i.e. 136-242 mA average at 3.3 V, with pulse
current 20-60x higher during emission given Table 21's 1.6-4.4% VCSEL duty - against a supply
Victor reported folding back at 100 mA ON 2026-09-06, WITH EXPOSURE STILL AT ZERO. The first
real firing would pull the bench supply into current limit and brown out the whole board,
taking the IMU down with it. Repeated over an evening of laser faults and automatic reboots.
WHAT THAT WOULD MEAN, and it is the uncomfortable part. If a brownout was severe enough to
leave the IMU permanently non-responsive rather than merely interrupted, it was severe enough
to damage silicon - and the VL53L9CX sat in the same event. So the control run is now also a
DAMAGE TEST: the board did exactly this on 2026-09-10 with that configuration, so if
legacy-config fails today, the most likely reading is that the part or the board no longer is
what it was, not that a register is wrong.
ALTERNATIVES NOT EXCLUDED: coincidence; the IMU rework degrading over hours (it was refitted
that morning); progressive IO overvoltage damage from the missing level shifters, which has
been an open risk since 2026-09-06 and would explain a part degrading over days rather than
instantly. None of these are ruled out and none of them are cheap to distinguish from the
firmware side.
NEXT MEASUREMENT, and it outranks the bisect: meter the IMU's supply rail. If it is down, that
is a board-level supply fault and it explains both devices at once. If it is up and the IMU
still NAKs, the IMU is damaged and the brownout hypothesis gains a lot of weight.
## 2026-09-11 - THE ANSWER: AP_CLK is 12 MHz, the devicetree said 8 MHz
What: Victor mentioned in passing that the board is now supplied with 12 MHz. The
devicetree still declared ext-clock-frequency = 8000000. That is the whole bug.
WHY IT BREAKS THE LASER AND NOTHING ELSE. UM3683 2.5.2: setting the external clock frequency
"allows the firmware to configure ALL INTERNAL CLOCKS required for correct sensor operation".
Declare 8 MHz while feeding 12 and every internal clock runs 1.5x faster than the firmware
believes - including the one timing the optical pulse against the blanking period, which
2.6.1 says exists "to comply with the power limits of laser Class 1". The laser driver trips
its own safety interlock and CABDT reports ERROR_CODE 0x0F00, LDD fault.
IT EXPLAINS EVERY OBSERVATION, INCLUDING THE ONE THAT KILLED THE SUPPLY THEORY:
- worked before: 8 MHz supplied, 8 MHz declared, consistent
- broke without a firmware change to blame, because the change was on the board
- an LDD SAFETY fault specifically, not a supply fault
- every supply error bit clear - vhv_ov, vhv_uv, spad_overload, current limit - because it
  was never a supply problem
- ldd[0] = 0x00: the interlock tripped, but the LDD had no internal error of its own
- AND THE CONTROL RUN FAILED WITH NB_SHOT_STEP_n AT ZERO. A supply-overload theory cannot
  explain a fault when the VCSEL is barely firing. A timing violation can: the pulse/blanking
  ratio is wrong on every pulse, however few there are. That was the fact that did not fit,
  and it is the fact that confirms this.
Fixed: ext-clock-frequency = 12000000 (legal, Table 11 gives 6-27 MHz), and the GRTC fast
clock output DELETED from P0.00 - GRTC divides the 16 MHz pclk and 8 MHz is its maximum at
divider 1, so it cannot produce 12 MHz and an external source plus a GRTC output on one pin
is two drivers fighting. Deleting it also compiles out the SYSCOUNTER keep-alive.
The bisect control is removed and normal configuration restored - exposure, DSS LUT and
profile writes all on. It did its job: the control failing at zero shots is what ruled the
firmware out and forced the search back to the hardware.
LESSON WORTH KEEPING. Four sessions went into this. The board changed and the devicetree did
not, and nothing in the firmware could have known - the device reports a laser fault, not a
clock fault, because from its point of view the clock is fine and the laser timing is wrong.
The config banner now prints resolution, exposure and I2C speed at boot; AP_CLK belongs in
that line too, and a mismatch between declared and measured is worth a loud check.
Still open and unexplained: the IMU stopped answering at 0x6b in the same window. A clock
change should not affect it. Either it is genuinely unrelated - rework, or its own supply -
or something else happened on the board at the same time.

