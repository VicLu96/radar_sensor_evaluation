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
