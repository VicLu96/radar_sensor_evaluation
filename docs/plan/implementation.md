# Implementation plan — firmware and test setup

Written 2026-08-31. Phases are ordered by **risk retired per week**, not by how
satisfying they are to build. The riskiest thing here is not the algorithm; it is
whether the sensor talks over I²C at a useful rate and what it costs in energy.

## Phase 0 — Answer the four questions that could invalidate the design *(first)*

Do these before writing application code. Each has a cheap answer and an expensive
consequence if wrong.

| # | Question | How to answer | If the answer is bad |
|---|---|---|---|
| 0.1 | **How big is the firmware blob, and how long does it take to load over I²C?** | Load it on the STM32 eval board with a scope on SCL | A multi-second boot forbids power-cycling, and the whole duty-cycle strategy changes |
| 0.2 | **What reduced-resolution modes exist?** | X-CUBE-53L9A1 headers and UM3656 | If 54×42 is the only mode, the energy-accuracy curve — the paper's core — has one point on it |
| 0.3 | **What does standby cost, and is firmware retained?** | Datasheet, then measure | Decides the whole sleep architecture |
| 0.4 | **Max I²C clock, sensor and nRF54L15 both** | Datasheets, then scope | 400 kHz vs 1 MHz doubles per-frame bus energy |
| 0.5 | **Can the board measure sensor and MCU rails separately?** | Look at the schematic | **If the rails are shared, the paper's central claim cannot be made** without a bodge — see below |

### 0.5 deserves its own note

The contribution is an energy breakdown *attributed by phase and by component*. That
requires measuring the sensor rail independently of the MCU rail. Victor has the Power
Profiler Kit II, which measures one rail at a time.

- **If the board already separates them** (or has a jumper / 0 Ω link on the sensor
  supply): nothing to do, and Phase 2 is straightforward.
- **If they share a rail**: cut a trace and fit a shunt, or wire the sensor supply
  through an external link. Unpleasant on an assembled board, so **check the schematic
  now** rather than after Phase 1.
- **If neither is possible**: the fallback is differential measurement — total energy
  with the sensor idle versus active, subtracting to attribute. Weaker, noisier, and
  it should be stated as a limitation rather than presented as direct measurement.

This is a hardware question with a paper-shaped consequence, which is why it is in
Phase 0 rather than Phase 2.

**Do not skip to Phase 1 because Phase 0 is boring.** Every one of these can force a
redesign, and all four are answerable in days with the eval board.

## Phase 1 — Bring-up on the custom board, staged

**Constraint (Victor, 2026-08-31): there is no DK and no ST evaluation board.** The
custom PCB carries the ISP2454-LL and the VL53L9CX together, and Victor writes the
Zephyr board files.

This is fine for the firmware architecture — a board file is a board file. It is *not*
fine for risk, and pretending otherwise is how bring-ups lose a month. Without a
known-good reference, the first time the sensor stays silent there are **four suspects
at once**: solder/assembly, board design, our driver port, and the sensor init
sequence. The DK existed to eliminate two of them.

**The replacement is a staged bring-up with hard gates.** Do not proceed past a gate
until it passes — each one eliminates a suspect, which is exactly what the reference
board would have done.

| Gate | Proves | How |
|---|---|---|
| **1.0 Board alive** | Power, clocks, SWD, Zephyr boots | Blink an LED, RTT prints |
| **1.1 Bus electrically sane** | Pull-ups, levels, wiring | **Logic analyser on SDA/SCL.** Not optional here — with no reference board this instrument *is* the reference |
| **1.2 Sensor ACKs its address** | Sensor powered, addressed, alive | I²C scan. **This is the milestone that separates hardware from software.** If it ACKs, the board is broadly right and every later bug is ours |
| **1.3 Device ID reads back correct** | Bus timing and register access | Read the ID register, compare to datasheet |
| **1.4 Firmware blob uploads and the sensor reports ready** | The hard part of init | Also yields the Phase 0.1 measurement for free |
| **1.5 One frame, any resolution** | End to end | Dump raw over RTT |
| **1.6 Frames render on a host** | Sanity of the data itself | Live depth-map viewer |

**Gate 1.2 is the important one.** An I²C scan that finds the sensor is cheap, takes an
afternoon, and converts "nothing works" into "the hardware is fine, keep debugging
software" — which is the single most valuable piece of information in the whole
project. Get there first, before any driver work.

### Substitutes for the missing reference board

- **Logic analyser** — mandatory, not a nice-to-have. It replaces the eval board as the
  arbiter of whether the sensor is responding.
- **`github.com/earlynerd/VL53L9-Arduino`** — a community port that reports working
  init, blob upload and frame reads. It is not our transport (they used I3C), but it is
  a **known-good init sequence** to diff ours against. This is now the closest thing to
  a reference implementation we have; read it before writing the port.
- **X-CUBE-53L9A1** — ST's own driver source. Read the init order from it even without
  the STM32 hardware.
- **A second populated board**, if any exist. Two boards failing identically means
  design; one failing means assembly. Worth knowing whether more than one exists.

### Then the driver

Port ST's ULD-style driver as a Zephyr out-of-tree module. There is no in-tree Zephyr
driver for this part; wrap ST's C driver behind a thin platform layer (`i2c_write`,
`i2c_read`, `wait_ms`) rather than rewriting it — a rewrite adds a fifth suspect.

**Pin the nRF Connect SDK version now** and record it in `DECISIONS.md`.

Build the **live depth-map viewer early**. With no reference hardware it is the main way
to tell a plausible-looking frame from a subtly wrong one, and it pays for itself many
times over.

**Done when:** a 54×42 frame renders on a laptop from the custom board, and the init
sequence is reproducible from cold power-on.

## Phase 2 — The measurement rig *(before the algorithm)*

The paper is an energy argument. The instrument comes before the thing being measured.

- **Power Profiler Kit II** (Victor has one) on the module rail, with the sensor rail
  separable — attributing energy to *sensor* versus *MCU+radio* is the whole point.
  Settled in Phase 0.5; if the rails are shared, a shunt goes in before Phase 2 rather
  than after.
- A **GPIO trace pin** toggled around each phase (sensor integration, I²C read,
  processing, BLE) so the power trace can be segmented by activity.
- **Repeatable capture:** scripted runs, fixed duration, results to CSV, in `tools/`.

**Done when:** energy per frame can be reported in µJ, split by phase, reproducibly.

## Phase 3 — Detection pipeline, classical first

Implemented on-device in C, mirrored in Python for offline work on recorded frames.

1. Per-zone **background depth model** (running median), confidence-gated.
2. **Foreground segmentation** against background.
3. **Connected-component clustering**; centroid and area per cluster.
4. **Track association** across frames, with track lifetime.
5. **Line-crossing counter** with direction; **occupancy** as live track count.

Record raw frames to a host during data collection so the offline copy can be re-run
against new algorithm versions without re-staging every experiment. **Recorded data is
the reusable asset here** — stage each scenario once.

**Done when:** counts match ground truth on a scripted walk-through.

## Phase 4 — The energy-accuracy sweep *(this is the paper)*

Systematically vary, measuring accuracy and energy at each point:

- **Resolution** — every mode found in 0.2
- **Frame rate** — 0.2, 0.5, 1, 2, 5 Hz
- **Duty strategy** — continuous low-rate vs. burst-on-event
- Optionally **integration time**, if exposed

Output: an **energy-per-correct-count Pareto front**, and the projected battery life at
each operating point. This is the contribution; everything before it is scaffolding.

## Phase 5 — BLE and the system demo

- Custom GATT service: count, direction, occupancy, dwell, battery, and a config
  characteristic for mode.
- **Counts only — never frames.** The privacy claim is architectural and free if the
  design never has a raw-frame path off the device.
- Connectionless **BLE advertising** for the periodic count is worth measuring against a
  connection: for a 1 Hz scalar it is very likely cheaper, and that is itself a
  reportable result.
- Phone or Raspberry Pi collector app, logging to CSV for the evaluation.

## Phase 6 — Long-run validation

- Multi-day deployment on a real doorway with independent ground truth (a second
  sensor, or timestamped video reviewed offline and then deleted).
- Battery-life measurement against the Phase 4 projection. **A measured discharge curve
  that matches a prediction is a far stronger claim than either alone.**

## Test setup — what to build

**Bench rig:**
- **Logic analyser on I²C** — with no reference board this is the primary diagnostic
  instrument, not an accessory
- Adjustable-height mount, 2.0–3.0 m, marked in 10 cm increments
- Floor tape marking the FoV footprint at the chosen height
- Fixed positions for repeatable single/double/abreast walk-throughs
- Power Profiler Kit II, sensor and MCU rails separated
- Host capture over RTT/UART, timestamped

**Scenario list — run every one at every operating point:**

| Scenario | Tests |
|---|---|
| Single person, normal walk | Baseline |
| Two abreast | **The resolution claim** — the headline experiment |
| Two in single file, close | Track separation |
| Walk, stop in FoV, continue | Background-model freeze |
| Person with trolley / large bag | False-positive shape handling |
| Dark clothing, dark hair | 940 nm absorption dropouts |
| Direct sunlight through the doorway | Ambient IR floor |
| Empty room, 1 hour | False-positive rate — **the most under-reported metric in this field** |
| Child-height target | Threshold sensitivity |

## Repository layout

```
firmware/          Zephyr application, nRF Connect SDK
  drivers/vl53l9cx/  out-of-tree Zephyr driver wrapping ST's ULD
  src/               sampling, detection, BLE
  boards/            ISP2454-LL board definition
tools/             host capture, frame viewer, energy post-processing
data/              recorded frame sets per scenario  (gitignored if large)
docs/              this research
notes/             YYYY-MM-DD.md session log
```

## Risks, honestly

| Risk | Severity | Mitigation |
|---|---|---|
| Blob load makes power-cycling uneconomic | **High** | Phase 0.1 answers it in days; standby-retain architecture if so |
| Only full resolution available | **High** — halves the paper | Phase 0.2; fall back to frame-rate and duty-cycle sweep as the axis |
| I²C at 400 kHz limits frame rate below what counting needs | Medium | Measure; reduced resolution; the application only wants ~1 Hz |
| No in-tree Zephyr driver; ST driver assumes STM32 HAL | Medium | Thin platform shim; ST's ULD is written to be portable |
| Shared power rail prevents attributing energy | **High** | Phase 0.5 — check the schematic now. Shunt if needed; differential measurement as a weak fallback |
| **Bring-up bugs confounded — no reference board** | **High** | Staged gates in Phase 1; logic analyser as the arbiter; gate 1.2 (I²C ACK) separates hardware from software early. Read the community port's init sequence before writing ours |
| Assembly fault on a one-off board mistaken for a design fault | Medium | Establish whether a second populated board exists. Two failing identically means design; one means assembly |
| 150 mW makes battery life unimpressive | Medium | It is the finding either way — report it honestly, that is the paper |
