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

**Do not skip to Phase 1 because Phase 0 is boring.** Every one of these can force a
redesign, and all four are answerable in days with the eval board.

## Phase 1 — Bring-up on known-good hardware

Goal: a frame on a screen, with the smallest possible number of unknowns.

1. **STM32 + STEVAL-VL53L9 / X-NUCLEO-53L9A1 with ST's X-CUBE-53L9A1.** Not our target
   platform — that is the point. It establishes that the sensor, the blob and our
   understanding of the init sequence are correct *before* introducing Zephyr, the
   nRF54L15 and a custom board simultaneously.
2. **nRF54L15-DK + sensor over I²C**, nRF Connect SDK, sensor firmware blob in flash.
   **Pin the SDK version now** and record it in `DECISIONS.md`.
3. Port ST's ULD-style driver as a Zephyr out-of-tree module. There is no in-tree
   Zephyr driver for this part; wrap ST's C driver behind a thin platform layer
   (`i2c_write`, `i2c_read`, `wait_ms`) rather than rewriting it.
4. **Stream raw frames to a host over UART/RTT** and view them. A live depth map is the
   single most valuable debugging artefact in the project — build it early, it pays for
   itself ten times.

**Done when:** a 54×42 frame renders on a laptop from the DK, and the init sequence is
reproducible from cold.

## Phase 2 — The measurement rig *(before the algorithm)*

The paper is an energy argument. The instrument comes before the thing being measured.

- **Power Profiler Kit II** on the module rail, with the sensor rail separable —
  attributing energy to *sensor* versus *MCU+radio* separately is the whole point, and
  it is impossible to retrofit if the board has one shared rail. **Check this against
  Victor's board now**; if it cannot be split, plan a shunt or a bodge wire.
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
| Shared power rail prevents attributing energy | Medium | **Check the board now**, before Phase 2 |
| Custom board bring-up bugs confounded with firmware bugs | Medium | DK first, always |
| 150 mW makes battery life unimpressive | Medium | It is the finding either way — report it honestly, that is the paper |
