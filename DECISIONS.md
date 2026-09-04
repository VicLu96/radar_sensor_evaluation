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
